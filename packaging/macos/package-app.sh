#!/bin/bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
build=${KRELLIX_BUILD_DIR:?Set KRELLIX_BUILD_DIR to the tested native Mac build}
qt=${KRELLIX_QT_PREFIX:?Set KRELLIX_QT_PREFIX to the Qt used for that build}
out=${KRELLIX_OUT_DIR:-"$root/dist/packages/macos"}
release=${KRELLIX_MACOS_RELEASE:-0.1.2-20261024.1}
stage=$(mktemp -d /tmp/krellix-package.XXXXXX)
app="$stage/Krellix.app"
mkdir -p "$app/Contents/"{MacOS,Helpers,Resources/cli,Resources/plugins/krellmoon,PlugIns/krellix} "$out"
cp "$build/bin/krellbt-battery" "$app/Contents/Helpers/"
cp "$build/bin/krellix" "$app/Contents/MacOS/"
for plugin in krellkam krelldacious krellweather krellwire krellmail krellmoon krellspectrum krellstock krellhealth krellstack krellpkg krellbluetooth; do
    cp "$build/plugins/lib$plugin.so" "$app/Contents/PlugIns/krellix/"
done
cp -R "$root/themes" "$app/Contents/Resources/"
cp "$root/plugins/krellmoon/assets/"*.png "$app/Contents/Resources/plugins/krellmoon/"
cp "$root/packaging/macos/krellix-all" "$app/Contents/Resources/cli/"
chmod 755 "$app/Contents/Resources/cli/krellix-all"
for category in platforms styles imageformats tls networkinformation; do
    if [[ -d "$qt/share/qt/plugins/$category" ]]; then
        cp -R "$qt/share/qt/plugins/$category" "$app/Contents/PlugIns/"
    fi
done
cat > "$app/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>org.cerberix.krellix</string>
<key>CFBundleName</key><string>Krellix</string>
<key>CFBundleExecutable</key><string>krellix</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>0.1.2</string>
<key>CFBundleVersion</key><string>20261024.1</string>
<key>LSMinimumSystemVersion</key><string>12.0</string>
<key>LSUIElement</key><true/>
<key>NSHighResolutionCapable</key><true/>
<key>NSBluetoothAlwaysUsageDescription</key><string>KrellBT reads battery levels from your connected Bluetooth accessories.</string>
<key>NSBluetoothPeripheralUsageDescription</key><string>KrellBT reads battery levels from your connected Bluetooth accessories.</string>
</dict></plist>
PLIST
printf '[Paths]\nPlugins = PlugIns\n' > "$app/Contents/Resources/qt.conf"
python3 "$root/packaging/macos/bundle-runtime.py" "$app"
/usr/bin/codesign --force --deep --sign "${KRELLIX_SIGN_IDENTITY:--}" "$app"
/usr/bin/codesign --verify --deep --strict "$app"
/usr/bin/osacompile -o "$stage/Krellix All.app" -e 'do shell script "/Applications/Krellix.app/Contents/Resources/cli/krellix-all start"'
/usr/libexec/PlistBuddy -c 'Add :LSUIElement bool true' "$stage/Krellix All.app/Contents/Info.plist"
/usr/bin/codesign --force --deep --sign "${KRELLIX_SIGN_IDENTITY:--}" "$stage/Krellix All.app"
cp "$root/packaging/macos/README.txt" "$stage/README.txt"
ln -s /Applications "$stage/Applications"
name="krellix-$release-Darwin-x86_64-selfcontained"
/usr/bin/hdiutil create -volname "Krellix $release" -srcfolder "$stage" -ov -format UDZO "$out/$name.dmg"
tar -czf "$out/$name.tar.gz" -C "$stage" Krellix.app 'Krellix All.app' README.txt
(cd "$out" && shasum -a 256 "$name.dmg" "$name.tar.gz" > "$name.sha256")
printf 'Stage: %s\nOutput: %s\n' "$stage" "$out/$name"
