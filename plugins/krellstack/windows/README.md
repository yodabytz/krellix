# KrellStack for Windows 0.1.1-windows.3

Requires the exact matching Krellix 0.1.2 Windows preview.2 x86_64 host. Existing
Linux/macOS implementations and host binaries are unchanged.

Close Krellix, extract the ZIP and run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\install-krellstack.ps1 -KrellixDirectory C:\path\to\Krellix
```

Enable KrellStack independently in each instance's Settings / Plugins. The
installer checks hashes and host compatibility, permits verified newer plugin
releases, refuses downgrades or unknown existing builds, and does not modify settings. The Qt helper
must remain next to krellix.exe to use its existing runtime libraries.

## Local Windows

Checks run in a separate, bounded helper, refreshed 30 seconds after completion.
Individual CLI calls have five-second deadlines (seven for explicit SSH scans),
with a 65-second overall deadline. A Windows job object ensures helper descendants
are terminated on timeout/shutdown. No service, runtime, container or image is
started, installed, upgraded, removed or otherwise changed.

- Docker: uses the local Windows docker_engine named pipe explicitly. Remote
  DOCKER_HOST/DOCKER_CONTEXT settings do not redirect this local monitor.
- Podman: requires a default local machine connection using loopback SSH.
  Nonlocal connections are reported unavailable rather than silently queried.
  Podman Compose projects are not counted in this version.
- Kubernetes: queries all namespaces in the selected local context. Allowed
  endpoints are localhost, 127.0.0.1, ::1 and kubernetes.docker.internal. Other
  clusters are not queried. Pods have their own count, not Docker container counts.
- Native .exe clients are discovered from absolute PATH directories and standard
  Docker/Podman installation paths. No local runtime is installed automatically.

Shows running/total containers, unique images, Compose projects derived from
Docker container labels (including stopped projects), networks and attention
counts. Attention includes unhealthy/restarting/dead/exited/stopped containers
and non-ready/non-completed Kubernetes pods. A stopped container may be intentional;
this is not a declaration that it is broken. Hover for per-source details.
The panel spells out running counts, images, projects, and stopped/unhealthy
counts instead of using the ambiguous abbreviations "ctr", "img", or "bad".

Failed, unavailable or malformed queries are never shown as a healthy empty
engine. If some sources succeed and others fail, totals include only successes
and the panel displays an exclamation mark with errors in its tooltip. Counts
from different runtimes are separate inventories; shared workloads can overlap.

## Explicit SSH Hosts

The existing Remote Docker SSH hosts box is supported for local instances.
Enter up to four comma/semicolon/newline-separated hostnames or user@host entries.
SSH aliases are supported. Use your Windows OpenSSH config for nonstandard ports
and identity files. Shell syntax and command-line options are rejected.

Only explicit hosts are queried, using fixed read-only Docker commands. Existing
trusted host keys and noninteractive authentication are required; unknown keys
are not accepted automatically. Docker must be readable for that remote account.
Local runtimes are not mixed into an explicit-host scan. Settings changes cancel
the previous query and trigger a new one without restarting Krellix.

## Remote Krellix Instances

An instance launched with --host consumes only its daemon's stack telemetry on
the existing core connection. Local CLI settings and additional SSH hosts are
ignored. Disconnects and errors clear previous counts; missing telemetry is
unavailable. No local fallback or extra connection is created. Server network
counts describe interfaces, not Docker network objects, and server-side cached
snapshot freshness/permissions still apply.

## Validation Scope

Tests cover real DLL loading, Windows helper/subprocess cleanup, structured
Docker/Podman/Kubernetes CLI fixtures, safe host input, missing/error/partial/
oversized responses, timeout, narrow layout, settings changes and remote
disconnect/reconnect. Live daemon monitoring is tested on the existing servers.
Missing-runtime behavior is verified. The local Docker Desktop test was blocked
by WSL VM creation timeouts, so live local Docker enumeration remains unverified.
Live local Podman/Kubernetes enumeration also remains unverified. Fixture tests
are not a substitute for engine integration checks. Quantumbytz's live remote
Docker counts and vibrixmedia's zero-container state were verified on Windows
with local Docker stopped. No extra remote connection or local fallback is used.
