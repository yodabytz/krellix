#!/bin/bash
set -euo pipefail
[[ $(uname -s) == Darwin ]] || { echo 'macOS packaging must run on macOS.' >&2; exit 1; }
root=$(cd "$(dirname "$0")/.." && pwd)
arch=${KRELLIX_MACOS_ARCH:-x86_64}
[[ "$arch" == x86_64 ]] || { echo 'This release pipeline currently supports x86_64 only.' >&2; exit 1; }
export KRELLIX_BUILD_DIR=${KRELLIX_BUILD_DIR:-"$root/build/macos-$arch"}
export KRELLIX_QT_PREFIX=${KRELLIX_QT_PREFIX:?Set KRELLIX_QT_PREFIX to the native Qt installation}
cmake -S "$root" -B "$KRELLIX_BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON -DCMAKE_OSX_ARCHITECTURES="$arch" -DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-12.0}" -DCMAKE_PREFIX_PATH="$KRELLIX_QT_PREFIX"
cmake --build "$KRELLIX_BUILD_DIR" --target krellix settings-lifecycle remote-telemetry bluetooth-data bluetooth-monitor krellkam krelldacious krellweather krellwire krellmail krellmoon krellspectrum krellstock krellhealth krellstack krellpkg krellbluetooth krellbt-battery
cmake --build "$KRELLIX_BUILD_DIR" --target netport-count remote-reconnect
ctest --test-dir "$KRELLIX_BUILD_DIR" --output-on-failure
bash "$root/packaging/macos/package-app.sh"
