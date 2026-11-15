# OutNumbered! engine reimplementation — specification

Target: a modern, portable engine that plays **Super Solvers OutNumbered!** (The Learning Company,
Windows CD release 1995; itself an update of the 1990 MS-DOS game) using the **original data files**
from the CD. This document set is a clean-room behavioural specification: it describes data
formats, rules, constants, layouts and algorithms. It deliberately contains no original code.

## Chapters

| file | content |
|---|---|
| `00-overview.md` | this file: scope, architecture, conventions, fidelity policy, glossary |
| `01-data-files.md` | the containers (NE resource DLLs), how to read every resource type, file map, MIDI/WAVE, TEXT, Windows dialog/menu resources |
| `02-graphics-engine.md` | ASEQ/ESEQ/RRGB/NFNT/FON/cursor formats, the 512×384 renderer, animated objects and screen elements, resource catalogue |
| `03-shell-ui-persistence-audio.md` | startup, program flow, sign-in, menus, dialogs, New Player Hints, save file, audio manager and catalogues, input |
| `04-action-game.md` | the real-time game: world layout, player, Telly, LiveWire, zapper/energy, clock, main loop, difficulty table |
| `05-rules.md` | RNGs, drill questions, Drill for Skill, the deduction puzzle (decoder), scoring, ranks, winning/losing |
| `06-math-challenges.md` | MTHT word-problem template language, problem selection, answer/try/hint flow, calculator, the 10 object displays |
| `07-screens.md` | screen layouts (HUD, drill, decoder) and scripted sequences (intro, hideout, bonus, ending) |
| `10-appendix-tables.md` | raw world/animation/object tables (as found in the data) and other fixed tables |
| `11-open-questions.md` | consolidated list of uncertainties and fidelity decisions to make |
| `fixtures/` | synthetic test files with expected outputs (`fixtures/README.md`) |
| `README.md` | the clean-room process, build requirements and the required conformance interface |

Read 00 → 01 → 02 first; 03–07 can be implemented in any order after the engine core exists.

## 1. The game in one page

The Master of Mischief (Morty Maxwell) is hiding in one of five rooms of SGTV, the Shady Glen TV
station (NEWS ROOM, GAME SHOW ROOM, CARTOON ROOM, EQUIPMENT ROOM, SOUND ROOM). The player, a Super
Solver, has from 9:00 PM until midnight (game clock; ~10 real minutes of unpaused play) to find him.

* The **hall** is two side-scrolling floors (5 screens each) joined by elevators. **Telly** the robot
  patrols it, throws "tricks" (cost 15 game minutes) and crashes into the player (cost 1 zapper
  energy). The player walks, jumps/flips over hazards, and zaps with a **zapper** (1 energy per zap).
  Zapping a **lit** Telly starts **drill questions** (arithmetic facts, clock keeps running); answering
  all but one correctly captures a **piece of the secret code** (4 pieces in total).
* The five **rooms** contain objects hiding **math challenges** (word problems about a map, bar
  graph, prize board, spinner, hit chart, volume sliders, monitor list, equipment list, line graph,
  video tapes). The clock pauses during a challenge. Solving one (up to 3 tries, hints after wrong
  answers) earns points and energy credit; solved or failed, it reveals **picture clues** for that
  room. **LiveWire**, a living electrical cord, roams rooms and costs energy on contact.
* An **energy supply machine** in the hall converts solved-challenge credit into zapper energy.
* In the **decoder** (clock paused) the player compares each room's 4 clue pictures with the code
  pieces using the **Match Rule** for their rank, and **Decides** on a room. Right → win, bonus
  points, lifetime score and rank increase. Wrong, or midnight → loss, no points.
* **Ranks** (Trainee → Champion) from the lifetime score set the difficulty "tier" (0..3): drill
  length, math level, Telly behaviour, clue count, match rule, bonus rates.
* **Drill for Skill** is a separate practice mode of endless customisable drill sets.
* Players sign in by name; progress, settings and an unfinished game are saved per player.

## 2. Architecture to implement

```
 platform layer: window, 512x384 indexed framebuffer → scaled output, audio mixer (4 SFX + 1 speech),
                 MIDI synth (SMF format 0) or pre-rendered music, keyboard/mouse, 100 Hz tick clock
 resource layer: NE resource reader for SSO1/2/3.DAT, SND.DAT, SPEECH.DAT, SSOWINCD.EXE, SSO.FON
 engine core:    ASEQ/ESEQ animation interpreter, screen-element compositor, dirty-rect present,
                 NFNT text renderer, palette (RRGB 128), dialog presenter, hot regions, input queue
 game layer:     shell/menus/sign-in/save · action game · drills · decoder · MTHT interpreter + displays
                 · scripted sequences (intro, hideout, bonus)
```

The original is a single-threaded loop: each frame polls input, advances game logic, advances
animations, composites and presents, then waits until 7 centiseconds after the previous frame
(≈ 13–14 fps for the action game; animations independently run at the fps stored in their ASEQ).
Modal screens (math challenge, decoder, drills, dialogs) run their own nested loops; leaving a game
is a non-local jump back to the sign-in loop. A reimplementation may restructure this as a state
machine as long as observable timing is kept.

## 3. Conventions used in all chapters

* **Coordinates** are pixels on the **512 × 384 canvas** (origin top-left) unless stated otherwise.
  The original centres this canvas 1:1 in a desktop-sized window; a port should scale it (integer or
  aspect-correct) and map mouse coordinates back.
* Horizontal positions in the action game use **columns** (~12.8 px) and vertical "design units"
  (×1.92 → px); see chapter 04 §1. These come from the 320×200 DOS original.
* **Time**: the tick is 1/100 s ("cs" = centisecond). Game clock values are `hours*100+minutes`.
* Integers in files are little-endian unless stated. "u16/s16/u32" as usual.
* Resource references are written `FILE TYPE id`, e.g. `SSO1 ASEQ 200`, `SPEECH WAVE 1838`,
  `EXE DIALOG 9136`.
* "Tier" `T = rank div 2` (0..3). Rank names/thresholds: chapter 05 §5.1.
* Confidence tags where present: **H** read directly from the original's behaviour/data, **M** strong
  inference, **L** guess.

## 4. Fidelity policy

The specification records the original behaviour exactly, including several defects. Each is
flagged where it occurs and collected in `11-open-questions.md`. Recommended defaults:

| category | recommendation |
|---|---|
| Wrong dialog ids (CD-missing 128→129, list-full 22012→23012, All-Star rank dialog 149→1, Expert first-time 9133 unreachable) | fix |
| Gameplay quirks (last energy unit never hits; walking into a screen edge trips the player; hall hot-spot zones very wide) | keep (they shape the feel); optional switch |
| Puzzle-generator defects (rare ambiguous All-Star puzzles, Expert "different pattern" sometimes identical, state carried across games) | fix by validating/re-rolling, keep original behaviour behind a switch |
| Drill-for-Skill timer running ~35 % fast | fix |
| Windows-specific: WinG/16-colour modes, WaveMix, MIDI mapper base/extended detection, single-instance, NumLock forcing, "refuse shutdown" | replace with modern equivalents (see each chapter) |
| Debug/beta menus and INI CheatKeys (dead in retail) | omit |

## 5. Platform replacements

* **Display**: one 8-bit indexed 512×384 framebuffer + 256-entry palette. Use all 256 entries of
  SSO1 RRGB 128. (The original loaded only entries 10..245 and took 0..9/246..255 from the Windows
  system palette; RRGB 128 already holds those 20 static colours, except that the file stores the
  dark primaries 1..6 as 191 where a Windows 3.1 system palette has 128. Either is acceptable; these
  entries are used only by a few UI cels.)
  Present by palette-expanding to RGBA and scaling. Dirty-tile tracking is optional.
* **Windows dialogs/menus**: the game relies on ~90 standard Windows dialog resources for hints,
  confirmations, help pages, rank messages and customisation, and on a menu bar. Re-create them as
  in-canvas or native modal panels, using the text and control layout from the EXE's DIALOG/MENU
  resources (chapter 01 §5), the TLC Keycaps font for key-icon glyphs, and the behaviour in chapter 03.
  A menu bar can become an in-game menu (Esc/F10) as long as every command remains reachable.
* **Audio**: WAVE resources are plain RIFF PCM (8-bit mono, 11025 or 22050 Hz). Mix up to 4 SFX
  channels plus speech (speech pre-empts all SFX). MIDI files are SMF format 0, 480 PPQ; use a GM
  synth (e.g. a SoundFont) with the **E** (extended) set by default; the **B** (base-level) set is
  for limited synths.
* **Input**: keyboard (arrows, Home, Space, Enter, Esc, Tab, Backspace, digits, letters) and mouse;
  see chapter 03 §6. Key repeat must not be used for action-game polling (poll held state).
* **Persistence**: keep the original `SSOWINCD.DAT` layout (chapter 03 §4) so existing save files
  work; store it in a per-user data directory rather than the program directory.

## 6. Glossary

| term | meaning |
|---|---|
| AO | animated object: an instance playing an ASEQ script |
| SE | screen element: one cel placed on screen with a z order |
| cel | one RLE-compressed image inside an ASEQ |
| ASEQ / ESEQ | animation resource (cels + scripts) / extra scripts for an ASEQ |
| RRGB | palette resource |
| NFNT | Mac-style bitmap font resource |
| MTHT | math-challenge (word problem) template resource |
| BINA 5000 | binary world/animation/object tables |
| INTS | list of sound ids |
| tier | rank div 2, 0..3 |
| code piece | one of 4 secret-code pictures won from drill sets |
| clue | one of 4 pictures per room revealed by math challenges |
| Match Rule | how a room's clues must relate to the code for it to be the hideout |
| pepper | internal name for zapper energy |
| MTM | internal name for a math challenge |
| DfS | Drill for Skill mode |
