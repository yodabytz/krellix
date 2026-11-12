# krellixd security (Linux release 0.1.2-29)

The daemon checks the actual kernel peer address before creating a Qt socket or
session. Non-allowlisted peers are reset without application data. This works
without a firewall, but application code cannot prevent the kernel TCP handshake,
SYN floods, or upstream bandwidth exhaustion. Keep a firewall as defense in depth.

Only literal IPs are accepted; invalid entries fail startup. IPv4-mapped IPv6 is
normalized. An empty allowlist refuses startup. Limits: 128 allowlist entries,
1..16 simultaneous clients, four admissions in a burst per source followed by
one per second, sampling 0.1..10 Hz, and output deadline 5..300 seconds.
The monitoring protocol is output-only. Any incoming application byte aborts
the session. Clients do not send requests or keepalive messages.

Only one output frame (at most 1 MiB) is queued. Slow readers cannot accumulate
samples or extend the frame deadline with incoming data or partial reads.
Connections are aborted, not gracefully drained, after errors or deadlines.
Reject logging is not emitted per connection. Process command-line arguments
are not exported; only process names remain. Helper command output is bounded.

Direct TCP remains supported and is NOT encrypted or cryptographically
authenticated. All machines sharing an allowed NAT IP are within that trust
boundary. SSH/VPN is optional; no launcher or protocol migration is required.

## Docker metadata migration

The daemon must not belong to the docker group or access Docker's control socket.
The installed zz-security.conf clears legacy SupplementaryGroups=docker grants.
Remove any administrator override that adds them back. Verify the effective
service groups and /proc/PID/status after restarting the service.

Enable the separate collector if Docker labels/counts are wanted:

    sudo systemctl daemon-reload
    sudo systemctl enable --now krellixd-docker-snapshot.timer
    sudo systemctl start krellixd-docker-snapshot.service
    sudo systemctl restart krellixd

The collector has no listening socket or client-controlled arguments. It performs
three fixed Docker GET operations and atomically publishes only selected labels
and aggregate counts in /run/krellixd-docker/snapshot.json. No container commands,
environment, mount paths or Docker credentials are published. The daemon reads
only root-owned, non-group/other-writable snapshots no older than 120 seconds.
Collector failure produces unavailable Docker metadata, never privilege fallback.

The collector requires Python 3. Its systemd unit limits runtime and resources.
Existing krellixd configurations, allowlists and client settings are preserved.
The GUI's local Docker behavior is unchanged; the network daemon has a separate
compile-time path that never opens docker.sock.

## Verification scope

tests/daemon-security.py exercises isolated loopback listeners: disallowed IPv4,
four clients, connection cap, reconnect, unexpected binary/JSON input, mapped IPv4,
native IPv6, non-reading clients, argument redaction, and invalid configuration.
Run this against packaged binaries as well as sanitizer builds. Testing does not
prove absence of every vulnerability; Qt/kernel dependencies must remain updated.
