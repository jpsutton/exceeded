# Exceeded

Exceeded is a **clean-room reimplementation of a game engine**: a new program, written from scratch, that can run the 1995 Windows
edutainment game *Super Solvers: OutNumbered!* on modern computers.

"Clean room" means the engine was written from a written description of how the original behaves, by people who never saw the original
program's code. Nothing from the original program was copied. The description, [`spec/`](spec/), is part of this repository.

Exceeded does **not** include the game. It reads the game's data from your own copy, so you need that copy to play.

## What you need

* **Your own copy of the game.** The Windows CD of *Super Solvers: OutNumbered!*, as a disc image (`.iso`, `.bin` + `.cue`) or a folder with the CD's contents.
* *(Optional)* The MS-DOS version of the game. If you add it, you can switch the music and sound effects to the original Tandy or
  PC-speaker sound in **Audio Options**.
* A computer running Linux, Windows 10 or 11 (64-bit), or macOS on Apple silicon. (Linux is where it has been used the most; the Windows and Mac builds are best effort.)

## Install

Builds are on the [Releases page](https://github.com/jpsutton/exceeded/releases/latest). Every file is signed, and `SHA256SUMS` lists their checksums.

### Arch Linux

Exceeded is in the jpsutton pacman repository. Add the repository once, then install:

```sh
curl -fsSLO https://jpsutton.github.io/arch-repo/setup.sh
bash setup.sh --install exceeded
```

After that, `pacman -Syu` keeps it up to date. You can also download the `.pkg.tar.zst` from the release and install it with `sudo pacman -U`.
Start it with `exceeded`, or from your applications menu.

### macOS

Apple silicon only. The build is best effort and isn't signed by Apple. The easiest way to install or update is the install script. It downloads the latest `.dmg`, checks it against the release's `SHA256SUMS`, and copies Exceeded into `/Applications`:

```sh
curl -fsSL https://raw.githubusercontent.com/jpsutton/exceeded/release/scripts/install-macos.sh | bash
```

Files downloaded with `curl` aren't quarantined, so Exceeded opens without a Gatekeeper prompt. Run the same command again to update.

If you download the `.dmg` in a browser instead, macOS says Apple could not verify that Exceeded is free of malware. After the first attempt to open it, go to **System Settings › Privacy & Security** and click **Open Anyway**. Or clear the quarantine from a terminal:

```sh
xattr -dr com.apple.quarantine /Applications/Exceeded.app
```

### Windows

Windows 10 or 11, 64-bit. The build is best effort and isn't signed, so Windows shows a warning the first time you run it.

1. Download `exceeded-<version>-windows-x64-setup.exe` from the [Releases page](https://github.com/jpsutton/exceeded/releases/latest) and run it.
2. If Windows SmartScreen says it protected your PC, click **More info**, then **Run anyway**.
3. Follow the installer. It installs for your user by default (no administrator prompt) and adds a Start menu shortcut. A desktop shortcut is optional.

To update, run the newer installer. To remove it, use **Settings › Apps** (or run the uninstaller from the install folder). Your players and settings are kept elsewhere (see below), so neither step loses them.

Prefer no installer? `exceeded-<version>-windows-x64.zip` is the same program: unzip it anywhere and run `exceeded.exe`.

### Build it yourself

You need a C compiler and these development packages: SDL2, FluidSynth, zlib and pkg-config.
On Windows, use an [MSYS2](https://www.msys2.org/) UCRT64 shell and `scripts/ci/build-windows.sh`, which also makes the `.zip`.
On Debian or Ubuntu:

```sh
sudo apt install build-essential pkg-config libsdl2-dev libfluidsynth-dev zlib1g-dev
```

Then:

```sh
git clone https://github.com/jpsutton/exceeded.git
cd exceeded
make
./engine
```

`sudo make install` installs it as `exceeded` under `/usr/local`.

## First start

The first time, a setup screen opens:

1. Drag your CD image (or the CD's folder) onto the window. You can also paste or type its location and press Enter.
2. If you have the DOS version, drag that on too. This step is optional.
3. Press **Install**. The game copies the files it needs (about 15 MB) and starts.

After that, starting it goes straight into the game.

## Playing

Everything works with the mouse, and the usual keys work too.

* The menu bar at the top is always there: **File**, **Options** and **Help**. Press **F10** or **Alt** to use it from the keyboard.
* **Tab** opens the decoder, where you decide who the Master of Mischief is and where he is hiding.
* **Esc** asks if you want to leave. Your game is saved when you quit, and you pick up where you left off the next time you sign in.
* **Ctrl+S** turns all sound on or off.
* Pick **Audio Options** from the Options menu for volume sliders, an "even out volume" switch (on by default, so nothing is
  suddenly loud), and the choice of music and sound style.

To start in full screen, run it with `--fullscreen`.

## Where your players and settings live

Everything is stored in your own folder: `~/.local/share/outnumbered` on Linux, `~/Library/Application Support/OutNumbered` on macOS and `%LOCALAPPDATA%\OutNumbered` on Windows. It holds:

* `game/` holds the installed game files
* `dos/` holds the DOS files, if you added them
* `saves/` holds your players, scores and settings

To reset the setup or add the DOS version later, run it with `--setup`.
To uninstall, delete that folder. **That also removes your saved players**, so copy `saves/` somewhere first if you want to keep them.

## Music

Music is played through FluidSynth using the free [GeneralUser GS](https://www.schristiancollins.com/generaluser.php) SoundFont,
which is included in the `soundfont/` folder (its license is in `soundfont/LICENSE.txt`). If you prefer another SoundFont, start with
`--soundfont /path/to/file.sf2`.

## License

Exceeded is free software under the [GNU General Public License, version 3](LICENSE). The bundled SoundFont has its own license (`soundfont/LICENSE.txt`). The game's own files are not part of this project and are not covered by it.

## Good to know

* Exceeded is an independent project. It is not made by, or connected to, the original publisher or developers.
* No game files (art, sound, music, text) are included or distributed here. You supply your own copy.
* Found a bug, or something that doesn't behave like the original game? Please open an issue.

## For developers

* `make` builds the `engine` program. `make dist` puts it next to the SoundFont and this README in `dist/`.
* The source is in `src/` (plain C). `third_party/font8x8` is a public-domain bitmap font used on the setup screen.
* [`spec/`](spec/) is the written specification the engine was built from: data formats, behavior, constants and algorithms. Start with
  [`spec/README.md`](spec/README.md), which also explains the two-team clean-room process. Its `fixtures/` are small test files made from
  scratch; none comes from the original game.
* `python3 tests/run_fixtures.py` checks the engine against those fixtures. `tests/mkdata.py` builds a small stand-in data set, so the
  engine can run without the real game.
* Work goes into `staging`. Merging `staging` into `release` builds the packages and publishes a release; the version comes from
  `ENGINE_VERSION` in `src/setup.h`, which must also match `pkgver` in `packaging/arch/PKGBUILD`.
