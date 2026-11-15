#!/usr/bin/env bash
# CI (release): the Windows build and its .zip, written to out/. Run in an MSYS2 UCRT64 shell with
#   pacman -S mingw-w64-ucrt-x86_64-{gcc,make,pkgconf,SDL2,fluidsynth,zlib} zip
#   scripts/ci/build-windows.sh
# The .zip holds exceeded.exe, the DLLs it needs (found with ldd), the licenses of the packages they come from,
# the SoundFont, README.md and LICENSE. Also the NSIS installer exceeded-<version>-windows-x64-setup.exe.
set -euo pipefail
cd "$(dirname "$0")/../.."
version=$(scripts/ci/version.sh)

make clean
make

name=exceeded-$version-windows-x64
dist=out/$name
rm -rf out
mkdir -p "$dist/soundfont" "$dist/licenses"
cp engine.exe "$dist/exceeded.exe"
cp soundfont/* "$dist/soundfont/"
cp README.md LICENSE "$dist/"

# The libraries it links, and what they link, from the MSYS2 prefix (not the ones that come with Windows).
prefix=$(cygpath -m "$MSYSTEM_PREFIX")
ldd engine.exe | awk '/=>/ {print $3}' | grep -i "^$MSYSTEM_PREFIX/\|^/ucrt64/" | sort -u > out/dlls.txt || true
if [ ! -s out/dlls.txt ]; then
  echo "no DLLs found with ldd; is this an MSYS2 UCRT64 shell?" >&2
  exit 1
fi
while read -r dll; do
  cp "$dll" "$dist/"
  pkg=$(pacman -Qoq "$dll" 2>/dev/null || true)
  if [ -n "$pkg" ] && [ -d "$MSYSTEM_PREFIX/share/licenses/${pkg#*-x86_64-}" ]; then
    mkdir -p "$dist/licenses/${pkg#*-x86_64-}"
    cp -r "$MSYSTEM_PREFIX/share/licenses/${pkg#*-x86_64-}/." "$dist/licenses/${pkg#*-x86_64-}/"
  fi
done < out/dlls.txt
rm -f out/dlls.txt
echo "prefix $prefix"
ls "$dist"

(cd out && zip -qr "$name.zip" "$name")

# The installer (NSIS, per user by default). makensis comes with the GitHub Windows runners; elsewhere install NSIS
# (choco install nsis) or set MAKENSIS to its path.
makensis=${MAKENSIS:-}
if [ -z "$makensis" ]; then
  if command -v makensis >/dev/null 2>&1; then makensis=makensis
  elif [ -x "/c/Program Files (x86)/NSIS/makensis.exe" ]; then makensis="/c/Program Files (x86)/NSIS/makensis.exe"
  elif [ -x "/c/Program Files/NSIS/makensis.exe" ]; then makensis="/c/Program Files/NSIS/makensis.exe"
  fi
fi
if [ -n "$makensis" ]; then
  MSYS2_ARG_CONV_EXCL='*' "$makensis" -V2 "-DVERSION=$version" "-DSRCDIR=$(cygpath -w "$PWD/$dist")" \
    "-DOUTFILE=$(cygpath -w "$PWD/out/$name-setup.exe")" "$(cygpath -w "$PWD/packaging/windows/exceeded.nsi")"
else
  echo "makensis not found: no installer built (only the .zip)" >&2
  [ -z "${REQUIRE_INSTALLER:-}" ] || exit 1
fi
ls -la out
