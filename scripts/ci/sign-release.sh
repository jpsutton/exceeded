#!/usr/bin/env bash
# CI (release): sign release assets.
#   sign-release.sh <dir>
# Needs EXCEEDED_GPG_KEY (armored private key; the release workflow passes the repo secret).
# For every file in <dir>: a detached .sig. Also SHA256SUMS (+ .sig). jpsutton/arch-repo checks
# the Arch package's .sig before adding it to the jpsutton pacman repository. Runs in an
# archlinux container.
set -euo pipefail
dir=${1:?asset dir}
: "${EXCEEDED_GPG_KEY:?EXCEEDED_GPG_KEY is not set}"

pacman -Sy --noconfirm --needed gnupg >/dev/null
export GNUPGHOME
GNUPGHOME=$(mktemp -d)
trap 'gpgconf --kill all; rm -rf "$GNUPGHOME"' EXIT
gpg --batch --quiet --import <<<"$EXCEEDED_GPG_KEY"
fpr=$(gpg --batch --with-colons --list-secret-keys | awk -F: '/^fpr/ {print $10; exit}')
echo "signing with $fpr"

cd "$dir"
rm -f ./*.sig SHA256SUMS*

for file in ./*; do
  [[ -f $file && $file != *.sig ]] || continue
  [[ -e $file.sig ]] || gpg --batch --yes --detach-sign --no-armor "$file"
done
sha256sum -- * | grep -v ' SHA256SUMS' > SHA256SUMS
gpg --batch --yes --detach-sign --no-armor SHA256SUMS
gpg --batch --armor --export "$fpr" > exceeded-signing-key.asc

# Prove it: every signature verifies against the imported key.
for sig in ./*.sig; do
  gpg --batch --quiet --verify "$sig" "${sig%.sig}"
done
ls -la
