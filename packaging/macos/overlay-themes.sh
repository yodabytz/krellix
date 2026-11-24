#!/bin/bash
set -euo pipefail
root=$(cd "$(dirname "$0")" && pwd)
app=${1:?Pass the staged Krellix.app path}
themes="$app/Contents/Resources/themes"
[[ "${app##*/}" == Krellix.app && -d "$themes" && ! -L "$themes" ]] || {
    echo 'Expected a staged Krellix.app with bundled themes.' >&2
    exit 1
}

for manifest in "$root/themes/"*/theme.json; do
    [[ -f "$manifest" ]] || continue
    override=${manifest%/theme.json}
    target="$themes/${override##*/}"
    rm -rf -- "$target"
    /usr/bin/ditto "$override" "$target"
done
