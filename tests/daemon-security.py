#!/usr/bin/env python3
"""Bounded localhost-only daemon security and compatibility regression tests."""
import contextlib
import json
import os
import socket
import subprocess
import sys
import tempfile
import time

BINARY = sys.argv[1]
BASE = [BINARY] + (['--allow-root'] if os.geteuid() == 0 else [])

@contextlib.contextmanager
def daemon(allow='127.0.0.1', host='127.0.0.1', cap=4):
    family = socket.AF_INET6 if ':' in host else socket.AF_INET
    with socket.socket(family) as reservation:
        reservation.bind((host, 0))
        port = reservation.getsockname()[1]
    with tempfile.TemporaryFile() as log:
        process = subprocess.Popen(BASE + ['--config', '/dev/null', '--listen', host,
            '--port', str(port), '--allow', allow, '--max-clients', str(cap),
            '--update-hz', '10', '--io-timeout', '5'], stdout=log, stderr=log)
        try:
            time.sleep(0.5)
            assert process.poll() is None, 'startup failed'
            yield process, port
            assert process.poll() is None, 'daemon crashed'
        finally:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            log.seek(0)
            output = log.read().decode(errors='replace')
            assert 'AddressSanitizer' not in output and 'runtime error:' not in output, output

def connect(port, source='127.0.0.1', host='127.0.0.1', small=False):
    s = socket.socket(socket.AF_INET6 if ':' in host else socket.AF_INET)
    s.settimeout(20)
    if small:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
    s.bind((source, 0))
    s.connect((host, port))
    return s

def closed(s):
    try:
        while s.recv(65536):
            pass
        return True
    except ConnectionResetError:
        return True

def sample(s):
    data = b''
    until = time.monotonic() + 20
    while time.monotonic() < until:
        block = s.recv(65536)
        assert block, 'closed while streaming'
        data += block
        while b'\n' in data:
            line, data = data.split(b'\n', 1)
            obj = json.loads(line)
            if obj.get('type') == 'sample':
                for row in obj.get('top', []):
                    assert row.get('cmd') == row.get('name'), 'arguments leaked'
                return obj
    raise AssertionError('no sample')

with daemon() as (_, port):
    with connect(port, '127.0.0.2') as s:
        assert closed(s)
    clients = [connect(port) for _ in range(4)]
    try:
        for s in clients:
            assert sample(s)
        with connect(port) as excess:
            assert closed(excess)
    finally:
        for s in clients:
            s.close()
    time.sleep(1.1)
    with connect(port) as s:
        assert sample(s)
print('PASS: denied IP, four clients, cap, reconnect', flush=True)

for payload in (b'\0', b'{"type":"sample"}\n', os.urandom(65536)):
    with daemon() as (_, port):
        with connect(port) as s:
            s.sendall(payload)
            assert closed(s)
        time.sleep(1.1)
        with connect(port) as s:
            assert sample(s)
print('PASS: single byte, fake JSON, random burst rejected; daemon usable', flush=True)

with daemon(allow='::ffff:127.0.0.1') as (_, port):
    with connect(port) as s:
        assert sample(s)
with daemon(allow='::1', host='::1') as (_, port):
    with connect(port, source='::1', host='::1') as s:
        assert sample(s)
print('PASS: mapped IPv4 and native IPv6', flush=True)

with daemon(cap=1) as (_, port):
    with connect(port, small=True) as slow:
        time.sleep(20)
        with connect(port) as fresh:
            assert sample(fresh)
print('PASS: non-reader evicted and slot reusable', flush=True)

for options in (['--allow', 'not-an-IP'], ['--allow', '0.0.0.0'],
                ['--max-clients', '100000000'], ['--io-timeout', '2147483647'],
                ['--update-hz', 'nan']):
    result = subprocess.run(BASE + ['--config', '/dev/null', '--allow', '127.0.0.1'] + options,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5)
    assert result.returncode != 0
print('PASS: invalid security limits fail closed', flush=True)
