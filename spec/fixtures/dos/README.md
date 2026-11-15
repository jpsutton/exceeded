# DOS audio fixtures (chapter 12)

Everything here was **built from scratch** by the dirty side's `make_dos_fixtures.py`. None of it
comes from the DOS game. The songs, envelopes and priorities are invented. The only real values are
the two 12-note divisor tables, which chapter 12 §2.3 states in full as the file signature. The
expected outputs come from the dirty side's reference driver. That driver was checked against the
emulator of the original driver: identical traces for all 52 songs, Tandy and speaker.

| file | what it tests | spec |
|---|---|---|
| `exepack/pack16.exe`, `pack18.exe`, `pack18_skip3.exe` | EXEPACK: 16-byte header, 18-byte header, skip_len 3; all three unpack to `exepack/unpacked.bin` (70,624 bytes: fill runs, copy runs, one 65,535-byte run limit, uncompressed prefix) | 12 §2.1–2.2 |
| `exepack/bad_signature.exe` | must fail ("no EXEPACK signature") | 12 §2.2 |
| `game/SSO.EXE`, `game/MOCKET.DAT` | a complete synthetic DOS game folder: data segment at 0x13F30, all tables, MOCKET directory with the end entry, both music blocks with 52-entry song tables | 12 §2.3–2.6 |
| `game_bad/` | same, but one Tandy divisor is wrong → `dos-check` must report `ok: false` | 12 §2.6 |
| `check_expected.json` | `--conform dos-check` for both folders, plus the EXEPACK cases | 12 §7.1 |
| `trace_expected.json` | `--conform dos-trace` for every synthetic song, both variants (`--ticks 4000`), and song 0 cut at `--ticks 40` | 12 §3, §7.2–7.3 |
| `script_*.txt` + `script_expected.json` | `--conform dos-script` for the five scripts, both variants | 12 §3.3, §3.14, §7.4 |
| `render_tandy_song0.wav`, `render_speaker_song0.wav` | reference renders of song 0 (44.1 kHz, 16-bit mono, reference levels, speaker voicing on) for the optional `dos-render` tolerance test | 12 §4, §7.5 |

## Synthetic songs (Tandy block; the speaker block has its own versions of the same numbers)

| song | content |
|---|---|
| 0 | the tour. ch0: D4/D5/D6/D0, octaves 0–5 and 15, the > 0x3FF halving, dotted, tie chains, D1/D2 with both rounding cases, D1 0, D7 (ignored), D0 0, a rest (one-tick envelope value), zero-length notes. ch1: rest before D5, E events in every octave branch (0, 1–3, 4, 5–12), E 00, D3 0 (pass-through), an envelope running out into sustain, D5 inside a tie (only the selection changes). ch2: gate 1/20/6, no-gate, every tie transition, rest with the tie bit. ch3: all 12 noise controls (octave ignored), rest, E 00 |
| 1 | 3-voice "music" tune (low priority) |
| 2, 3 | 1-voice music |
| 6 | 1-voice effect, higher priority than songs 1–3 (channels 1–2 of a running tune keep sounding) |
| 7 | low-priority effect (same priority as song 1) |
| 8 | noise-only effect |
| 9 | short 2-voice song for the loop test |
| 10 | empty song (ends at tick 0) |
| 11 | zero-length notes only, then a timed note |
| 12 | no D4 (uses the carried-over tempo base), rest before D5 |
| 51 | its priority is read from the first envelope pointer (very high) |
| others | unused (all channels 0xFF in Tandy, a lone end event on the speaker) |

The synthetic priority table (songs 0–50) is `1 1 1 3 2 2 4 1 3 2 1 1` followed by 39 × `1`. Song 51
reads the first envelope pointer word (0x4800 = 18432). The envelope pointer table points to 36
synthetic envelopes starting at DS:0x4800 (listed in `check_expected.json`).

## Commands
```
engine --conform dos-unpack exepack/pack18.exe --out img.bin            # == exepack/unpacked.bin
engine --conform dos-check game --out c.json                             # == check_expected.json cases[0]
engine --dos-game game --conform dos-trace 0 --variant tandy --ticks 4000 --out t.json
engine --dos-game game --conform dos-script script_priority.txt --variant speaker --out s.json
engine --dos-game game --conform dos-render 0 --variant tandy --rate 44100 --out r.wav
```
In `check_expected.json` only `ok` and the listed values must match. The `reason` text is free.
