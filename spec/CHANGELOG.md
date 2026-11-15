# Spec changelog

## v1 — 2026-11-14
Initial release: chapters 00–07, 10, 11; synthetic fixtures; two-team process and conformance
interface (README).

## v1.1 — 2026-11-14
* 02 §2.3: USER-opcode (≤ −500) hook return values clarified. The tick never ends early; bit 0 rewinds
  to the tick's start index.
* 02 §5.2: direct drawing into the work buffer persists. Do not rebuild the frame from back + SEs.
* README §3.1: `text` image width rule; `aseq-trace` representation of paused and stopped AOs.
* fixtures: `graphics_expected.json` NFNT rows now follow the width rule (18 px instead of 24).

## v1.2 — 2026-11-14
* 05 §3.1/§3.4: the secret-code array is part of the game state. It is zeroed at sign-in only and
  carries over between "play again" games, so a stale All-Star code slot holds the previous game's
  picture. Candidate rows persist for the whole program run.
* fixtures/rules.json: puzzle tier 3 games 2 and 3 regenerated accordingly (the earlier fixture
  wrongly cleared the code each game). Note text clarified.
* README §3.1: `aseq-trace` lists cels after clipping; a cel entirely outside the clip rectangle is
  omitted (as your engine already does).

## v1.3 — 2026-11-14
* 04 §14 (new): action-game sound table (ids, priorities, loop flags, triggers), splat effect, Telly
  lit-belly overlay, native facing and mirroring of every sprite, erase/update/draw order. There is
  no footstep sound: the per-frame choice is background music (03 §5.2).
* 04 §6.1: after a trick hits the player, Telly is removed as well.
* 04 §9.2: frame timing detail (input polled at the start of an iteration; the first iteration
  presents immediately).
* 03 §1.2: dialog button results, Enter/Esc behaviour, dialog-unit scale.
* 10 appendix B (new): button structure of all 96 dialogs.
* README §3.1: `press` and `seed time` semantics; virtual time starts at 0.
* 03 §5.2: enter.mid plays on every room entry, not only in the Decide sequence.

## v1.4 — 2026-11-14
* 07 Part C (new): decoder open/close sequence (every open re-spins all collected pictures; no row
  highlighted initially), code-piece reveal sequence after a won drill, bonus-panel recolouring rule
  (whole panel; colours 251/254), text placement of the bonus lines, sign-in list fonts and colours.
* 07 Part A §5: initial decoder highlight corrected (none).
* README §3.1: `signin` selects existing players unchanged (saved games resume); save-dir use in
  play mode; extra `state` fields requested for screens.

## v1.5 — 2026-11-14
* 07 Part C §C.5: music auto-selection off during the ending; the bonus panel is an animated
  object with post-drawn text (fixes damage and remnants); Decide-prompt highlight rules.
* 02 §4.1: game font handles (normal 28625, large 28568, medium 28558); 06: spinner font = 28625.
* 10 appendix C: calculator key rectangles.
* README §3.1: `intro` script command.

## v1.6 — 2026-11-14
* 02 opcode −4 PAUSE: interpretation continues in the same tick; the cel entry after the PAUSE is
  drawn as the paused frame.
* 07 Part B §3.1 / Part C §C.5: the bonus panel rests at (102,35) (spans (102,35)–(408,149)), not
  (102,30). The title and value-line positions are unchanged (absolute canvas y 65 / 112).

## v1.7 — 2026-11-14
* 03 §3 (See Rank…): exact DIALOG 200 control usage: statics 100/101/102 (placeholder text "Text"),
  header lines Name/Rank/Score, CR line terminators, score column right-aligned, empty names skipped.

## v1.8 — 2026-11-14
* 07 Part D (new): Drill for Skill in full: entry and starting customisation, screen composition
  (incl. keypad ASEQ 8540), set loop, end-of-set perfect / try-again scripts with music rules,
  Continue button, counters and time display, input and keypad rectangles, menus and leaving,
  DIALOG 220 behaviour, an original bug not to reproduce.
* 07 §D.10: corrections to Part A §4, 03 §2.2/§2.3/§2.7/sound table/NumLock and 05 §2.6.

## v1.9 — 2026-11-14
* 06 §7.1 (new): the answer display of math challenges (with and without the calculator): blank
  rectangle and colour, font NFNT 28558 black, right-aligned entry, operator position and glyphs,
  what each calculator state shows, no caret, when it redraws and clears.
* 06 §7: a 5th digit and an operator in state 2 are ignored silently (no beep).
* 07 §0 text convention corrected: the pen baseline is at y + ascent − descent (not y + ascent).
  This moves every NFNT text in chapters 06/07 up by the font's descent.

## v1.10 — 2026-11-14
* 06 §6.1 (new): session sounds of a math challenge (opening stops sounds/music; key clicks 1756;
  correct 1743 waited; wrong 1760, waited on the 3rd; hints before/after).
* 06 §8.10 (new): per-display sounds and timelines. There is **no** common "object opened" sound;
  1701 belongs to the Bar Graph only.
* 03 sound table: challenge sound rows replaced; 1753/1754/1763 are Telly hall sounds.

## v1.11 — 2026-11-14
* 04 §5.5 rewritten: the complete elevator ride frame by frame (approach, door cel positions at the
  frame origin + (37,37) = (9 + 13·(slotCol − L) + 37, 105), 4-step open/close with cab cel 179,
  player cels and layering, slide directions and steps, arrival, sounds, clock frozen) plus a list of
  corrections to earlier text (§5.5, §8, §14.7, 07 Part A).
* 04 §14.7: elevator sentence replaced; door cels are kept in the background.

## v1.12 — 2026-11-14
* 06 §8.11 (new): exact Prize-board flashing (problem RNG, cumulative 6-cs schedule, invisible frame
  recolour, final black redraw), Monitor static (XOR 0xFF 2×2 blocks, count, even/odd frames, picture
  restored), Spinner pointer positions per cel and redraw order, Equipment header (no colour change;
  8 × 20 cs pause).
* 06 §8.7: the "header bands rotate" sentence corrected.

## v1.13 — 2026-11-14
* 04 §5.4 rewritten: room entry and exit frame by frame (enter.mid start and hold, auto-walk, door cels
  134–136 at (9 + 13·(doorWorld − scroll), 101) kept in the scene, fixed player cels 13/22/23/18, room
  build order with the LiveWire roll before the arrival, hint 5 after it, the hall door shown open and
  closing on exit, plain doors, no door sound), plus corrections.
* 04 §3.1: cel 134 is the closed door, not a sign.
* 07 Part A §0: stamped cels always persist; flag 0x80 means "clip to the playfield".

## v1.14 — 2026-11-14
* 04 §6.3 (new): Telly interactions frame by frame: global animation phase, materialise/dematerialise
  cels, throw release frames, trick flight, jumping over, trick hits / Telly crash / splat freeze (objects
  redrawn under the hit word), flashing, the zap (a frozen synchronous sequence; pose cel 49 kept; bolt
  positions), lit Telly → drill → hall (Telly and any trick removed), draw order. Corrections list.
* 04 §7 Zap bullet and §14.4 splat sequence now point to §6.3.

## v1.15 — 2026-11-14
* 06 §8.4 Great 8: clarified that both value columns get a header (column 1 from #0, column 2 from #1).

## v1.16 — 2026-11-14
* 06 §8 (Common + §8.0–§8.9) rewritten with exact layout rules re-derived from the original: every
  display uses the pixel text routine, x = left edge after the stated alignment rule, y = routine input
  (07 §0), absolute canvas coordinates. Notable fixes: Spinner values are CENTRED on X+16 (not right-
  aligned), the centre number is F0, a blank label skips the whole centre block; Map name clamp; Bar
  graph and Line graph label/title fonts and positions; Great 8 special rows come from #2/#3, column
  fill order and value alignment; Volume knob start and zero-slider knob; Equipment column positions,
  fill order and name splitting.
* 06 §8.10: timing note — the bar/segment/VCR waits are delay(n) units (50 → 20 cs, 150 → 63 cs, …).

## v1.17 — 2026-11-14
* 07 Part A §4 rewritten from "8604 cel placement" to the failure bullet: drawing model, button press
  order and positions (cel 0 at (328,210), cel 1 at (327,204)), every erase/fill rectangle in pixels,
  d.u. fill conversion, question line widths, cursor, praise in FS, wrong-answer sequence incl. final
  clear, code-piece reveal erase. The (145,80) 220×80 doubt is resolved.
* 07 Part D §D.3 adjustments (49 cs wait only before the first problem of a set) and a corrections list.

## v1.18 — 2026-11-14
* 06 §8.8: exact line rasterisation (start from the endpoint with the smaller major-axis coordinate,
  d = 2·minor − major, step when d ≥ 0, vertical run ±t per point) and the y-axis title step of 17 px
  on both graphs (the 18 is only a factor in the hard graph's start-y formula). This overrides the word
  "Bresenham" above.

## v1.19 — 2026-11-14
* 07 Part B §3.3 score summary: the painted screen is part of the scene (frame + background); erases in the
  beam / marker steps restore what was under the cel.

## v1.20 — 2026-11-14
* 04 §7 energy supply machine rewritten frame by frame: approach, state 15 (cels 9–12, 203; dy −2),
  saved rectangle, energy added and HUD shown before the beam, 9 beam frames (196/197) or refusal
  (199,200,201,199) at (machine x, 91), 12 cs each, sound 1707/1708 looped from frame 0 to the end,
  state 29 (dy +2), facing restored; credit +2 per solved challenge. Corrections list (state table
  rows 15/29, §5.3 timing, cel 203 in the hall set).

## v1.21 — 2026-11-14
* 12 (new, optional, non-original feature): live playback of the 1990 MS-DOS music and sound effects
  from the user's DOS files (`--dos-audio`, `--music-style`, `--sfx-style`, `--dos-game`). Covers
  MZ/EXEPACK unpacking, the driver tables in the load image, MOCKET.DAT and the two music blocks,
  validation, the tick-exact driver (events, commands D0–D6, ties, gate, envelopes, Tandy/speaker
  pitch, priorities, forced restarts, looping, the state that carries over between songs), SN76489
  and PC-speaker synthesis and levels, and the mapping of Windows music slots and SND ids to DOS
  songs, by game event where DOS differs (award zap and score screen = song 16, wrong-decision
  start = 17, the ending scripts' forced restarts). It also specifies the Audio Options enhancement:
  the OPTIONS button built from the CANCEL art (CANCEL's function moves to Esc), the dialog, and
  `outnumbered.ini`.
* README: chapter 12 in the contents and the conformance list. New conformance commands dos-unpack,
  dos-check, dos-trace, dos-script, dos-render (12 §7).
* fixtures/dos/ (new): synthetic DOS game folders, EXEPACK files, expected traces, scripts and
  reference renders.

## v1.22 — 2026-11-14
* 12 §5.1 / §5.6 (pauses and driver time):
  * a DOS driver pauses only where Windows pauses MIDI (dialog open, inactive window, open menu);
  * the game-clock pause (decoder, math challenges, the Decide / ending / bonus / score sequences,
    the clock pause around hints) never pauses a DOS driver;
  * a separate effect driver never pauses;
  * driver time = game time minus that driver's paused time; scheduled requests count driver ticks.
* 12 §7.6: events.log `DOS` lines are always stamped in game time (scheduled requests too) and
  written in execution order.
* 12 §5.2: the wait for a waited jingle also ends after the song's length in game time, even if the
  driver is not advancing (no hang).
* 12 §5.3 / §5.5: 1760 in the wrong-decision (lose) ending = `force(17)`. A plain play was ignored
  under the lightning song 11.
* 12 §6.4: on OK in Audio Options, restart only a music slot that is currently playing or paused,
  not one remembered after a stop.
* 12 §7.5: the RMS tolerance leaves out 10 ms windows at or next to a sound-on/off change. Chord
  windows may match either voice's frequency.

## v1.23 — 2026-11-14
* 12 §5.3, math-display mapping fixed:
  * 1736 is the Volume display. It plays DOS song 27 (the DOS slider sound), once per moving slider.
  * 1751 is the Spinner pointer tick. The first 1751 of a spin plays song 30 (the DOS spinner tune);
    later 1751s of the same spin, and Windows stops of 1751, do nothing.
  * Before this fix, 1736 played song 30 and 1751 was silent.
* 12 §5.3 / §5.5, award-zap hold: a 1738 copy (song 10) that falls due while the award zap
  (song 16) is current waits, together with the copies after it. They keep their 60-tick spacing,
  so the big zap is no longer cut.
* 12 §7.6: stamps never decrease across all log lines. The DOS drivers are run up to the current
  game time before any line is written.
* Unchanged, on purpose: a higher-priority effect may still replace the door jingle and end the
  room-entry wait early (DOS's single voice).

## v1.23a — 2026-11-14
* 12 §5.5 / §5.3, award animation (lifetime < 500,000) in DOS effect style. This replaces the v1.23
  award-zap hold, which is removed.
  * The DOS award timeline plays from the start of the player's zap (ESEQ 1002 seq 3), counted on
    the DOS tick clock: forced song 16 at +90, song 10 at +300 / +360 / +420, song 50 at +510 / +540
    / +570.
  * The Windows cues 1764, 1738 and 1739 make no DOS request during the award.
  * The victory step waits until both the award clips and the DOS timeline (≈ 2.7 s) are done.
* Champion branch: unchanged per-cue mapping (1738 triple, 1739 / 1768 / 1769 forced). Its DOS
  script timing is not known (open point).
* 12 §8 A5 updated.

## v1.24 — 2026-11-14
* 13 (new, non-original): the engine uses the user's files as distributed. `--data` may be an ISO
  9660 CD image (`.iso`), a raw 2352-byte MODE1/MODE2 image (`.bin`/`.img`, detected by the sync
  pattern) or, optionally, a `.cue` sheet. `--dos-game` / `DosGameDir` may be a `.zip`. The chapter
  specifies the minimal ISO 9660 reader (volume descriptors, PVD, directory records, names, extents,
  multi-extent files, optional Joliet), the game-folder search (top, then first-level folders),
  the minimal ZIP reader (EOCD search, central directory, local header, stored/deflate, CRC-32), the
  DOS-folder search (zip: shallowest folder with SSO.EXE + MOCKET.DAT; folder: depth ≤ 2), error
  codes and messages, and the Audio Options field accepting a `.zip`.
* 13 §6: data sources are never written. When the save directory has no `SSOWINCD.DAT`, the data
  source's valid template is the initial content, written to the save directory at the first write.
  `SSOWINCD.INI` is read from the save directory, else from the data source. Writes go to the save
  directory. `CDDrive` / `MIDIPath` are ignored.
* New conformance command `list-files PATH [--role data|dos]` (13 §8.1). Every `--data` command
  accepts an image; `dos-check` and the `--dos-game` commands accept a zip.
* README: chapter 13 in the contents, the CLI note and the conformance list. 12 §1.2: cross-reference
  (13 §5 replaces the `SSOUTN` subfolder rule). fixtures/README: `containers/`.
* fixtures/containers/ (new): synthetic images, a folder twin, zips, negative cases, `expected.json`.

## v1.25 — 2026-11-14
* README: the engine bundles the free GeneralUser GS SoundFont (delivered separately) and looks it up
  next to the executable; SoundFont lookup order defined; `SoundFont=` key in outnumbered.ini [Audio].

## v1.26 — 2026-11-14
* 14 (new, non-original): first-run setup and per-user installation.
  * Base directory: `--home` > `$OUTNUMBERED_HOME` > platform default (`$XDG_DATA_HOME/outnumbered`
    or `~/.local/share/outnumbered`; `<Local AppData>\OutNumbered`; `~/Library/Application
    Support/OutNumbered`). Layout `game/`, `dos/`, `saves/`, `install.json`. Paths are UTF-8 (wide
    API on Windows); folders 0700, files 0600; installed files are created new (never read-only).
  * Start-up without `--data`: a valid installation (manifest + sizes; CRCs with `--verify-install`)
    runs the game, otherwise the setup screen opens. `--setup` forces it. Explicit `--data` and
    `--dos-game` are unchanged.
  * DOS precedence: `--dos-game` > `DosGameDir` > installed `dos/`.
  * The default save directory is `<base>/saves` in every interactive run.
  * Setup screen: works without game assets (font8x8, public domain). Exact layout, palette, rows,
    messages, focus, keyboard and mouse. Drag and drop (multi-path drops, folders) plus a text
    field.
  * Classification uses chapter 13 for the Windows role (fixed install set) and 13 §5 + 12 §2.6 for
    the DOS role.
  * Install: staged copy with CRC verification, commit by rename, manifest written last, cancel,
    error codes and Retry. A load failure from the installed base returns to the setup screen.
  * Audio Options gets an Install button (12 §6.3 amended, 14 §9).
  * Repair through `--setup`; uninstall by deleting the base directory.
* New conformance commands `install` and `resolve`; play-script commands `setup`, `drop`, `type`, and
  the `setup` object in `state` (14 §10).
* README: chapter 14, CLI note, default save directory, conformance list.
* 12 §6.3 and 13 §6: cross-references. fixtures/README: `setup/`.
* fixtures/setup/ (new): prepared homes, `dos_invalid.zip`, the play script `setup_basic.txt`,
  `expected.json`. Built by the dirty side's `make_setup_fixtures.py` from the synthetic container
  fixtures.

## v1.27 — 2026-11-14
* 15 (new, non-original): audio levels. Playtest report: music and effects at very different volumes.
  * Measurements (BS.1770 integrated loudness): Windows MIDI music played ≈ 25 dB below effects and
    speech; Tandy music ≈ 30 dB above Windows music; Windows effects spread 27 dB, Tandy effects
    18 dB. The original sets no levels at all (15 §1.1).
  * Bus targets: music −20, effects −16, speech −15 LUFS. Fixed style gains per (style, bus)
    (15 §3); they replace the DOS gains ×2.48 / ×0.53 (12 §4.3).
  * "Even out volume" (default on): per-asset loudness measured by the engine on the user's data
    (WAVE at load, DOS songs offline, MIDI by an offline FluidSynth render on a worker thread), gain
    limited to ±12 dB (music ±6 dB) around the style gain, WAVE peak cap +3 dBFS (15 §4).
  * Master soft clip (knee 0.8) replaces the hard clip (15 §5).
  * Audio Options: Music / Effects / Speech sliders (0–100, 40·log10 law, live, Cancel restores) and
    an "Even out volume" check box; wider template; Test required (15 §6). INI keys `MusicVolume`,
    `EffectsVolume`, `SpeechVolume`, `EvenOutVolume`.
  * Conformance: `--conform audio-levels`; play-script `volume`; `state.audio.volumes` (15 §7).
* 12 §4.3 / §6.3 / §6.5, 03 §5.1: pointers to chapter 15. README: chapter 15, conformance list,
  music reference level note. fixtures/README: `audio_levels/`.
* fixtures/audio_levels/ (new): 10 synthetic WAVEs + `expected.json`, built by the dirty side's
  `tools/audio_levels.py --fixtures`.

## v1.28 — 2026-11-14
* 03 §1.2: TLC Keycaps dialogs use 1 DLU = 2.0 px horizontally (Windows 95 base-unit rule: average width
  of A–Z a–z = 8 px), not 1.75. Fixes wrapped-and-clipped hint text. The non-original Audio Options
  dialog (12 §6.3 / 15) keeps 1.75 px so its layout stays within the 512-px canvas.

## v1.29 — 2026-11-14
Follow-up to the engine 1.12.0 check of chapter 15.
* 15 §4.2: a MIDI or DOS song requested before its measurement is done is measured on demand,
  synchronously, before it starts. The wait is capped at 1.5 s; when the cap is hit, that play uses the
  style gain and the measured gain applies from the next start. (Before this, the first play of the title
  song always used the style gain: intro.mid was 3.9 dB above target.)
* 15 §7.1 tolerances: DOS `lufs` / Even-out-on `gain_db` ±1.5 dB against a 12 §7.5-conformant render
  (was ±0.3, which assumed one renderer); DOS `peak_dbfs` is no longer compared. The engine's DOS
  values must match §4.1 applied to its own `dos-render` output.
* 12 §6.4: OK restarts the music only when a style, the DOS folder or DosSharedVoice changed. A
  volume-only (or Even-out-only) OK keeps the music playing (confirms engine 1.12.0's choice).
* README §3.1 and 15 §7.1: options are accepted in any position relative to `--conform CMD`.
  `audio-levels` honours `--music-volume`, `--effects-volume` and `--speech-volume` as well as
  `--volumes` (the later option wins for a bus).

## v1.30 — 2026-11-14
* 03 §3.0 (new): the Windows menu bar is always visible above the game area (logical frame 512×404),
  Windows 95 look, mouse and keyboard handling, pausing, and play-script `frame NAME full` / `menuclick`.

## v1.31 — 2026-11-14
* 03 §3.0a (new): menu behaviour on every screen (what opening a menu pauses, items greyed during the
  ending sequences, Return to Sign In / Exit mid-challenge and mid-animation, customisation and calculator
  toggles mid-challenge, recommendations).
* 03 §4.3: robustness rule — never save before the game's state is initialised; discard an invalid saved
  state at sign-in. (A playtester's new player got an all-zero saved game and an empty, unwinnable game.)

## v1.32 — 2026-11-15
Playtest report: in DOS effect style, the elevator doors closing after the player steps out made the
same sound as the ride between floors.
* 12 §5.3, 1714: DOS plays **one** sound per elevator ride, the ride song, requested once when the
  departure doors have finished closing: song **22** going up (departure area 0 or 4), song **21**
  going down (area 5 or 9). The arrival door close (the second 1714 of a ride) makes **no sound**.
  Door opens (1713) stay silent. Song 21 is no longer unused.
* 12 §5.6: the arrival "stop all sounds" of the ride does not stop the ride song.
* 12 §5.6a (new): the elevator ride's DOS sound, phase by phase, with timing.
* 12 §1.1: the Music / Effects / Voice reference now points to §5.7 Options.
* 15 §7.1: `audio-levels` lists DOS song 21 as well (now played).

## v1.33 — 2026-11-15
First findings from running the original DOS game in DOSBox (Tandy mode, audio captured).
* 12 §4.1: the Tandy chip's voices are **unipolar**. Each tone or noise voice outputs 0 or
  +2·amp, never negative; the 4 voices are summed and AC-coupled (one-pole high-pass, 20 Hz
  recommended). Before, the spec implied a ±amp square. The difference is audible wherever a voice
  is switched on and off quickly. Most strongly, the elevator ride songs 21/22 sound like a tick-rate
  buzz rather than a clean sweep, as the real machine does. Short notes gain a click at each start.
  Sustained tones keep the same level.
* fixtures/dos/render_tandy_song0.wav regenerated with the unipolar model. Other DOS fixtures are
  unchanged. The §7.5 tolerances still apply.
* Checked against the recordings, with no spec change: driver tempo is exact; the Tandy and speaker
  title songs match the reference model; the v1.32 elevator ride (one ride song after the departure
  close, none at the arrival close) matches the original.

## v1.34 — 2026-11-15
Remaining gaps between DOS mode and the original DOS game (from the DOS sound-request survey).
* 12 §1.2: `--dos-audio` also accepts `windows`. Every style option takes `--name=value` or
  `--name value`. An unknown or missing value is an error (exit status 1).
* 12 §5.3:
  * new row: zap hits a **lit** Telly → `force(16)` after the Windows stop-all (no Windows SFX id).
    The drill static's 160-tick stop counts from its first request.
  * 1763 (unlit) → `force(42)` (was `play`).
* 12 §5.5, lose ending (MoM wins after a wrong decision): the DOS timeline from the lightning step
  (17 at +0, forced 11 / 20 / 49 at +60 / +180 / +300). The cues 1762 and 1760 and the following
  MIDNITE make no request. The wait before the gloat speech ends when the timeline is finished.
  A7 is resolved (song 20 included).
* 12 §5.2 / §5.5, midnight when the clock runs out in play: 10 plain requests of song 49, 100 ticks
  apart (3 audible starts on Tandy, 4 on the speaker). The MIDNITE wait ends when they are done.
  After a wrong decision MIDNITE makes no request.
* 12 §5.5, Champion branch: the decoded DOS timeline from the aim clip: forced 43 +60, 22 +500,
  13 ×6 from +820 every 60, 14 +1450, 48 +1750. Cues 1738 / 1739 / 1768 / 1769 are ignored there,
  and the victory step waits for the timeline. The open point is closed; A5 updated.
* 12 §5.3, score-screen Champion extra: 1727 → `force(14)`, 1739 → `force(7)`. A note gives the
  exact-timeline alternative (+210 / +390).
* 12 §5.2: hall/room music is re-requested only while the player is standing or walking (04 §5.2
  state 0–2).
* 12 §5.3: clearer rows for the Telly hum (song 17 = two chirps, restarted every frame),
  materialise (the per-frame song 50 requests of DOS are always ignored) and the trick-song repeats.
* 12 §5.3 confidence note and §5.5 introduction updated: the award, Champion and lose endings use
  DOS timelines; song 17 at the start of the correct ending is never heard.
