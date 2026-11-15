# Exceeded

A modern, open-source player for **Super Solvers: OutNumbered!** (the 1995 Windows version), the math-adventure game where you chase the
Master of Mischief through his hideout, solving math problems to collect clues.

Exceeded is a new game engine written from scratch. It does **not** include the game itself. You need your own copy of the game
(the Windows CD) to play.

## What you need

* **Your copy of the game.** The Windows CD, as a disc image (`.iso`, `.bin` + `.cue`) or a folder with the CD's contents.
* *(Optional)* The MS-DOS version of the game. If you add it, you can switch the music and sound effects to the original Tandy or
  PC-speaker sound in **Audio Options**.
* A Linux computer. (This is the only system it has been built and tested on so far.)

## Get it running

You need a C compiler and these development packages: SDL2, FluidSynth, zlib and pkg-config.
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

## First start

The first time, a setup screen opens:

1. Drag your CD image (or the CD's folder) onto the window. You can also paste or type its location and press Enter.
2. If you have the DOS version, drag that on too. This step is optional.
3. Press **Install**. The game copies the files it needs (about 15 MB) and starts.

After that, `./engine` goes straight into the game.

## Playing

Everything works with the mouse, and the usual keys work too.

* The menu bar at the top is always there: **File**, **Options** and **Help**. Press **F10** or **Alt** to use it from the keyboard.
* **Tab** opens the decoder, where you decide who the Master of Mischief is and where he is hiding.
* **Esc** asks if you want to leave. Your game is saved when you quit, and you pick up where you left off the next time you sign in.
* **Ctrl+S** turns all sound on or off.
* Pick **Audio Options** from the Options menu for volume sliders, an "even out volume" switch (on by default, so nothing is
  suddenly loud), and the choice of music and sound style.

To start in full screen, run `./engine --fullscreen`.

## Where your players and settings live

Everything is stored in your own folder, by default `~/.local/share/outnumbered`:

* `game/` holds the installed game files
* `dos/` holds the DOS files, if you added them
* `saves/` holds your players, scores and settings

To reset the setup or add the DOS version later, run `./engine --setup`.
To uninstall, delete that folder. **That also removes your saved players**, so copy `saves/` somewhere first if you want to keep them.

## Music

Music is played through FluidSynth using the free [GeneralUser GS](https://www.schristiancollins.com/generaluser.php) SoundFont,
which is included in the `soundfont/` folder (its license is in `soundfont/LICENSE.txt`). If you prefer another SoundFont, start with
`./engine --soundfont /path/to/file.sf2`.

## Good to know

* This is an unofficial fan project. It is not made by, or connected to, the original publisher or developers.
* No game files (art, sound, music, text) are included or distributed here. You supply your own copy.
* Found a bug, or something that doesn't behave like the original? Please open an issue.

## For developers

* `make` builds the `engine` program. `make dist` puts it next to the SoundFont and this README in `dist/`.
* The source is in `src/` (plain C). `tests/mkdata.py` builds a small stand-in data set so the engine can be exercised without the real game.
* `third_party/font8x8` is a public-domain bitmap font used on the setup screen.
