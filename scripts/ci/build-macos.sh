#!/usr/bin/env bash
# CI (release): the macOS app and its .dmg, written to out/. Run on an Apple silicon Mac with
# Homebrew: brew install sdl2 fluid-synth zlib pkgconf dylibbundler
#   scripts/ci/build-macos.sh
# The app carries its libraries (SDL2, FluidSynth and what they need) and the SoundFont, and is
# ad-hoc signed: without a sealed bundle, a downloaded copy fails Gatekeeper as "damaged"
# instead of "unverified".
set -euo pipefail
cd "$(dirname "$0")/../.."
version=$(scripts/ci/version.sh)

make clean
make

app=out/Exceeded.app
rm -rf out
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources" "$app/Contents/Frameworks"
cp engine "$app/Contents/MacOS/exceeded"
cp -R soundfont "$app/Contents/Resources/"
cat > "$app/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>Exceeded</string>
  <key>CFBundleDisplayName</key><string>Exceeded</string>
  <key>CFBundleIdentifier</key><string>io.github.jpsutton.exceeded</string>
  <key>CFBundleExecutable</key><string>exceeded</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>${version}</string>
  <key>CFBundleVersion</key><string>${version}</string>
  <key>LSMinimumSystemVersion</key><string>13.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.educational-games</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST

# Copy the libraries the program links into the app and point it at them.
dylibbundler -od -b -x "$app/Contents/MacOS/exceeded" -d "$app/Contents/Frameworks" -p @executable_path/../Frameworks

codesign --force --deep --sign - "$app"
codesign --verify --deep --strict --verbose=2 "$app"

# Every library the program links must now be in the app or part of macOS itself.
outside=$(otool -L "$app/Contents/MacOS/exceeded" | tail -n +2 | awk '{print $1}' | grep -Ev '^(@executable_path|/usr/lib/|/System/)' || true)
if [ -n "$outside" ]; then
  echo "the program still links libraries outside the app:" >&2
  echo "$outside" >&2
  exit 1
fi

stage=$(mktemp -d)
ditto "$app" "$stage/Exceeded.app"
ln -s /Applications "$stage/Applications"
hdiutil create -volname "Exceeded $version" -srcfolder "$stage" -ov -format UDZO "out/exceeded-$version-macos-arm64.dmg"
rm -rf "$stage"
ls -la out
