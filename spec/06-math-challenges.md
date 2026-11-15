# 06 — Math challenges (MTHT word problems)

Status: format, interpreter, RNG, selection, scoring/try flow and calculator are decoded with high
confidence. Per-object display layouts are decoded to the level of data indices, background art,
main coordinates and fonts (medium confidence on fine pixel details).

---------------------------------------------------------------------------------------------
## 1. Overview

Each of the 10 "objects" in the TV-station rooms can hide a math challenge (word problem). A
challenge is produced by running a small **template program** (an MTHT resource in SSO2.DAT)
that fills an *instance buffer* with variables: random numbers, chosen names, chart data,
4 alternative question texts, the answer and a hint. The object-specific display routine then
draws a chart/map/etc. from fixed slots of that buffer; the question text, the answer and the
hint come from four designated variables.

### 1.1 Object types (index `T`, 0..9)

| T | Object (beta menu name) | "study the …" word | MTHT ids |
|---|---|---|---|
| 0 | Map | map | 1–8 |
| 1 | Bar Graph | graph | 11–18 |
| 2 | Prize is Right (prize board) | prizes | 21–28 |
| 3 | Spinner | spinner | 31–38 |
| 4 | Great 8 (hit chart) | chart | 41–48 |
| 5 | Volume (mixer sliders) | settings | 51–58 |
| 6 | Monitor (stock list on TV monitor) | list | 61–68 |
| 7 | Equipment list | chart | 71–78 |
| 8 | Line Graph | graph | 81–88 |
| 9 | VCR / tapes | tapes | 91–98 |

### 1.2 Resource id

```
sub   = operation index: 0 = Add, 1 = Divide, 2 = Multiply, 3 = Subtract
hard  = 0 or 1
MTHT id = T*10 + sub + 4*hard + 1
```
So ids x1..x4 are EASY add/divide/multiply/subtract, x5..x8 HARD add/divide/multiply/subtract.
(Bar/prize/spinner resources even carry tags "EA","ED","EM","ES","HA"… in their first variable.
Note the tag letters are not always consistent with the id, e.g. MTHT_13 says "EM"; the id
mapping above is authoritative — verified from hint texts "Add…/Divide…/Multiply…/Subtract…".)

---------------------------------------------------------------------------------------------
## 2. MTHT binary format

All integers little-endian.

```
u16  N                              number of variables
N × { u16 count; u16 size; u16 offset }   variable table
u16  answerVar, questionVar, hintVar, titleVar   (0-based variable indices)
u8   stream[]                       template program (runs to end; trailing zero padding)
```
* Variable *i* (referenced 1-based as `i+1` inside the program) occupies `count` elements of
  `size` bytes at `offset` in the instance buffer. Offsets are contiguous; buffer size =
  Σ count*size. The buffer is zero-filled before running.
* An element may hold a 16-bit signed integer (first 2 bytes) or a NUL-terminated string.
* **Flat element numbering**: all elements of all variables numbered consecutively
  (var 1 elem 0 = #0, …). Display routines access data by flat number ("#n" below); words via
  "value of #n", strings via "pointer to #n".
* The resource files are padded to 512-byte multiples; padding after the stream is zero.

### 2.1 Program execution (instantiation)

```
seedRNG(objectSeed[T])                      // see §4
buf = zeros(bufsize); P = 0; exclusion = []; stack = []
for v in 0..N-1:
  for e in 0..count[v]-1:
     W = offset[v] + e*size[v]            // write cursor
     loop:
        b = stream[P++]
        if b == 0x0A: break               // end of this element
        if b >= 0x80: execute opcode b    // may write at W (and advance W) or elsewhere
        else: buf[W++] = b                // literal byte (incl. 0x00)
```
The stream is consumed strictly sequentially; there is exactly one "line" (terminated by 0x0A)
per element, in variable order. Bytes ≥0x80 that are not opcodes (0x80–0xDE, 0xF7–0xFF) are
ignored when they occur at line level (they do occur as parts of operands only).

### 2.2 Operand syntax

* **Reference** `REF` = `10 28 a b 10 29` i.e. `\x10( var idx \x10)`: element `idx` (0-based)
  of variable `var` (1-based). Each of `var`/`idx` is either a signed byte, or a nested
  `\x10( … \x10)` whose *word value* is used (e.g. `\x10( 02 \x10( 04 03 \x10) \x10)` =
  var2[ value(var4[3]) ]). A `\x10` followed by anything other than `(`/`)` is taken as the
  literal number 16. A REF denotes a buffer *address*; "value(REF)" = int16 stored there.
  Many opcodes skip the two bytes `\x10(` blindly (they assume a REF follows).
* **VALUE** = either a REF (if the next two bytes are `\x10(`; its value is used) or a 2-byte
  literal int16.
* **FF** – most opcodes end by skipping bytes up to and including the next 0xFF.
* **CSTR** – NUL-terminated literal string.

### 2.3 Opcodes

"emit w" = store int16 w at W, W += 2. "copy s" = copy string bytes at W, W += len.

| byte | name | syntax | effect |
|---|---|---|---|
| DF | word | `DF lo hi FF` | emit literal word |
| E0 | strword | `E0 (REF \| CSTR) lo hi FF` | copy string *including* NUL (from REF's string or literal), then emit word (unused in data: 1 occurrence) |
| E1 | sum | `E1 REF VALUE FF` | emit sum of VALUE consecutive words starting at REF |
| E2 | expr | `E2 tokens… FF` | evaluate RPN expression (§2.4), emit result |
| E3 | qnum | `E3 FF` | emit the object's question number (0..3) (§4) |
| E4 | nop | `E4` | nothing (no FF skipped) |
| E5 | itoa | `E5 REF FF` | copy decimal text of value(REF) (no NUL; negative gets '-') |
| E6 | str | `E6 REF FF` | copy the string at REF (no NUL) |
| E7 | argmin | `E7 n REF×n FF` | emit 0-based index of smallest value (first one; start 9999) |
| E8 | argmax | `E8 n REF×n FF` | emit index of largest value (first; start 0) |
| E9 | min | `E9 n REF×n FF` | emit minimum (start value 9999) |
| EA | max | `EA n REF×n FF` | emit maximum (start value 0) |
| EB | val | `EB REF FF` | emit value(REF) |
| EC | exclude | `EC VALUE FF` | add value to exclusion list (if not present); emits nothing |
| ED | strcpy | `ED REFdst REFsrc FF` | copy string incl. NUL from src to dst; W unchanged |
| EE | strset | `EE REFdst CSTR FF` | copy literal string incl. NUL to dst; W unchanged |
| EF | set | `EF REFdst VALUE FF` | store word at dst; W unchanged |
| F0 | select | `F0 REF n GROUP×(n+1) FF` | k = value(REF); execute the opcode in group k only |
| F1 | pickstr | `F1 REF CSTR… FF` | k = value(REF); copy the k-th (0-based) string *including NUL* |
| F2 | pickword | `F2 REF word… FF` | emit the k-th word of the list, k = value(REF) |
| F3 | randselect | `F3 n GROUP×(n+1) FF` | k = rand(0,n); execute group k |
| F4 | randstr | `F4 n CSTR… FF` | k = rand(0,n); copy k-th string incl. NUL |
| F5 | randword | `F5 n word… FF` | emit k-th word, k = rand(0,n) |
| F6 | rand | `F6 VALUElo VALUEhi mods… FF` | constrained random number (§2.5), emit |

GROUP = `10 2A <one opcode with its operands> 10 2B` (`\x10*` … `\x10+`). Skipping a group
scans with nesting depth: `\x10*` +1, `\x10+` −1 (a `\x10`+other byte is not a delimiter).
After the chosen group the interpreter skips the remaining groups and then one byte (the FF).
In F1/F4 the strings are consecutive CSTRs followed by FF; F2/F5 words are consecutive int16s
then FF. (For F4/F5 n is a byte = number of items − 1.)

### 2.4 Expressions (E2)

RPN over 16-bit signed ints, stack-based. Token loop until FF:
* operand opcodes: `DF` (word+FF), `E1`, `E2` (nested), `E9`, `EA`, `EB` (REF+FF), `F2`, `F5`,
  `F6` – each pushes its result;
* `10 2C` = +, `10 2D` = −, `10 2E` = ×, `10 2F` = ÷ : pop a (top), pop b, push b op a;
  division truncates toward zero (C `idiv`), all results wrap to 16 bits;
* any other byte: it and the next byte form a literal int16 that is pushed (so a literal can
  never start with 0x10 or with an opcode byte; references must use `EB`).
Final result = top of stack.

### 2.5 Constrained random (F6)

```
lo = VALUE; hi = VALUE; if lo > hi: swap
step = 0; mode = 0
until FF:  CA → mode = FRESH ; CB → mode = UNIQUE
           CC REF → hi = min(hi, value(REF))     ("not above")
           CD REF → lo = max(lo, value(REF))     ("not below")
           CE VALUE → step = VALUE
           other bytes ignored
gen() = step ? rand_step(step, lo, hi) : rand(lo, hi)
FRESH : exclusion = [];  v = gen()
UNIQUE: repeat v = gen() until v not in exclusion
else  : v = gen()
append v to exclusion (if absent);  return v
```
The exclusion list is shared by the whole instantiation (EC adds too); it has room for 50
entries (no bounds check in the original). Typical usage: `rand 11..99 fresh` then
`rand 11..99 unique` ×3 → four distinct prices.

---------------------------------------------------------------------------------------------
## 3. Random number generator (problem RNG)

Park–Miller "minimal standard": state S (31-bit), S ← S·16807 mod (2³¹−1), computed with the
usual 16-bit-halves trick:
```
seed(s): S = s (16-bit value, high half 0)
next():  S = (S * 16807) mod 0x7FFFFFFF        // exact result of the original's arithmetic
         r = S & 0xFFFF; if r == 0x8000: r = 0
         return r as signed int16
rand(a,b):  if b < a: swap; u = next() + 32768 (0..65535)
            return a + ((u*(b-a) + 32767) >> 16)          // inclusive, rounded
rand_step(step, lo, hi):
            n = trunc((hi-lo+1)/step); base = trunc((lo+step-1)/step)
            return (rand(0, n) + base) * step               // multiple of step, ≥ lo
```
Note rand_step can exceed `hi` by up to one step when (hi−lo+1) is not a multiple of step
(e.g. `rand 3..120 step 30` → 30,60,90,120). Reimplement exactly to reproduce the original
problems for a given seed.

The same RNG stream continues after instantiation and is consumed by display routines
(equipment-list filler values, Great 8 filler values, prize-board flashing, monitor "static",
praise selection). Only the deterministic parts (filler values) matter for content.

---------------------------------------------------------------------------------------------
## 4. Choosing the problem (per game)

Per-game state, 10 entries each (index T):
* `objectSeed[T]` ∈ 1..10 — at new game: `1 + rand()*10/32768` (Borland C `rand()`, after
  which `srand(time)` is called). So each object has only 10 possible instances per resource
  (the beta menu "BetaSeed 1..10" matches).
* `question[T]` ∈ 0..3 — `rand()*4/32768` (Borland rand).
* `sub[T]` — pick k = problemRNG.rand(0,3) repeatedly until operation k is enabled in the
  player's problem settings; k is the customization order (0 Addition, 1 Multiplication,
  2 Subtraction, 3 Division); `sub = {0:0, 1:2, 2:3, 3:1}[k]` (maps to Add/Multiply/Subtract/Divide
  in the MTHT order). A second routine re-rolls question/sub for objects not yet solved
  (it is called when settings change; it tests the solved flag with the wrong index — quirk).
* `state[T]`: 0 none, 1 has a challenge, 2 solved, 3 failed. At tiers 0–1 one object of each
  pair (0/1, 2/3, 4/5, 6/7, 8/9) gets a challenge (coin flip); at tiers ≥2 all 10 do. (Tier = rank div 2,
  never the customised Level.)
* `wrongTries[T]` (reset per game).

**Level** L (0..3) — used **only** to choose easy or hard templates: normally rank index / 2 (Trainee+Junior 0, Senior+Pro 1, Expert+Master 2,
All-Star+Champion 3). In a customized game the level is the "Level" chosen in Problem
Customization (1–4 → 0–3), stored in the low 2 bits of settings byte 0.

**Easy/hard**: L = 0 → easy; L = 3 → hard; L = 1 or 2 → hard for Add and Subtract, easy for
Divide and Multiply.

Question index selection: the template uses `E3` (qnum) to fetch `question[T]`, and the
question variable typically does `E6 var[qnum]` to copy one of 4 texts.

---------------------------------------------------------------------------------------------
## 5. Instance outputs used by the engine

Using the 4 trailing indices of the header:
* **answer** = int16 at element 0 of `answerVar`.
* **question text** = element 0 of `questionVar` holds a word *n* (always 1 in the data);
  the displayed question is the concatenation of elements 1..n (each a string).
* **hint** = string at element 0 of `hintVar`.
* `titleVar` (always variable 0) — just the start of the display data; not used as a title by
  the generic code.

Text conventions inside strings: `^` is rendered as the degree sign (font char 0xA1); `\r`
or `\n` = line break; `~XXXYYY` (6 ASCII digits) in drawn text sets the pen position (x=XXX,
y=YYY) — supported by the text routine, not used in MTHT data.

---------------------------------------------------------------------------------------------
## 6. Challenge session flow

```
open challenge on object T (only if state[T]==1):
  sub = sub[T]; seed RNG with objectSeed[T]; load MTHT(id), run template
  draw screen: question panel ASEQ 10000 at (297,35); object display (§8); question text
  answer = answer word
  input loop (keyboard or on-screen calculator, §7) until finished or ESC
  on ESC: leave; challenge stays available (state unchanged, tries kept)
on Enter with empty entry: message "To solve the math problem, you must type in a number
  before you press Enter."
on submit: correct iff atoi(entry) == answer
  correct → SFX 1743 (see §6.1), random praise (5 strings: "That's correct!", "That's the answer!",
            "That's right!", "You're right!", "Good job!") chosen with the problem RNG;
            finish
  wrong   → wrongTries[T]++ ; SFX 1760 (see §6.1)
            1st wrong: 3 lines: "That's not correct." / "Read the question;" /
                       "study the <word>."  (word from §1.1)
            2nd wrong: "HINT: " + hint text
            3rd wrong: "That's not correct.  The correct answer is <answer>." ; finish
            entry cleared (calculator reset)
after finish (not ESC):
  if correct: state[T]=2; score += W0 − W1*wrongTries[T]; energyCredits += 2
  else:       state[T]=3
  room-clue routine is invoked (chapter 05) and a confirmation box shown
```
Points table (W0, W1) by **tier** (rank div 2; the customised Level does not affect points): L0 (300,100), L1 (300,100), L2 (600,200), L3 (600,200).
So 1st/2nd/3rd-try success = 300/200/100 (L0–1) or 600/400/200 (L2–3). Three wrong answers
give no points. (energyCredits: value 2 from the same table, consumed by the hall energy supply
machine; chapter 04 §7.)

Text placement (canvas coordinates): question text word-wrapped at x=313..481, y=70, font
NFNT 28558; feedback/hint area x=106..295 starting y=313 (lines spaced font height+7);
praise / correct-answer line at x=29..289, y=313.

---------------------------------------------------------------------------------------------


### 6.1 Session sounds

All effect requests below use loop = off. Rules from chapter 03 apply: a request for an id that is
already playing is ignored; with the Sound Effects option off every request is silently skipped and
every "wait for sound X to end" returns at once.

1. **Approach.** Pressing Up at an object plays the player's turn-towards-the-object animation. It is
   **silent** (no per-object sound). If the challenge is available and the matching new-player hint has
   not been shown yet (hints option on), hint 11 (calculator on) or hint 10 (calculator off) is shown
   first: all sounds stopped, dialog + its speech (chapter 03).
2. **Open.** All sounds **and the background music** are stopped. The challenge screen and question
   panel are drawn, then the object display builds up with its own sound (§8.10). The question text
   appears only **after** the display routine has finished, including its "wait for sound to end".
3. **During the question.**
   * Keyboard entry is silent. Rejected input (5th digit, operator when an operator is pending) is
     silent.
   * Mouse button pressed on an on-screen key (calculator keypad, or the digit keys of the answer box when
     the calculator is off): SFX **1756**, priority 2.
   * Mouse button pressed on the Exit button: SFX **1756**, priority 2 (releasing on it = Esc).
   * Message dialogs (empty entry, calculator errors): all sound effects stopped, music paused while the
     dialog is up; the dialog has no sound of its own.
4. **Answer.**
   * Correct: SFX **1743**, priority 2; the praise line is drawn; then the game **waits until 1743 has
     finished** (input blocked) and the challenge finishes.
   * Wrong, 1st or 2nd time: SFX **1760**, priority 2; feedback / hint text drawn; no wait, input
     continues (1760 plays out).
   * Wrong, 3rd time: SFX **1760**, priority 2; the correct-answer line is drawn; the game **waits until
     1760 has finished**, then the challenge finishes.
5. **Close.** Correct only: the points are added with the usual score-change SFX **1747** (priority 2)
   while the challenge screen is still shown. The room-clue routine and the "continue" box are silent.
   Esc skips all of this (no sound). Back in the room, hint 7 (solved) or hint 8 (failed) is shown once
   per player if hints are on (dialog + speech). The room music restarts through the normal per-frame
   music selection (chapter 04).

## 7. Answer entry and calculator

The calculator is an Options-menu toggle (default on; chapter 03 §3.2). Entry buffer: max **4 digits**, digits only;
a lone leading "0" is replaced by the next digit; a 5th digit is ignored (no sound). Backspace deletes;
`c`/`C`/Esc clears.

Without calculator: keys 0–9, Backspace, Enter submit, c/C clear. Background ASEQ 8534 at
(299,235) (answer box).

With calculator (ASEQ 8531 at (299,235), clickable keypad, keys `7 8 9 / 4 5 6 * 1 2 3 - 0 = = +
c Enter`): a 5-state machine, values held as 32-bit:

| state | digit | operator (+ − × ÷) | = | Enter | Backspace |
|---|---|---|---|---|---|
| 0 empty | start number → 1 | – | – | msg "must type in a number…" | – |
| 1 first number | append | remember op, operand A=entry → 2 | – | **submit** entry | erase; empty → 0 |
| 2 op chosen | clear display, start B → 3 | ignored (no sound) | – | msg "Type in the next number. Then press = …" | drop op → 1 |
| 3 second number | append | compute, result becomes A, new op → 2 | compute → 4 | msg "Press = to have the calculator work out the problem…" | erase; empty → 2 (show A) |
| 4 result shown | start new number → 1 | op with A=result → 2 | – | **submit** result | – |

Compute: × ; ÷ (B=0 → message "You cannot divide a number by zero.", reset; remainder ≠ 0 →
message "In this game, all correct answers are whole numbers (they have no remainders). Try
again.", reset); +; −. Result > 9999 → "The answer to this problem is too large for the
calculator. Try again.", reset. Result < 0 → "This calculator cannot subtract a larger number
from a smaller one.", reset. Messages are Windows dialogs 20100–20108 in the EXE ; some unused embedded strings are alternative (DOS-era) wordings of the same messages.

### 7.1 Answer display

The same display is used with the calculator (ASEQ 8531) and without it (answer box ASEQ 8534). Both
pictures are drawn at (299,235) and have the same light-blue window. Drill for Skill and the Telly drill
do **not** use this display; they have their own in-slot entry (chapter 07).

**Blank.** Fill the rectangle x 316..393, y 251..276 (inclusive, 78×26 px) with palette index **129**
(219,219,255), straight into the frame. This is one row lower than the window in the art (y 250..275),
so the fill also paints the top row of the bottom bevel (y 276) light blue. That is how the original
looks.

**Draw** (done after every key or keypad click handled by the entry, whatever the key; also run
once when the challenge opens, which only blanks the window):
1. Blank the window as above.
2. Entry text = the entry buffer (0–4 decimal digits, no sign, no padding). Font **NFNT 28558**, colour
   **0** (black). It is **right-aligned**: its total advance width (sum of the advance widths) ends at
   x = **364**, so the pen starts at 364 − width (each digit advances 8 px, so n digits start at 364 − 8n).
   **Baseline y = 267**. 28558 has ascent 12, so the glyph cells span y 255..270 and digit ink falls on
   y 256..266. Worked example: "36" puts ink at x 349..354 and 357..362.
   An empty buffer draws nothing: there is no "0", no placeholder, no cursor or underscore, and nothing
   blinks.
3. Operator: only when the calculator option is on and an operator is pending. Draw one character in
   the same font and colour, right-aligned so that its advance ends at x = **379**, **baseline y = 265**.
   Character: `*` → glyph **0xC5** (×), `/` → glyph **0xD6** (÷), `+` and `-` as themselves; the
   remainder case below uses `r`. Example: × ink at x 370..377, y 258..264.

(Coordinate note: in chapter 07 §0's "text, pixel" terms these are y = 259 for the entry and y = 257 for
the operator. The original's text routine puts the baseline at y + ascent − descent, which is 8 px for
28558, not y + ascent. The baselines above are authoritative.)

**What the display shows** (calculator on; A = first operand):

| situation | entry text | operator |
|---|---|---|
| challenge opened, after reset (C/c, error, wrong answer) | empty | none |
| state 0 → typing the first number (state 1) | the digits typed | none |
| operator pressed in state 1 (→ 2) | **A stays displayed** (the buffer is not cleared) | the new operator |
| first digit of B (→ 3) and further digits | B's digits | the pending operator |
| Backspace in state 2 (→ 1) | A | none |
| Backspace in state 3 that empties B (→ 2) | A (re-rendered from its value) | the pending operator |
| = in state 3 (→ 4) | the result | none |
| operator in state 3 (chained) | the result, which becomes A | the new operator |
| operator in state 4 | the result (= A) | the new operator |
| digit in state 4 (→ 1) | that digit only | none |

Without the calculator only the entry text is ever drawn. Digits, Backspace and C/c edit the buffer.

Error cases (the reset blanks the display at once and also resets the state, operator and A):
* Division with a remainder: the display first shows the quotient with an `r` in the operator
  position. The remainder message dialog then appears over it. After the dialog closes the calculator
  resets and the display is blank.
* Division by zero: the dialog appears while the display still shows B and ÷. Then reset (blank).
* Result > 9999 or < 0: reset first (blank), then the dialog.
* Quirk to reproduce: if a **chained operator** in state 3 triggers one of these errors, the reset is
  followed by state 2 with that operator pending and A = 0. The display shows an empty entry plus the
  operator symbol.

Clearing: C/c (key or keypad), a wrong answer, any calculator error, and opening one of the menu pop-up pages
during a challenge (the three page commands that redraw the message panel) all reset the entry and blank the display. Enter does not change the display except
through these cases: a correct answer ends the challenge, and a wrong one resets. Enter also restores
the message area (96,300)–(300,374) from the background before it is handled.

Entry rules (both modes): at most 4 digits, and further digits are ignored silently. A buffer holding
just "0" is shown as "0", and the next digit replaces it. Results are always 0..9999, so they fit in
4 digits.

---------------------------------------------------------------------------------------------
## 8. Object displays

Common: the challenge draws a screen page. `ASEQ n at (x,y)` means cel 0 of ASEQ resource n (SSO2.DAT),
drawn with its top-left corner at (x,y). It is always drawn with the global palette RRGB 128; the RRGBs
with the same ids are not used. All coordinates are pixels on the 512×384 canvas. No display uses an
origin of its own: the numbers below are absolute canvas positions. "#n" = flat element n (§2). Unless
stated otherwise, the text colour is 0. Each display has its own sound effect (§8.10).

**Text in every display uses the pixel text routine of 07 §0 only.** No display text uses design units
and none is rescaled. Each text (x, y) below is the routine input:
* x = the pen start. This is the left end of the advance box, before the glyph's kern/left offset is
  applied.
* y = the routine input y. The baseline is y + ascent − descent. The glyph bitmap's top row is
  y − descent.

Baseline offsets used here: **F0 (28625) +10, F1 (28568) +14, F2 (28558) +8, FC (19637) +14,
FM (25240) +12.**

Fonts: F0 = NFNT 28625, F1 = NFNT 28568, F2 = NFNT 28558, FC = NFNT 19637 (loaded for the challenge),
FM = NFNT 25240 (map).

Notation: w(F,s) = the sum of the advance widths of s in font F. ⌊v/2⌋ = integer halving. Every value
halved here is ≥ 0. Alignment words mean exactly this:
* **centred on c**: x = c − ⌊w/2⌋.
* **right-aligned at r**: x = r − w (r is one past the last advance pixel).
* **left at l**: x = l.

When a centre is written as a formula such as ⌊(a + b − w)/2⌋, use that formula exactly. Writing it
as c − ⌊w/2⌋ can be one pixel off.

Integers are printed in decimal with no padding.

### 8.0 Map (T=0)
* Background ASEQ 8500 at (13,36).
* Title #0 in FC, at x = 13 + ⌊(280 − w)/2⌋, y = 42.
* The three cities: names #1, #2, #3 and value labels #6, #7, #8 (strings such as "29^" or "123 mm";
  "^" prints the degree glyph). If word #4 ≠ 0, the value labels are not drawn.
* City table (label (x,y), dot (x,y)):

| city | label (x,y) | dot (x,y) | | city | label | dot |
|---|---|---|---|---|---|---|
| Boston | 282,100 | 283,133 | | Dallas | 147,177 | 159,211 |
| New York | 296,111 | 273,144 | | Houston | 158,206 | 164,242 |
| Dover | 246,129 | 264,162 | | Topeka | 159,127 | 168,163 |
| Miami | 244,221 | 254,258 | | Seattle | 46,88 | 40,81 |
| Tampa | 235,214 | 246,248 | | Oakland | 50,134 | 38,165 |
| Atlanta | 222,184 | 232,220 | | Fremont | 46,174 | 37,170 |
| Chicago | 190,97 | 205,128 | | Pasadena | 50,166 | 55,201 |
| St. Louis | 181,120 | 189,157 | | San Diego | 57,167 | 59,207 |
| Detroit | 215,92 | 230,124 | | Phoenix | 81,169 | 88,205 |

* For each city, in the order #1, #2, #3:
  1. Draw the dot: an inclusive filled rectangle (dotX−2, dotY−2)–(dotX+1, dotY+1) in colour 1
     (4×4 pixels).
  2. Draw the name in FM. Let h = ⌊w(FM,name)/2⌋. x = labelX + 5 − h, then x = max(x, 17), then
     x = min(x, 288 − 2h). y = labelY clamped to [53, 228].
  3. Draw the value label in FM, centred on x + h (the name's centre): x' = x + h −
     ⌊w(FM,label)/2⌋, y' = y + 15.
* A name that is not in the table has no dot. Its label position is (0,0) before clamping.

### 8.1 Bar Graph (T=1)
* Background ASEQ 8501 at (14,36). n = #2 bars (≤ 5). Bar values #3..#(3+n−1). Axis base B = #8, grid
  step S = #9. X labels #10.., x-axis title #15, y-axis title #16, chart title #17. Bar colour =
  [13,5,2,249,13][#18].
* gap = ⌊(188 − 17n)/(n+1)⌋. Bar i has its left edge L_i = 76 + gap + (gap+17)·i.
* Drawing order: y tick labels → y-axis title → x labels → x-axis title → chart title (all of these
  before any bar grows) → then the bars, one after another.
* **Y tick labels** (F2): for k = 0..5 the value B + k·S. Prefix "$" if #17 is exactly
  "Cassette Cost" or "Cost of Video Tape". Right-aligned at 72 (x = max(0, 72 − w)),
  y = 210 − 26k. The y is not adjusted further.
* **Y-axis title** (F0, vertical, one character per step): n_c = the length of #16 in characters,
  spaces included. The first y = 148 − ⌊(16·n_c − 3)/2⌋. For each character: a space adds 13 to y and
  draws nothing. Any other character is drawn centred on x = 32 (x = 32 − ⌊w(ch)/2⌋) at the current y,
  then y += 16.
* **X labels** (F2): #(10+i), centred on L_i + 8, y = 219.
* **X-axis title** (F0): #15, centred on 170, y = 235.
* **Chart title**: #17 in F1. If w(F1,#17) > 188, use FC instead. Centred on 150, y = 52.
* **Bars**: h = ⌊(value − B)·26 / S⌋. The bar grows upward one row per cs, from y = 213 up to
  y = 215 − h (that is h − 1 rows; nothing is drawn if h ≤ 1). Each row y gets:
  * colour 0 at (L_i − 1, y) and at (L_i + 18, y);
  * the bar colour from L_i to L_i + 17 inclusive (18 px).

### 8.2 Prize board (T=2)
* Background ASEQ 8502 at (13,36). Prices #1..#4 for prizes A..D, drawn as "$"+decimal in F2.
* Price p is drawn with its left at x = X[p] + 13 and y = Y[p] + 39 (routine input y), where
  X = [54,103,217,209] and Y = [120,43,58,143].
* Frame rect of p: (x1+10, y1+39)–(x2+13, y2+39), with x1 = [38,136,196,188], y1 = [124,47,64,147],
  x2 = [88,186,216,238], y2 = [154,77,94,177] (used only by the recolour of §8.11).
* Animation: as in §8.11 (unchanged).

### 8.3 Spinner (T=3)
* Background ASEQ 8505 at (13,36). Pointer = ASEQ 8520 cel k, top-left as given in §8.11.
* **Segment values** #5..#14 (decimal, F0, colour 0). Value i is **centred on X[i] + 16**:
  x = X[i] + 16 − ⌊w(F0,s)/2⌋, y = Y[i] + 48. Here X = [139,187,208,208,187,139,91,70,70,91] and
  Y = [35,53,86,130,167,185,167,130,86,53]. The resulting centres (x) and baselines are (155,93)
  (203,111) (224,144) (224,188) (203,225) (155,243) (107,225) (86,188) (86,144) (107,111).
* **Centre text.** Do everything below only if the first character of #17 is not a space. If #17
  starts with a space, nothing at all is drawn in the centre: no #17, no "SPIN", no #18 and no
  rectangle.
  1. #17 in F2, centred on 156, y = 142.
  2. Unless #17 is exactly "LAST": "SPIN" in F2, centred on 156, y = 178.
  3. If the first character of #18 is not a space: #18 in **F0**, centred on 156, y = 159.
     Otherwise: a filled rectangle (128,156)–(181,171) inclusive, colour 0xFD.
* The ten values are drawn after the pointer and the centre text, so they appear over the pointer. The
  landed pointer partly covers the value of the landed segment, and that value stays readable on top of
  it. This is correct.
* The white box behind the centre number is part of the background art. It is not drawn by the display.

### 8.4 Great 8 chart (T=4)
* Background ASEQ 8503 at (13,36). All text is in F2, colour 0. Rows i = 0..7 have y_i = 103 + 18(i+1).
* **Headers:** two of them, 3 lines each, at y = 67, 79, 91. The header code for value column 1 is #0;
  for value column 2 it is #1. Codes: 3 → "Last/Wk's/Pos.", 4 → "Weeks/on/Chart",
  5 → "Calls/This/Week", 6 → "Calls/Last/Month". Each line s goes at:
  * column 1: x = ⌊(408 − w(s))/2⌋;
  * column 2: x = ⌊(518 − w(s))/2⌋.
* **Ranks and names:** rank text "1.".."8." at x = 19; names at x = 32. Rows are 0-based. Row r1 = word
  #2 gets name #12, and row r2 = word #3 gets name #13. **r1 and r2 come from the data (any 0..7). They
  are not fixed rows.** All other rows get names #14, #15, … in increasing row order.
* **Value column 1:** every row, right-aligned at R1 = ⌊(w(F2, decimal(#9)) + 408)/2⌋. Only the width
  of the upper bound #9 is used, not the widest value actually shown.
  * Row r1: #4. Row r2: #6.
  * Row 2, if it is neither r1 nor r2 and #20 > 7: #22.
  * Every other row: v = rand(#8,#9), drawn again until v is not in the exclusion list. The list starts
    as [#4, #6], plus #22 if #20 > 7, and each accepted filler is added to it.
* **Value column 2:** right-aligned at R2 = ⌊(w(F2, decimal(#11)) + 518)/2⌋.
  * Row r1: #5. Row r2: #7.
  * Row 2 under the same condition as above: #23.
  * Every other row: rand(#10,#11), with no distinctness test.
* **Order** (this affects the RNG): headers → all ranks and names (rows 0..7) → all of value column 1
  (rows 0..7) → all of value column 2 (rows 0..7).
* Animation: ASEQ 8511 cels 0..3 cycling at (18,62), 12 frames × 27 ticks (unchanged).

### 8.5 Volume / mixer (T=5)
* Background ASEQ 8504 at (14,36).
* Title #9 in F1: x = 157 − ⌊w/2⌋, y = 49.
* Three sliders (100s, 10s, 1s) with knob ASEQ 8533 cel 0 at x = [34,125,216]. Slider i has the
  setting v = #(24+i).
* Each slider runs steps s = 0..17v. The knob's top-left goes to (x, 244 − s). Before each step after
  the first, the rectangle of the previous knob is restored from the background. Present each step and
  wait 1 cs.
* So the knob always starts at y = 244 and ends at y = 244 − 17v. **A slider with v = 0 still gets its
  knob drawn once at y = 244** (silently). The background has no knobs.

### 8.6 Monitor (T=6)
* Background ASEQ 8506 at (13,36).
* Title #24 in F0: x = 13 + ⌊(274 − w)/2⌋, y = 64.
* Six rows k = 0..5 at y = 83 + 20k, all in F2:
  * name #(5+k) with its left at 57;
  * value decimal(#(11+k)) right-aligned at 200;
  * unit #(17+k) with its left at 206.
* Then the TV-static effect of §8.11 (unchanged).

### 8.7 Equipment list (T=7)
* Background ASEQ 8509 at (13,36). All text is in F2, colour 0.
* Lines k = 0..7 have y_k = 123 + 18k. Let r = word #0, the subject row.
* **Names** (left at x = 20, one string per line, 8 lines). Build the line list from line 0 upward:
  * At line r, put the subject name #10. If #10 contains spaces, it is split at every space over
    consecutive lines. Every piece after the first keeps its leading space ("Paint Brushes" →
    "Paint", " Brushes").
  * All other lines take #11, #12, … in order.
  * Lines past 8 are dropped.
* **Value rows.** The values follow a row cursor Y: start at Y = 105 and walk the row indices i = 0..7.
  * For a normal row: Y += 18, then draw at Y.
  * For the subject row, if #10 contains a space: Y += 9, then Y += 18, draw at Y, then Y += 9, and the
    next row index is skipped. The subject's values then sit halfway between its two name lines, and
    the later rows line up with their names again.
* **Column 1**, centred in [98,187]: x = ⌊(286 − w)/2⌋.
  * Subject: decimal(#1) + #18. #18 is dropped if it is one character long.
  * Filler row j (j = 0, 1, … in row order): decimal(rand(#4,#5)) + #(19+j).
* **Column 2**, right-aligned at R2 = ⌊(w("180") + 426)/2⌋ (= 225 with F2).
  * Subject: #2.
  * Fillers: rand(#6,#7).
* **Column 3**: the number is right-aligned at R3 = ⌊(w("$1500") + 525)/2⌋ (= 282 with F2). The "$" is
  drawn separately on every value row at the fixed x = R3 − w("$1500") (= 242). It is **not** placed
  directly in front of the number.
  * Subject: #3.
  * Fillers: rand(#8,#9).
* **Order** (this affects the RNG): all "$" signs → all names → column 1 for every row → column 2 for
  every row → column 3 for every row.
* No visible animation. After the rows there is a pause of 8 × delay(50) (see §8.11) while 1718 plays.
  The header bands do **not** change colour.

### 8.8 Line graph (T=8)
* Background: easy ASEQ 8507 at (14,36); hard ASEQ 8508 at (14,36).
* Title #17 in FC, centred on 154, y = 43.
* **Y-axis title #18 in FC**, vertical. n_c = its length including spaces.
  * Start y: easy max(145 − ⌊17·n_c/2⌋, 73); hard max(129 − ⌊18·n_c/2⌋, 72).
  * For each character: a space adds 14 to y. Any other character is centred on x = 33, then y += 17.
* **X-axis title #19 in FC**, centred on 177, y = 238 (easy) or 210 (hard).
* **Tick labels** (F2): #4 + k·#5, right-aligned at 71. Easy: k = 0..4, y = 197 − 29k. Hard: k = 0..3,
  y = 170 − 29k. The y is not adjusted further.
* **X labels** (F2): #(20+i) for i < #2, centred on x_i = 104 + 36i, y = 226 (easy) or 198 (hard).
* **Point y:** d = (v − #4)·29, q = trunc(d / #5), r = d − q·#5. y(v) = Y0 − q, minus one more if
  r > ⌊#5/2⌋. Y0 = 201 (easy) or 171 (hard).
* **Order:** background, title, y title, x title, tick labels and all x labels are drawn first. Then, on
  the hard graph if #3 > 1, the legend. Then the series.
* **Legend** (hard, #3 > 1): text #25 at (76, 235) and #26 at (76, 251), both F2, colour 0. Sample
  lines from x = 21 to 72:
  * at y = 236, colour 0xF9, thickness 1;
  * at y = 252, colour 0, thickness 0.
* **Lines:** a segment with thickness t is a Bresenham line. At each of its points (x, y), draw the
  vertical run (x, y−t)…(x, y+t) (2t+1 px) for every slope, steep or flat.
* **Series 1:** a 9×9 marker (x−4, y−4)–(x+4, y+4) in colour 0xF9 at the first point (silent). Then for
  each next point: a line of thickness 1 in colour 0xF9, then its marker, then the sound/delay of
  §8.10.
* **Series 2** (hard, #3 > 1): values #11.., 7×7 markers (x−3, y−3)–(x+3, y+3) in colour 0, lines of
  thickness 0 in colour 0. Same sequence.

**Line rasterisation and y-axis title step (v1.18):**
* Let dx = |x1 − x0| and dy = |y1 − y0|.
* **If dy ≤ dx (x-major):**
  1. Swap the endpoints if x0 > x1.
  2. Plot (x0,y0).
  3. Set d = 2dy − dx.
  4. For x = x0+1 … x1: if d ≥ 0, step y one pixel towards y1 and add 2(dy − dx) to d; otherwise add
     2dy to d. Then plot (x, y).
* **Otherwise (y-major):** the same with x and y exchanged.
  1. Swap the endpoints if y0 > y1, so the walk always starts at the endpoint with the smaller y.
  2. Plot (x0,y0).
  3. Set d = 2dx − dy.
  4. For y = y0+1 … y1: if d ≥ 0, step x one pixel towards x1 and add 2(dx − dy) to d; otherwise add
     2dx to d. Then plot (x, y).
* Every plotted point (x, y) draws the vertical run (x, y−t)…(x, y+t).
* Also, §8.8 "Y-axis title": the step per drawn character is 17 px on **both** graphs. The 18 in the
  hard graph's start-y formula is only a factor in that formula.

### 8.9 VCR / tapes (T=9)
* Background ASEQ 8510 at (14,36). Text in F2.
* Tapes i = 0..3 are D, C, B, A, drawn bottom to top at y_i = 191, 155, 119, 83. For each tape:
  * tape ASEQ 8530 at (81, y_i);
  * the letter with its left at 124, y = y_i + 14;
  * the length decimal(#(3−i)) right-aligned at 185, y = y_i + 15;
  * the unit #4 with its left at 190, y = y_i + 15.
* Each tape appears with a sound, followed by delay(60), delay(50), delay(40), delay(30). These are the
  scripted delay of §8.11 (24, 20, 16, 12 cs), not raw ticks.

### 8.10 Display sounds

Every display uses exactly one SFX id. All are priority 10 and not looped, except the Spinner tick
(priority 2). Each display loads its sound, plays it as described, and at the end **waits until the sound
has finished** (immediately true if it was stopped or sound is off) before returning.

| T | display | SFX | length | when |
|---|---|---|---|---|
| 0 | Map | 1734 | 3.9 s | once, after the title is drawn, before the cities; then wait for it to end |
| 1 | Bar graph | 1701 | 1.9 s | once per bar, restarted for each bar; cut when that bar stops growing |
| 2 | Prize board | 1742 | 1.1 s | once, when the flashing starts; flashing lasts as long as the sound |
| 3 | Spinner | 1751 (prio 2) | 0.05 s | one tick per pointer step |
| 4 | Great 8 | 1722 | 2.2 s | once, when the cel animation starts; animation lasts as long as the sound |
| 5 | Volume | 1736 | 2.2 s | once per slider that moves; cut when the knob stops |
| 6 | Monitor | 1740 | 0.7 s | once, when the static starts; static lasts as long as the sound |
| 7 | Equipment list | 1718 | 1.9 s | once, right after the background, before the rows; then wait for it to end |
| 8 | Line graph | 1730 | 0.17 s | once per line segment drawn (both series on the hard graph) |
| 9 | VCR / tapes | 1758 | 0.12 s | once per tape appearing |

Timelines (sound on; "wait end" = wait until the display's sound has finished):

* **Map (T=0):** background + title → start 1734 → draw the three dots, names and value labels → wait
  end (≈3.9 s in total; the finished map stays on screen until then).
* **Bar graph (T=1):** for each bar in order: start 1701 → bar grows 1 px/tick to its height → **stop
  1701** → 50-tick delay. (A bar of height 0 gives an immediately cut 1701.)
* **Prize board (T=2):** prices drawn → N = random 5..8 is drawn (same RNG as the existing §8.2 flash count) →
  start 1742 → flash steps repeat **while 1742 is still playing** (sound off: N flash steps) → final
  redraw of the four prices → wait end.
* **Spinner (T=3):** for each step s = 0, 1, 2, … (max 40): start 1751 → draw pointer cel (s mod 10) and
  centre text → wait s ticks → **stop 1751**; stop after the landing step as now. Then wait end. (The
  step-0 tick is stopped at once and is inaudible; later ticks are 0.05 s, so the result is a click per
  step that slows down.)
* **Great 8 (T=4):** chart drawn → start 1722 → cel cycle 0..3 at 27 ticks per frame **while 1722 is
  playing** (sound off: exactly 12 frames) → wait end.
* **Volume (T=5):** title → for each slider (100s, 10s, 1s) whose setting v is not 0: start 1736 → knob
  moves 17·v px at 1 px/tick → **stop 1736**. Sliders with v = 0 neither move nor sound.
* **Monitor (T=6):** title and six rows drawn → start 1740 → static frames (at least 1 tick each) continue
  **while 1740 is playing, plus one more frame if the frame count is odd** (sound off: exactly 60 frames)
  → wait end.
* **Equipment list (T=7):** background → start 1718 → all rows drawn → pause 8 × 20 cs (no visible
  change, §8.11) → wait end.
* **Line graph (T=8):** first marker drawn silently; for each following point: segment + marker drawn →
  start 1730 → 150-tick delay → wait end. Hard graph with a second series: the same again for every
  segment of series 2.
* **VCR (T=9):** for each tape D, C, B, A: tape, letter, length and unit drawn → start 1758 → delay
  60 / 50 / 40 / 30 ticks → wait end.

Sound off: no sound is played; the Prize board, Great 8 and Monitor use their fixed counts given above;
all other delays and movements keep their timing; the Map then has no pause.

No display uses animation-script sound cues (user opcodes); all their sounds are the direct requests above.

**Timing note (v1.16):**
* The "50-tick delay" after each bar (§8.1/§8.10) is delay(50) = 20 cs.
* The "150-tick delay" per line segment (§8.8/§8.10) is delay(150) = 63 cs.
* The VCR delays are as stated in §8.9 above.
* The Spinner's per-step wait and the bar/knob 1-cs steps are raw centiseconds.

### 8.11 Display animation details

Conventions: `rand(a,b)` is the problem RNG of §3 (the same stream; the C runtime rand() is never
used). "now" is the 100 Hz tick clock (cs). `delay(n)` is the scripted-delay helper,
`floor(floor(n·4/13)·100/73)` cs (so delay(50) = 20 cs). "Recolour rect (x1,y1)-(x2,y2) from A to B"
means every pixel whose palette index equals A in x1 ≤ x < x2, y1 ≤ y < y2 becomes B.

**Prize board (T=2).**
* T0 = now, taken when the display starts (before the background is drawn). The four price strings
  "$"+#1..#4 are prepared but **not drawn** yet.
* N = rand(5,8) is always drawn first, then 1742 starts. prev = −1.
* While (sound on: 1742 is playing; sound off: fewer than N flashes done) — the test comes before
  each flash, so zero flashes are possible — do flash k = 1, 2, …:
  1. p = rand(0,3), repeated until p ≠ prev; prev = p.
  2. c1 = L[rand(0,15)], repeated until c1 is not 0xFF or 0xFB, where
     L = [04 00 02 0D 01 05 10 11 07 FC FA FE F9 FD FB FF]. Draw price p in colour c1 at its position
     (§8.2), directly over whatever is there (no erase).
  3. c2 = L[rand(0,15)], repeated until c2 is one of {02,0D,05,10,11,FE,F9,FD}. Recolour p's frame rect
     (§8.2) from 0xFB to c2.
  4. Wait until now ≥ T0 + 6·k cs (cumulative schedule; no wait if already late).
  5. Draw price p in colour 0xFF (white); recolour the frame rect from c2 back to 0xFB.
  The shipped background has no 0xFB pixels, so steps 3/5 change nothing visible; they must still
  consume the RNG. Visible result: a price shows in colour c1 for about 6 cs, then stays white.
* End: with screen presentation held, draw all four prices in colour 0 in a random order: each pick
  is p = rand(0,3) repeated until p is not yet drawn and p ≠ prev (prev = the last flashed price for
  the first pick only, then −1). Then present once: the four black prices appear simultaneously.

**Spinner (T=3).**
* The background is stamped into both the screen and the background buffer.
* Step s (0, 1, 2, …): start 1751; if s > 0, restore the previous pointer cel's rectangle from the
  background buffer; draw ASEQ 8520 cel k = s mod 10 with its top-left at (144+dx_k, 65+dy_k), the
  offsets coming from the ASEQ's sequence entry for cel k:
  dx,dy = (0,0) (27,25) (44,59) (44,97) (30,125) (−3,141) (−53,121) (−83,98) (−80,63) (−54,24),
  i.e. top-left (144,65) (171,90) (188,124) (188,162) (174,190) (141,206) (91,186) (61,163) (64,128)
  (90,89). Then redraw the centre text (or the 0xFD rect) **and all ten segment values** on top of the
  pointer, and present. Wait until now ≥ (time at that point) + s cs — raw centiseconds, not delay(n).
  Stop 1751. Stop after this step if s > 13 and s mod 10 = #2 (otherwise up to s = 39).
* The last pointer cel stays on screen.

**Monitor static (T=6).**
* count = 51, frame f = 0. Loop while (sound on: 1740 is playing, or f is odd; sound off: f < 60);
  the test comes before each frame.
* Each frame: wait until now ≥ (now at frame start) + 1 cs. If f mod 4 = 0, count −= 1 (so frame f has
  50 − floor(f/4) blocks). On even frames choose new positions for i = 0..count−1:
  x_i = 43 + rand(0,220) (43..263), then y_i = 62 + rand(0,142) (62..204). Odd frames reuse the
  previous frame's positions without calling the RNG.
* For each block, invert the four pixels (x,y), (x+1,y), (x,y+1), (x+1,y+1): new index = old index
  XOR 0xFF (= 255 − index). Present once per frame; f += 1.
* Because every odd frame repeats the even frame's XOR and the loop always ends after an even number
  of frames, the picture (including the title and rows) is exactly restored at the end. Nothing
  accumulates.

**Equipment list (T=7).** The header-band colour rotation is disabled in the original (the routine
returns immediately): no pixels and no palette entries change. The "8 rotations" are just
8 × delay(50) = 8 × 20 cs = 1.6 s of pause with the finished table unchanged, while 1718 keeps playing;
then wait for 1718 to end. Do not paint the bands (doing so would wipe the header labels).

## 9. Notes on the data

* Displayed data and question stay consistent because both come from the same buffer:
  e.g. Map: v4[3] = random region 0..2, cities v2[0..2] each picked from a regional list,
  temperatures v5[1..3] = rand 15..80, answer = delta + temperature[region].
* Templates often store display parameters into variable 1 with `EF set` (equipment list,
  Great 8, VCR) after computing them, overwriting an earlier `qnum` copy.
* Inline literal `{0}`-style words (DF) are used for counts/flags such as v11[0]=1 (one
  question element).
* See §11 for a worked test vector.

---------------------------------------------------------------------------------------------
## 10. Open questions

1. Bar-graph x label "$" prefix flag appears never set (dead code); the y-axis "$" rule is live.
2. The re-roll routine that runs when settings change mid-game tests the "solved" flag using the
   operation number instead of the object index (original bug). Recommended: skip objects whose
   state is 2 or 3; offer the literal behaviour behind a fidelity switch.
3. The hard line-graph templates (MTHT 85–88, two series) were only checked statically.
4. (Resolved in v1.5) The spinner segment font is the default font NFNT 28625.
5. Fine pixel offsets of the object displays were not compared against a running original. Cels have
   no hotspot: a cel is drawn with its top-left corner at the given position (plus the sequence
   entry's dx/dy, which is 0 for these still images).

---------------------------------------------------------------------------------------------
## 11. Test vector

MTHT 1 (Map, Add, easy), objectSeed = 1, question = 0, after `seed(1)` of the problem RNG:

| variable | value |
|---|---|
| title (#0) | "Noon Temperature" |
| cities #1..#3 | Fremont, Houston, New York |
| labels #6..#8 | "29°", "38°", "19°" |
| temperatures #11..#13 | 29, 38, 19 |
| delta #21 | 13 |
| answer #26 | **51** |

Question: "Oops! Somebody goofed! The temperature shown on the map for Houston is 13 degrees (°)
lower than it should be. What is the real noon temperature in Houston?"
Hint: "Add 13 to the temperature shown for Houston."

An implementation of §2–§3 must reproduce this exactly. (Park–Miller check: after `seed(1)`,
`next()` returns 16807, 15089, −21287, 3114, −18558.)
