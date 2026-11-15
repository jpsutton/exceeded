# OutNumbered! reimplementation — clean-room specification

This folder is the **only** input for the clean-room implementer. It describes the 1995 Windows CD
release of *Super Solvers OutNumbered!* (The Learning Company): its data formats, behaviour,
constants, layouts and algorithms. It contains no code from the original program.

## 1. The two-team process

| | clean side (you, the implementer) | dirty side (the reverse engineer) |
|---|---|---|
| has | this folder: spec + synthetic fixtures | the original game files, their disassembly, reference tools |
| never sees | the original game files, disassembly, extracted assets, the dirty side's tools | (does not write engine code) |
| produces | the engine source code and builds | spec errata, conformance reports, answers to questions |

Rules for the clean side:

1. **Do not obtain or open the original game files** (the ISO, `SSOWINCD.EXE`, `*.DAT`, `SSO.FON`,
   MIDI files) or anything derived from them. Develop against the spec and `fixtures/` only. The
   engine loads the original files at run time from a directory given on the command line; the
   dirty side does that.
2. Do not look for disassemblies, decompilations or other reverse-engineering material for this game.
   Generic public format documentation (NE executable, RIFF/WAVE, Standard MIDI File, Windows
   DIALOG/MENU resources) is fine.
3. If the spec is unclear, wrong or missing something, **ask the dirty side** (§4); do not guess
   silently. Note any assumption you make in code comments and in your hand-off notes.

What comes back from the dirty side is behavioural only: observed vs. expected values or images,
with spec section references, plus updated spec files with a changelog entry. It never includes
original code, addresses or disassembly.

## 2. Contents

| file | content |
|---|---|
| `00-overview.md` | scope, architecture, conventions, fidelity policy, glossary. **Start here.** |
| `01-data-files.md` | containers (NE resource DLLs), every resource type, TEXT/WAVE/MIDI, dialogs/menus |
| `02-graphics-engine.md` | image/animation/palette/font formats, the 512×384 renderer, resource catalogue |
| `03-shell-ui-persistence-audio.md` | startup, program flow, sign-in, menus, dialogs, hints, save file, audio, input |
| `04-action-game.md` | the real-time game: world, player, enemies, energy, clock, main loop, difficulty |
| `05-rules.md` | RNGs, drill questions, Drill for Skill, deduction puzzle, scoring, ranks |
| `06-math-challenges.md` | MTHT word-problem language, selection, answer flow, calculator, object displays |
| `07-screens.md` | HUD, drill and decoder layouts; intro, ending, bonus and score sequences |
| `10-appendix-tables.md` | fixed tables |
| `11-open-questions.md` | known defects of the original (with recommended decisions) and open questions |
| `12-dos-audio.md` | **optional, non-original**: live DOS (1990) music and effects from the user's DOS files (Tandy/PCjr or PC speaker), the Audio Options screen and `outnumbered.ini` |
| `13-input-containers.md` | **non-original**: `--data` as a CD image (`.iso`, raw `.bin`, `.cue`) and `--dos-game` as a `.zip`; minimal ISO 9660 and ZIP readers, game-folder search, where writes go, `list-files` |
| `14-first-run-setup.md` | **non-original**: per-user base directory, start-up resolution without `--data`, the drag-and-drop setup screen (embedded font, no game assets), install/verify/manifest, repair and uninstall, `install` / `resolve` conformance |
| `15-audio-levels.md` | **non-original**: music / effects / speech balance per audio style, "Even out volume" loudness normalisation (BS.1770), volume sliders in Audio Options, `audio-levels` conformance |
| `fixtures/` | synthetic test files with expected outputs (see `fixtures/README.md`) |

## 3. Build and run requirements

The dirty side builds and runs your engine on **Linux x86_64**. Available there: gcc/g++ 16, GNU
make, Go 1.27, pkg-config, SDL2 2.32 and SDL3 3.4 development packages, FluidSynth 2.6, libpng,
zlib. Not available: cmake, Rust/cargo, network access during the build, and no General MIDI
SoundFont is installed (make the SoundFont path configurable; music may be silent without one).

**Bundled SoundFont (v1.25).** The engine's distribution bundles **GeneralUser GS v2.0.3** by S. Christian
Collins (free; its licence allows use in software projects and repackaging; ship its LICENSE.txt
alongside). It is delivered to you separately as `soundfont/GeneralUser-GS.sf2` + `soundfont/LICENSE.txt`;
install both into the engine's distribution folder unchanged. SoundFont lookup order for Windows-style
music: `--soundfont FILE` → `SoundFont=` in `outnumbered.ini` [Audio] → `$SSO_SOUNDFONT` →
`<executable dir>/soundfont/GeneralUser-GS.sf2` → any `*.sf2` in `<executable dir>/soundfont/` →
system paths. The Audio Options dialog's status line may show which SoundFont is in use (optional).
Music levels are defined relative to a FluidSynth render at `synth.gain` 0.2 mixed to mono as (L+R)/2;
the engine applies the music bus gain of chapter 15 on top (v1.27).

* Choose your own language/libraries from what is available. Vendored single-file libraries are fine.
* `make` (or one documented command) must build from a clean checkout.
* `engine --data <dir>` runs the game, where `<dir>` contains the CD's `SSOWINCD` files
  (`SSO1.DAT`, `SSO2.DAT`, `SSO3.DAT`, `SND.DAT`, `SPEECH.DAT`, `SSOWINCD.EXE`, `SSO.FON`, `MIDI/`).
  Match file names case-insensitively. Store the player database in a writable directory given by
  `--save-dir` (default: `<base>/saves`, chapter 14 §1).
* `engine` **without `--data`** uses the per-user installation in `<base>` (`--home DIR`,
  `$OUTNUMBERED_HOME`, or the platform default) and shows the first-run setup screen when there is
  none or it is damaged; `--setup` forces that screen. See chapter 14.
  `--data` may also be the CD image itself (`.iso`, raw `.bin`, optional `.cue`), and `--dos-game` a
  `.zip`. Nothing is ever written to the data source. See chapter 13.

### 3.1 Conformance interface (required)

The dirty side compares your engine with its reference by running these commands. They must run
headless (no window, no audio device) and write their result to `--out`. **Options may appear in any
position** (before or after `--conform CMD`) with the same effect (v1.29).

```
engine --data DIR --conform list-resources             --out res.json
engine --data DIR --conform render-cel ASEQ_ID CEL     --out cel.png   # RGBA; transparent = alpha 0
engine --data DIR --conform aseq-trace ASEQ_ID SEQ N   --out t.json    # format as fixtures/graphics_expected.json
engine --data DIR --conform text NFNT_ID "string"      --out t.png     # black on white, pen at (1, ascent); size below
engine --data DIR --conform mtht MTHT_ID SEED QNUM     --out m.json    # {answer, question, hint, buffer_hex}
engine --data DIR --conform drill TIER SRAND N         --out d.json    # format as fixtures/rules.json
engine --data DIR --conform puzzle TIER SRAND GAMES    --out p.json    # format as fixtures/rules.json
engine          --conform save-dump FILE               --out s.json    # format as fixtures/save_expected.json
engine --data DIR --conform play SCRIPT                --out DIR2      # scripted play, see below
engine --conform dos-unpack|dos-check …  /  engine --dos-game DIR --conform dos-trace|dos-script|dos-render …   # optional DOS audio, 12 §7
engine --conform list-files PATH [--role data|dos]      --out files.json  # logical files of a folder/image/zip, 13 §8
engine --conform install --home DIR [SRC ...]           --out result.json # headless first-run install, 14 §10.1
engine --conform resolve [--home DIR] [options]         --out r.json      # start-up resolution, 14 §10.2
engine [--home DIR] --conform play SCRIPT               --out DIR2        # script starting with `setup`: setup screen, 14 §10.3
engine --data DIR [--dos-game DIR] [--soundfont F] --conform audio-levels [--even-out 0|1] [--volumes M,E,S | --music-volume N --effects-volume N --speech-volume N] --out a.json  # per-asset loudness and gains, 15 §7
```

`text` image size: height = fRectHeight; width = max(pen_end_x, rightmost set pixel + 1) + 1. Only
the set pixels are compared.

`aseq-trace`: while an AO is paused or holding its last frame, repeat the displayed cels in each
entry, as you do now. A stopped AO (END) reports `{"tick": t, "state": "stopped"}`. Cels are listed after clipping: one entirely outside the clip
rectangle is omitted.

`play` runs the real game in **deterministic mode**:
* Virtual time: the 100 Hz tick clock advances only as the script says; there is no wall-clock
  dependence and no frame skipping.
* Fixed seeds: wherever the original seeds from the time of day, use values from the script.
* Scripted input; audio is muted, but the sound and music events are still logged.

Script format, one command per line, `#` starts a comment:
```
seed time <n>              # value used wherever the original seeds RNG-A from time()/timeGetTime()
signin <NAME> [score]      # create/select a player with that lifetime score and start a game
drill                      # or: enter Drill for Skill
wait <cs>                  # advance virtual time by <cs> centiseconds
key down <KEY> / key up <KEY> / press <KEY>      # KEY: LEFT RIGHT UP DOWN HOME SPACE ENTER ESC TAB BACKSPACE, or a character
click <x> <y>              # canvas coordinates
frame <name>               # write <name>.png (the 512×384 canvas)
state <name>               # write <name>.json: the 99-byte game state decoded (03 §4.4) plus player
                           # column/y/animation state, scroll, area, Telly/LiveWire/trick state,
                           # energy credit, clock, and the dialog id if a dialog is open
                           # plus: "screen" (hall/room/drill/challenge/decoder/ending/...), the player
                           # record's object seeds, and per screen: drill {display, answer, asked,
                           # correct, required}; challenge {object, mtht_id, seed, question_number,
                           # wrong_tries, entry}; decoder {focus, highlighted_room}
```
Script semantics:
* `press K`: key down, held until the game has polled input once (one action frame), then released.
  For keys the original reads from its event queue (Enter, Esc, Tab, Backspace, characters, and the
  VK codes listed in 03 §6.1) one key event is also queued.
* `seed time <n>`: n (low 16 bits) is used for **every** time-based seed: the program-start
  `srand(time())` and each `srand(timeGetTime()/10)` re-seed (new game, math-challenge setup).
* Virtual time starts at 0 when the program starts.
* `intro`: start the program normally at the title screen, with no automatic sign-in. Script keys
  then drive the title, intro and narration, and the sign-in screen. Use typed characters and ENTER
  to sign in through the UI.
* `signin NAME [score]`: if NAME already exists in the player database, select that player **as is**,
  including its saved game, which must then resume (03 §2.4–2.5). If a score is given, only the lifetime
  score is overwritten. A new player is created only when NAME does not exist. The database lives in
  `--save-dir` if given, otherwise in `DIR2/save`; an existing file there is used, not replaced.

Write `DIR2/events.log` with one line per sound, speech, music or dialog event:
`<tick> SFX|SPEECH|MUSIC|DIALOG <id> [details]`.

Modal dialogs in `play` mode are answered by `press ENTER` / `press ESC` or by `click`.

## 4. Communication

* The dirty side is the Patchbay session that sent you this folder. Send questions and hand-offs to
  it with `send_agent_message`.
* **Hand-off of builds:** send your source tree (no build output, no game files) with
  `wormhole send <dir>` and message the dirty side the wormhole code, the commit or version label,
  and what changed. A short `HANDOFF.md` in the tree (status, known gaps, assumptions) helps.
* **Reports back:** numbered findings. Each gives the spec section, what your engine did, what the
  original does, and how to reproduce it (a conformance command or play script). Spec updates come
  as replacement files plus an entry in `CHANGELOG.md`.

## 5. Suggested order of work

1. NE resource reader and type names (01). Fixture: `FIXTURE.DAT`.
2. RNGs (05 §1, 06 §3). Fixture: `rng.json`.
3. Palette, cel decoder, NFNT text (02). Fixtures: `graphics_expected.json`.
4. Animation interpreter and compositor (02 §2.4, §5); deterministic tick clock.
5. MTHT interpreter (06). Fixture: `mtht_expected.json`.
6. Save file (03 §4). Fixture: `save_SSOWINCD.DAT`.
7. Rules: drill deck, questions, puzzle (05). Fixture: `rules.json`.
8. Audio: WAVE mixing, speech priority, MIDI (03 §5).
9. Shell (03), action game (04), drills and decoder (05), math challenges (06), screens and
   sequences (07), with the `play` mode working throughout.

Hand off early (after step 3 or 4) so the dirty side can check the real assets as soon as possible.
