import importlib.util
import io
import stat
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import call, patch


script = Path(__file__).resolve().parents[1] / 'packaging' / 'docker-snapshot.py'
spec = importlib.util.spec_from_file_location('docker_snapshot', script)
collector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(collector)


class ImageCacheTest(unittest.TestCase):
    def test_fresh_cache_skips_image_request_but_refreshes_status(self):
        cached = {'images': 4, 'images_collected_at': 1000, 'running': 0}
        containers = [{'State': 'running'}, {'State': 'exited'}]
        with patch.object(collector, 'read_snapshot', return_value=cached), \
                patch.object(collector.time, 'time', return_value=1299), \
                patch.object(collector, 'fetch', side_effect=[[], containers]) as fetch:
            result = collector.collect('/unused/path')
        self.assertEqual(fetch.call_args_list, [call('/networks'), call('/containers/json?all=1')])
        self.assertEqual((result['containers'], result['running'], result['unhealthy']), (2, 1, 1))
        self.assertEqual((result['images'], result['images_collected_at']), (4, 1000))
        self.assertEqual(result['collected_at'], 1299)

    def test_cache_expires_at_five_minutes(self):
        with patch.object(collector, 'fetch', return_value=[{}, {}]) as fetch:
            self.assertEqual(collector.image_count({'images': 9, 'images_collected_at': 1000}, 1300), (2, 1300))
        fetch.assert_called_once_with('/images/json')

    def test_legacy_or_invalid_cache_is_refreshed(self):
        for cached in ({}, {'images': 9}, {'images': True, 'images_collected_at': 1000},
                       {'images': -1, 'images_collected_at': 1000},
                       {'images': 9, 'images_collected_at': 1002}):
            with self.subTest(cached=cached), patch.object(collector, 'fetch', return_value=[]):
                self.assertEqual(collector.image_count(cached, 1001), (0, 1001))

    def test_failed_image_query_preserves_previous_count_and_expiry(self):
        cached = {'images': 5, 'images_collected_at': 1000}
        with patch.object(collector, 'fetch', side_effect=TimeoutError), patch('sys.stderr', new_callable=io.StringIO):
            self.assertEqual(collector.image_count(cached, 1300), (5, 1000))
            with self.assertRaises(TimeoutError):
                collector.image_count({}, 1300)

    def test_read_previous_snapshot_is_bounded_and_root_owned(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'snapshot.json'
            path.write_text('{"images":4,"images_collected_at":1000}', encoding='utf-8')
            for uid, mode, accepts in ((0, 0o644, True), (1000, 0o644, False), (0, 0o666, False)):
                info = SimpleNamespace(st_uid=uid, st_mode=stat.S_IFREG | mode)
                with self.subTest(uid=uid, mode=mode), patch.object(collector.os, 'fstat', return_value=info):
                    self.assertEqual(bool(collector.read_snapshot(path)), accepts)
            info = SimpleNamespace(st_uid=0, st_mode=stat.S_IFREG | 0o644)
            with patch.object(collector.os, 'fstat', return_value=info), patch.object(collector, 'MAX_BYTES', 8):
                self.assertEqual(collector.read_snapshot(path), {})
            link = Path(directory) / 'link'
            link.symlink_to(path)
            self.assertEqual(collector.read_snapshot(link), {})


if __name__ == '__main__':
    unittest.main()
