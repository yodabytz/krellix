#!/usr/bin/env sh
set -eu

if [ "$(uname -s)" != "Darwin" ]; then
    printf '%s\n' "macOS packaging must run on macOS." >&2
    exit 1
fi

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
arch=${KRELLIX_MACOS_ARCH:-x86_64}
deployment_target=${MACOSX_DEPLOYMENT_TARGET:-12.0}
build_dir=${KRELLIX_BUILD_DIR:-"$root/build/macos-$arch"}
out_dir=${KRELLIX_OUT_DIR:-"$root/dist/packages/macos"}
qt_prefix=${KRELLIX_QT_PREFIX:-}

mkdir -p "$out_dir"

cmake_args="
    -S $root
    -B $build_dir
    -G Ninja
    -DCMAKE_BUILD_TYPE=RelWithDebInfo
    -DCMAKE_OSX_ARCHITECTURES=$arch
    -DCMAKE_OSX_DEPLOYMENT_TARGET=$deployment_target
    -DCPACK_GENERATOR=DragNDrop;TGZ
    -DCMAKE_INSTALL_RPATH=@executable_path/../Frameworks
    -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON
"

if [ -n "$qt_prefix" ]; then
    cmake_args="$cmake_args -DCMAKE_PREFIX_PATH=$qt_prefix"
fi

# shellcheck disable=SC2086
cmake $cmake_args
cmake --build "$build_dir"

(
    cd "$build_dir"
    cpack -G "DragNDrop;TGZ" -B "$out_dir"
)

printf '%s\n' "Krellix macOS packages written to $out_dir"
