#!/usr/bin/env bash
# Print the program version, after checking that src/setup.h and the PKGBUILD hold the same one.
set -euo pipefail
cd "$(dirname "$0")/../.."
src=$(sed -n 's/^#define ENGINE_VERSION "\(.*\)"$/\1/p' src/setup.h)
pkg=$(sed -n 's/^pkgver=//p' packaging/arch/PKGBUILD)
if [ -z "$src" ] || [ "$src" != "$pkg" ]; then
  echo "version mismatch: src/setup.h=$src packaging/arch/PKGBUILD=$pkg" >&2
  exit 1
fi
echo "$src"
