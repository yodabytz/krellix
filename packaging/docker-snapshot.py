#!/usr/bin/python3
"""Root-only fixed-purpose Docker collector. No network listener or caller input."""
import http.client
import json
import os
import socket
import tempfile
import time

MAX_BYTES = 1024 * 1024


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


def snapshot(networks, containers, images):
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
            'containers': len(containers), 'images': len(images), 'compose': len(projects),
            'running': sum(c.get('State') == 'running' for c in containers),
            'unhealthy': sum(c.get('State') in ('restarting', 'exited', 'dead')
                             or 'unhealthy' in str(c.get('Status', '')).lower() for c in containers)}


def main():
    if os.geteuid() != 0:
        raise SystemExit('Collector must run as root')
    result = snapshot(fetch('/networks'), fetch('/containers/json?all=1'), fetch('/images/json'))
    data = json.dumps(result, separators=(',', ':')).encode()
    if len(data) > MAX_BYTES:
        raise SystemExit('Snapshot exceeded limit')
    directory = '/run/krellixd-docker'
    os.makedirs(directory, mode=0o755, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.snapshot-', dir=directory)
    try:
        with os.fdopen(fd, 'wb') as output:
            output.write(data)
            output.flush()
            os.fchmod(output.fileno(), 0o644)
        os.replace(temporary, directory + '/snapshot.json')
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


if __name__ == '__main__':
    main()
