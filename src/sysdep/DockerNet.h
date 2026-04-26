#pragma once

#include <QString>

// Maps Linux bridge interface names (docker0, br-<id12>) to their
// human-readable Docker network names (e.g. "bridge", "internal",
// "matrix_internal"). Non-Docker bridges return an empty alias.
//
// Looked up on demand via the local Docker daemon socket
// (/var/run/docker.sock); the result is cached so the cost is paid
// at most once every refresh interval. If the socket is unreachable
// (no Docker installed, no permission, daemon down) the lookup just
// returns empty strings — callers fall back to the raw interface name.
class DockerNet
{
public:
    static QString aliasForBridge(const QString &iface);
};
