# Synthetic test fixtures

Every file in this folder was **built from scratch** to exercise the formats and algorithms in the
spec. None contains data from the original game. Expected outputs were produced by the dirty side's
reference tools, which were validated against the original data. Use these as unit tests. If a
fixture and the spec disagree, report it (see `../README.md`). Do not "fix" either side on your own.

| file | what it tests | spec |
|---|---|---|
| `rng.json` | Borland `rand`/`srand`, `scaled(n)`, `lo + rand() % n`, Park–Miller next / range / rand_step | 05 §1, 06 §3 |
| `FIXTURE.DAT` + `container_expected.json` | NE resource container, RT_NAMETABLE type names, resource lookup, padding | 01 §2 |
| `graphics_expected.json` | ASEQ cel RLE decode and script playback trace, RRGB palette, NFNT text rendering, DIALOG and MENU parsing, WAVE, TEXT, INTS (all payloads live in `FIXTURE.DAT`) | 01 §4–5, 02 §2–4 |
| `mtht_expected.json` | the MTHT interpreter: every opcode, groups, constrained random, expressions, outputs | 06 §2–5 |
| `save_SSOWINCD.DAT` + `save_expected.json` | player database layout, header list, player record, saved-game block | 03 §4 |
| `rules.json` | default drill deck sizes, drill question sequences, puzzle generation for all four tiers | 05 §2–3 |
| `dos/` | optional DOS audio: EXEPACK, DOS file validation, driver traces and request scripts, reference renders (own README) | 12 |
| `containers/` + `containers/expected.json` | synthetic CD images (ISO, Joliet, raw 2352 MODE1/MODE2, cue), a folder twin, DOS zips (stored, deflate, nested) and negative cases; expected `list-files` output (case table in 13 §8.3) | 13 |
| `audio_levels/` + `audio_levels/expected.json` | synthetic 8-bit WAVEs: BS.1770 loudness, sample peak, per-asset gain with Even out on/off, slider law (cases in 15 §7.3) | 15 |
| `setup/` + `setup/expected.json` | first-run install and start-up resolution: prepared synthetic installations (`homes/`), an invalid DOS zip, a setup-screen play script; expected `install`, `resolve` and `state` results (case table in 14 §10.4). Sources are the `containers/` files | 14 |

## FIXTURE.DAT resources

| type (ordinal) | id | content |
|---|---|---|
| NAMETABLE (15) | 1 | names custom types 0x7F01..0x7F07 = ASEQ, RRGB, TEXT, WAVE, MTHT, INTS, NFNT |
| ASEQ | 100 | 3 cels, 2 sequences, fps 10 |
| RRGB | 128 | 256 colours: R = i, G = 255 − i, B = (7·i) mod 256 (low bytes deliberately non-zero for G) |
| TEXT | 500 | `"Line one\r\nLine two"` |
| WAVE | 1700 | RIFF PCM 8-bit mono 11025 Hz, 64 samples |
| MTHT | 1 | the opcode-tour program below |
| INTS | 1700 | 1700, 1705, 1769, terminator |
| NFNT | 15001 | 3-glyph font ('A', 'B', 'C' missing) plus the missing glyph |
| DIALOG | 200 | 5 controls, DS_SETFONT, key-cap glyph 0x84 in text |
| MENU | 300 | two popups, separator, checked and grayed items |

The alignment shift is 4 (16-byte units). The game files use 9 (512 bytes); the reader must honour
the stored value.

## ASEQ 100 — script used for the trace

Sequence 0 (entries 0..16): SETPOS (10,20) with z 5 · cel 0 · cel 1 mirrored at (+2,0) · SETCOUNT 3 ·
LABEL 7 · MOVEX +3 · cel 2 at (0,+1) · REPEAT · MOVEY +4 · MULTI 2 · cel 0 (0,0) · cel 1 (+5,0) · NOP ·
cel 2 mirrored at (+1,+1) · JUMP 16 · (an entry that must never be drawn) · END.

Sequence 1: cel 1 · cel 2 · LOOP.

The trace lists what is drawn at each frame period. The fixture leaves out the opcodes that use the
RNG or game hooks (−5, −10, −12…−14, −16…−18, −21, −22, ≤ −500).

## MTHT 1 — the opcode-tour program

Variables are numbered from 1, as inside the program. "Size" is the element size in bytes.

| var | count × size | line content | expect |
|---|---|---|---|
| 1 | 1×24 | literal text | "Fixture Title" (title variable) |
| 2 | 3×2 | F6 rand 10..50 FRESH, then UNIQUE ×2 | three distinct numbers |
| 3 | 1×2 | E1 sum of 3 words from var2[0] | sum |
| 4 | 1×2 | E2 `(v2[0] + v2[1]) × 2 − v2[2]` | **answer variable** |
| 5 | 1×2 | E3 | question number |
| 6 | 1×8 | E5 itoa(var4) | decimal text |
| 7–10 | 1×2 each | E7 argmin, E8 argmax, E9 min, EA max over var2[0..2] | |
| 11 | 1×2 | EB value of var2[2] | |
| 12 | 1×2 | EC exclude 25, then F6 rand 20..30 UNIQUE | never 25, never an earlier value |
| 13 | 1×8 | F1 pick string by question number from zero/one/two/three | |
| 14 | 1×2 | F2 pick word by question number from 10/20/30/40 | |
| 15 | 1×8 | F4 random string red/green/blue | |
| 16 | 1×2 | F5 random word 100/200/300/400 | |
| 17 | 1×2 | F3 random group: DF 111 or DF 222 | |
| 18 | 1×2 | F0 group by question number: 1000, 1001, 1002, or E2 7×6 | |
| 19 | 1×2 | DF −123 | |
| 20 | 1×8 | empty line (written by var 21) | copy of var15 |
| 21 | 1×2 | ED strcpy var20[0] ← var15[0] | (writes nothing at its own position) |
| 22 | 1×8 | empty line (written by var 23) | "fixed" |
| 23 | 1×2 | EE strset var22[0] ← "fixed" | |
| 24 | 1×2 | empty line (written by var 25) | value of var10 |
| 25 | 1×2 | EF set var24[0] ← var10 | |
| 26 | 1×8 | E0 string "ab" (with NUL) then word 513 | bytes `61 62 00 01 02` |
| 27 | 1×4 | E4 then literal "ok" | |
| 28 | 1×2 | F6 3..120 step 30 | a multiple of 30; can be 120 |
| 29 | 1×2 | F6 0..99 "not above" var9 | ≤ min |
| 30 | 1×2 | F6 0..99 "not below" var10 | ≥ max |
| 31 | 1×2 | EB of var2[ value(var5[0]) ] (nested reference) | for question 3 this reads past var2 into var3 (flat buffer), on purpose |
| 32 | 1×48 | mixed literal text, E6, E5 and a `^` | |
| 33 | 2×64 | element 0 = DF 1; element 1 = question text | **question variable** |
| 34 | 1×32 | hint text with E5 | **hint variable** |

Header: answerVar = 3, questionVar = 32, hintVar = 33, titleVar = 0 (0-based). Each instance in
`mtht_expected.json` gives the full instance buffer (`buffer_hex`) and every element read both as
an int16 and as a string; compare whichever is meaningful for the variable.

## Save fixture

Three players: ALICE (31,000, record slot 2), BOB (0, slot 0) and ZED (500,123, slot 1). The header
list is sorted by name. BOB has customised settings, a saved game in progress and options byte 5
(calculator on, hints off, clock on). The other 96 records are fresh defaults.
`save_expected.json` lists the decoded values field by field.

## Rules fixture

* `drill_sets`: per tier, the deck is built from that tier's default customisation, then `srand(S)`
  is called and 12 questions are drawn. Each question shows the underlying fact, the format, the
  displayed string (`?` marks the hidden field) and the answer.
* `puzzles`: per tier, fresh program-start state, `srand(S)`, then three consecutive games. This
  also tests that the picture pool and pattern table persist across games (05 §3.4).
