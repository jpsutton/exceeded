#!/usr/bin/env bash
# CI (release): Arch package via packaging/arch/PKGBUILD, written to out/.
#   docker run --rm -v "$PWD:/src" -w /src archlinux:latest scripts/ci/build-arch.sh
set -euo pipefail
pacman -Syu --noconfirm --needed base-devel sdl2 fluidsynth zlib pkgconf git >/dev/null

# makepkg refuses to run as root.
id builder >/dev/null 2>&1 || useradd -m builder
chown -R builder: "$PWD"
su builder -c 'git config --global --add safe.directory "*" && cd packaging/arch && makepkg -f --nodeps --noconfirm'
mkdir -p out
mv packaging/arch/*.pkg.tar.zst out/
ls -la out
