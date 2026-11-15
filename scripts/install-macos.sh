#!/usr/bin/env bash
# Install or update Exceeded on macOS (Apple silicon) from the latest release:
#   curl -fsSL https://raw.githubusercontent.com/jpsutton/exceeded/release/scripts/install-macos.sh | bash
#
# Files that curl downloads aren't quarantined, so the app opens without a Gatekeeper prompt.
# The .dmg is checked against the release's SHA256SUMS. Installs into $EXCEEDED_INSTALL_DIR,
# else /Applications if writable, else ~/Applications. Run it again to update.
set -euo pipefail

base=https://github.com/jpsutton/exceeded/releases/latest/download

die() {
  echo "install-macos.sh: $*" >&2
  exit 1
}

main() {
  [[ $(uname -s) == Darwin ]] || die "this script is for macOS"
  [[ $(uname -m) == arm64 ]] || die "Exceeded ships for Apple silicon (arm64) only"

  local dir=${EXCEEDED_INSTALL_DIR:-}
  if [[ -z $dir ]]; then
    if [[ -w /Applications ]]; then dir=/Applications; else dir=$HOME/Applications; fi
  fi
  mkdir -p "$dir"

  local work dmg mnt app
  work=$(mktemp -d "${TMPDIR:-/tmp}/exceeded-install.XXXXXX")
  mnt=$work/mnt
  curl -fsSL -o "$work/SHA256SUMS" "$base/SHA256SUMS"
  dmg=$(awk '$2 ~ /\.dmg$/ { print $2; exit }' "$work/SHA256SUMS")
  [[ -n $dmg ]] || die "the latest release has no macOS .dmg"
  echo "Downloading $dmg"
  curl -fL --progress-bar -o "$work/$dmg" "$base/$dmg"
  (cd "$work" && grep " $dmg\$" SHA256SUMS | shasum -a 256 -c -s) || die "checksum mismatch for $dmg"

  mkdir "$mnt"
  hdiutil attach -quiet -nobrowse -readonly -mountpoint "$mnt" "$work/$dmg"
  app=$(find "$mnt" -maxdepth 1 -name '*.app' | head -n 1)
  if [[ -z $app ]]; then
    hdiutil detach -quiet "$mnt" || true
    die "no app in $dmg"
  fi

  # Replace the old copy only once the new one is fully copied.
  local dest=$dir/$(basename "$app")
  rm -rf "$dest.new" "$dest.old"
  ditto "$app" "$dest.new"
  hdiutil detach -quiet "$mnt"
  [[ -e $dest ]] && mv "$dest" "$dest.old"
  mv "$dest.new" "$dest"
  rm -rf "$dest.old" "$work"
  echo "Installed $dest"
}

# Everything runs from main, so a download cut short by `curl | bash` runs nothing.
main "$@"
