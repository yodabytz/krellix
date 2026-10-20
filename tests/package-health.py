#!/usr/bin/env python3
"""Exercise the real daemon with controlled dpkg audit responses."""
import json, os, pathlib, socket, subprocess, sys, tempfile, time
binary = sys.argv[1]
for case, script, expected in [
    ('healthy', 'exit 0', None),
    ('unconfigured', "echo 'The following packages have been unpacked but not yet configured:'; echo ' example-package'; exit 0", 'Package configuration incomplete'),
    ('failed-check', 'exit 2', 'Package check unavailable'),
]:
    with tempfile.TemporaryDirectory() as folder:
        command = pathlib.Path(folder, 'dpkg')
        command.write_text('#!/bin/sh\n' + script + '\n')
        command.chmod(0o700)
        with socket.socket() as port_socket:
            port_socket.bind(('127.0.0.1', 0))
            port = port_socket.getsockname()[1]
        env = dict(os.environ, PATH=folder + ':' + os.environ['PATH'])
        with open(os.devnull, 'wb') as log:
            proc = subprocess.Popen([binary, '--config', '/dev/null', '--listen', '127.0.0.1', '--port', str(port), '--allow', '127.0.0.1'], env=env, stdout=log, stderr=log)
            try:
                for attempt in range(100):
                    try:
                        conn = socket.create_connection(('127.0.0.1', port), timeout=1)
                        break
                    except OSError:
                        time.sleep(.05)
                else:
                    raise AssertionError('daemon did not start')
                with conn:
                    conn.settimeout(40)
                    stream = conn.makefile('r')
                    while True:
                        item = json.loads(stream.readline())
                        if item.get('type') == 'sample':
                            break
                health = item['health']
                text = json.dumps(health)
                assert 'Package transaction errors' not in text, text
                if expected:
                    assert expected in text, text
                else:
                    assert 'Package configuration incomplete' not in text and 'Package check unavailable' not in text, text
                print('PASS:', case)
            finally:
                proc.terminate()
                proc.wait(timeout=5)
