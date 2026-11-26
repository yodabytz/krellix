#!/usr/bin/python3
"""Root-only fixed-purpose Docker collector. No network listener or caller input."""
import http.client
import json
import os
import socket
import stat
import sys
import tempfile
import time

MAX_BYTES = 1024 * 1024
IMAGE_REFRESH_SECONDS = 300


class DockerConnection(http.client.HTTPConnection):
    def connect(self):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(3)
        self.sock.connect('/run/docker.sock')


def fetch(path):
    connection = DockerConnection('localhost', timeout=3)
    try:
        connection.request('GET', path)
        response = connection.getresponse()
        data = response.read(MAX_BYTES + 1)
        if response.status != 200 or len(data) > MAX_BYTES:
            raise ValueError('Docker response failed or exceeded limit')
        result = json.loads(data)
        if not isinstance(result, list):
            raise ValueError('Invalid Docker response')
        return result
    finally:
        connection.close()


def read_snapshot(path):
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        with os.fdopen(fd, 'rb') as previous:
            info = os.fstat(previous.fileno())
            if (not stat.S_ISREG(info.st_mode) or info.st_uid != 0
                    or info.st_mode & (stat.S_IWGRP | stat.S_IWOTH)):
                return {}
            data = previous.read(MAX_BYTES + 1)
        if len(data) <= MAX_BYTES:
            result = json.loads(data)
            if isinstance(result, dict):
                return result
    except (OSError, ValueError, RecursionError):
        pass
    return {}


def image_count(previous, now):
    count = previous.get('images')
    collected = previous.get('images_collected_at')
    valid = (type(count) is int and 0 <= count <= MAX_BYTES
             and type(collected) is int and 0 < collected <= now)
    if valid and now - collected < IMAGE_REFRESH_SECONDS:
        return count, collected
    try:
        return len(fetch('/images/json')), now
    except (OSError, http.client.HTTPException, ValueError, RecursionError):
        if not valid:
            raise
        print('Docker image count refresh failed; retaining previous count', file=sys.stderr)
        return count, collected


def snapshot(networks, containers, image_total, images_collected_at):
    safe_networks = []
    for network in networks:
        if network.get('Driver') != 'bridge':
            continue
        options = network.get('Options') or {}
        safe_networks.append({
            'Driver': 'bridge', 'Name': str(network.get('Name', ''))[:128],
            'Id': str(network.get('Id', ''))[:64],
            'Options': {'com.docker.network.bridge.name':
                str(options.get('com.docker.network.bridge.name', ''))[:64]},
        })
    projects = {str((c.get('Labels') or {}).get('com.docker.compose.project'))
                for c in containers if (c.get('Labels') or {}).get('com.docker.compose.project')}
    return {'collected_at': int(time.time()), 'networks': safe_networks,
            'containers': len(containers), 'images': image_total,
            'images_collected_at': images_collected_at, 'compose': len(projects),
            'running': sum(c.get('State') == 'running' for c in containers),
            'unhealthy': sum(c.get('State') in ('restarting', 'exited', 'dead')
                             or 'unhealthy' in str(c.get('Status', '')).lower() for c in containers)}


def collect(path):
    networks = fetch('/networks')
    containers = fetch('/containers/json?all=1')
    count, count_at = image_count(read_snapshot(path), int(time.time()))
    return snapshot(networks, containers, count, count_at)


def main():
    if os.geteuid() != 0:
        raise SystemExit('Collector must run as root')
    directory = '/run/krellixd-docker'
    path = os.path.join(directory, 'snapshot.json')
    result = collect(path)
    data = json.dumps(result, separators=(',', ':')).encode()
    if len(data) > MAX_BYTES:
        raise SystemExit('Snapshot exceeded limit')
    os.makedirs(directory, mode=0o755, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.snapshot-', dir=directory)
    try:
        with os.fdopen(fd, 'wb') as output:
            output.write(data)
            output.flush()
            os.fchmod(output.fileno(), 0o644)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


if __name__ == '__main__':
    main()
