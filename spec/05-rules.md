# 05 — Game rules: RNGs, drills, deduction puzzle, scoring, ranks

Scope: rules and numbers that govern a game, independent of rendering and real-time movement.
Movement/Telly/LiveWire AI/clock rate → chapter 04; MTHT word-problem format → chapter 06;
save files, dialogs, audio → chapter 03. Cross-references are given where the layers touch.

Confidence markers: **[high]** read directly from code/data and cross-checked; **[med]** read from code
but intent inferred; **[low]** guess.


---

## 0. Key concepts and the "tier"

* **Rank** (0..7): Trainee, Junior, Senior, Pro, Expert, Master, All-Star, Champion, derived from the
  player's lifetime score (§5.1).
* **Tier** `T = rank >> 1` (0..3). Almost every rule parameter is indexed by tier, not rank:

| Tier | Ranks | Drill Qs per Telly | Clues per math challenge | Challenges needed per room | Match Rule |
|---|---|---|---|---|---|
| 0 | Trainee, Junior | 4 | 4 | 1 | 0 (same pictures, same pattern) |
| 1 | Senior, Pro | 6 | 4 | 1 | 0 (same pictures, same pattern) |
| 2 | Expert, Master | 8 | 2 | 2 | 1 (same pictures, different pattern) |
| 3 | All-Star, Champion | 10 | 2 | 2 | 2 (different pictures, same pattern) |

The tier is recomputed from the current lifetime score at the start of every game (after sign-in and
after every "play again"), and is fixed for that game. **[high]**

### 0.1 Per-tier parameter table

| Field # | T0 | T1 | T2 | T3 | Meaning |
|---|---|---|---|---|---|
| 0 | 300 | 300 | 600 | 600 | Math challenge base points **[high]** |
| 1 | 100 | 100 | 200 | 200 | Math challenge penalty per wrong try **[high]** |
| 2 | 10 | 15 | 30 | 40 | Telly spawn chance per frame, out of 1000 (chapter 04 §6.1) **[high]** |
| 3 | 20 | 25 | 30 | 35 | Telly throw chance per frame while stopped, % (chapter 04 §6.1) **[high]** |
| 4 | 0 | 30 | 40 | 50 | Telly is *lit* unless random(1..100) ≤ this, i.e. lit chance 100/70/60/50 % **[high]** |
| 5 | 1 | 2 | 2 | 2 | Telly walk speed, columns/frame; trick speed random(v, v+1) (chapter 04) **[high]** |
| 6 | 8 | 8 | 6 | 4 | Telly materialise distance: appears POPIN+8 columns from the player (chapter 04) **[high]** |
| 7 | 20 | 30 | 40 | 50 | LiveWire appearance chance in % per room entry / challenge end (chapter 04 §6.2) **[high]** |
| 8 | 1000 | 2000 | 3000 | 5000 | "Winning Game" bonus **[high]** |
| 9 | 100 | 150 | 300 | 400 | Bonus points per unit of energy left **[high]** |
| 10 | 25 | 35 | 50 | 60 | Bonus points per game minute left **[high]** |
| 11 | 20 | 30 | 40 | 60 | unknown (no reader found) |
| 12 (low byte) | 2 | 2 | 2 | 2 | Energy units credited per solved math challenge **[high]** |

Other per-tier tables:
* Winning-bonus count-up step: 50, 100, 150, 250. (Cosmetic: the Winning Game bonus is
  added in chunks of this size.)
* Clues revealed per math challenge: 4, 4, 2, 2.

---

## 1. Random number generators

Two independent generators exist. Both must be reproduced bit-exactly if outcomes are to match.

### 1.1 RNG-A: Borland C `rand()` (the "global" RNG)  **[high]**

State: 32-bit unsigned `S`, initial value 1.

```
srand(seed16):  S = seed16            (seed is a 16-bit unsigned; high half cleared)
rand():         S = (S * 0x015A4E35 + 1) mod 2^32
                return (S >> 16) & 0x7FFF          # 0..32767
```
Test vector: after `srand(1)`, `rand()` returns 346, 130, 10982, 1090, 11656.

Helpers used by the game (all on RNG-A):
* `scaled(n) = floor(rand() * n / 32768)` → 0..n-1 (used for almost every choice in this spec).
* `rnd_mod(lo, hi) = lo + rand() % (hi - lo + 1)` (used for percent rolls, e.g. random(1,100)).

### 1.2 RNG-B: Park–Miller "minimal standard"  **[high]**

State: 32-bit `P`, initial value 1.

```
pm_seed(v16):  P = v16                              (16-bit value, high half cleared)
pm_next():     P = (16807 * P) mod (2^31 - 1)        (computed with the usual 16-bit split;
                                                      result identical to the exact modulus)
               r = P & 0xFFFF, interpreted as signed 16-bit
               if r == -32768: r = 0
               return r                              # -32767..32767
pm_range(lo, hi):   if hi < lo: swap
                    u = pm_next() + 32768            # 0..65535 (32768 for the remapped value)
                    return lo + (((hi - lo) * u + 0x7FFF) >> 16)     # arithmetic shift
```
Note `pm_range` rounds, so the end values are half as likely as interior ones
(e.g. `pm_range(0,4)` → 0 and 4 each 12.5 %, 1..3 each 25 %).
Test vector: after `pm_seed(1)`, `pm_next()` returns 16807, 15089, −21287, 3114, −18558.

### 1.3 Seeding timeline  **[high]**

1. Program start (once): `srand(time(NULL))` (C `time()`, seconds since 1970, truncated to 16 bits).
2. Each **new game** (not a resumed one):
   1. `gameSeed = rand()` (or a debug fixed seed if the BetaSeed menu set one).
   2. `srand(gameSeed)`; `gameSeed` is stored in the saved-game block ("Show Game Seed" shows it).
   3. Ten math-challenge seeds: for object i = 0..9: `mathSeed[i] = scaled(10) + 1` (1..10).
   4. `srand(timeGetTime() / 10)` — i.e. **everything after this point is time-seeded**, not
      reproducible from `gameSeed`.
   5. Per-object variant/op choices and which objects hold challenges (§3.6), then the deduction
      puzzle (§3.2).
3. **Resumed game**: the saved state (incl. puzzle and gameSeed; the mathSeeds are kept in the player record, chapter 03 §4.2) is reloaded and
   `srand(gameSeed)` is called; nothing is regenerated.
4. RNG-B is re-seeded with `mathSeed[obj]` every time a math challenge object is opened, so a given
   object always produces the same problem numbers within one game (and across save/resume). Between
   challenges RNG-B simply continues (it is also used, unseeded, to pick each object's operation at
   new-game time, and to pick the math-challenge praise string).

**Consequence for a re-implementation:** only the math-challenge numbers are a deterministic function
of the game seed. The puzzle, drill questions, praise choice, Telly/LiveWire rolls etc. come from a
time-seeded RNG-A stream; a reimplementation may use any seeding policy, but to reproduce the
*original's* behaviour for a given RNG state use the exact call order documented below.

---

## 2. Drill questions ("Inside Telly's Belly" and Drill for Skill)

### 2.1 Customization record (13 bytes)

Each player has two 13-byte records: one for the main game, one for Drill for Skill (DfS). One active
copy (`cust[0..12]`) is used at run time. Bits are packed per byte index:

| Bit | Byte indices | Meaning |
|---|---|---|
| 0x10 | 0..3 | operation enabled: 0 = +, 1 = ×, 2 = −, 3 = ÷ |
| 0x08 | 0..2 | drill format enabled: 0 = `a+b=?`, 1 = `?+b=c`, 2 = `a+?=c` |
| 0x04 | 0..12 | number n emphasized |
| 0x03 | 0 | main game: word-problem **Level** 0..3 ("Level 1..4"); DfS: **set size** index 0..3 (5,10,15,20) |

Dialog mapping (DIALOG_220 = DfS, DIALOG_221 = main game): checkbox IDs 101..104 → op 0..3
(Addition, Multiplication, Subtraction, Division), 105..117 → numbers 0..12, 118..120 → formats 0..2,
radio 121..124 → the 2-bit field. **[high]**

Default records per tier (also what "Reset" restores, and what DIALOG_221's Level radio loads —
selecting Level k loads the tier-(k-1) defaults, so "4 (Selects all)" = everything):

| Tier | Bytes (hex) | Ops | Emphasized numbers | Formats | 2-bit field |
|---|---|---|---|---|---|
| 0 | 1c 04 14 04 04 04 00 00 00 00 00 00 00 | + − | 0–5 | a+b=? | 0 (DfS: 5 per set) |
| 1 | 1d 0c 1c 04 04 04 04 00 04 00 04 00 00 | + − | 0–6, 8, 10 | all three | 1 (10) |
| 2 | 1e 14 14 14 04 04 04 04 04 04 04 00 00 | + × − ÷ | 0–10 | a+b=? | 2 (15) |
| 3 | 1f 1c 1c 14 04 04 04 04 04 04 04 04 04 | + × − ÷ | 0–12 | all three | 3 (20) |

Validation when OK is pressed (first failing test wins) **[high]**:
1. no operation checked → DIALOG_140 "at least one box checked in each area";
2. no number checked → DIALOG_140; 3. no format checked → DIALOG_140;
4. the only operation is × (or the only operation is ÷) **and** the only number is 0 → DIALOG_141
   "choose more numbers to do division or multiplication".

Main game: if the player has never customized, the active record is the tier default (and is
written back to the player's record). If customized, the player's record is used and its Level
field becomes the word-problem level for the next game (the drill count, clue count, puzzle, and
bonus still use the rank tier). Changing word-problem settings mid-game shows DIALOG_20108 and
takes effect next game. **[med]**

### 2.2 The fact deck  **[high]**

Built at the start of every drill set from `cust`:

```
deck = []
for op in 0..3 where cust[op] & 0x10:
    used = 13x13 boolean, all false
    for n in 0..12 where cust[n] & 0x04:          # emphasized numbers, ascending
        for m in 0..12:
            if not used[n][m] and not (op == ÷ and n == 0):
                deck.append((op, lo=n, hi=m)); used[n][m] = true
            if not used[m][n] and not (op == ÷ and m == 0):
                deck.append((op, lo=m, hi=n)); used[m][n] = true
```
So each op contributes every ordered pair (lo, hi) in 0..12 × 0..12 in which at least one element is
emphasized, once; for ÷ pairs with lo = 0 are excluded. (Max 4 × 169 = 676 entries.) Deck sizes for
the defaults: T0 240, T1 306, T2 647, T3 663.

Drawing (sampling without replacement): `remaining` is reset to the deck size at the start of every
set. To draw: `if remaining == 0: remaining = len(deck)`; `j = scaled(remaining)`; swap `deck[j]` with
`deck[remaining-1]`; `remaining -= 1`; the drawn fact is `deck[remaining]`. (The deck array is
permuted in place; it is rebuilt — in canonical order — at the next set.)

### 2.3 Turning a fact into a question  **[high]**

Format choice (per question, RNG-A): repeat `k = scaled(3)` until format k is enabled.

The question is displayed as three fields `A op B = C` (op glyph from "+*-/" → shown as + × − ÷),
with one field replaced by "?" according to the format:

| op | A | B | C |
|---|---|---|---|
| + | hi | lo | hi + lo |
| × | hi | lo | hi × lo |
| − | lo + hi | lo | hi |
| ÷ | lo × hi | lo (never 0) | hi |

Format 0 hides C, format 1 hides A, format 2 hides B. The expected answer is the hidden value.
Special case: ÷ with format 2 and A = 0 (i.e. `0 ÷ ? = 0`) is replaced by `lo ÷ ? = 1` (answer lo).
Ambiguity rule when checking: for × only, if the format is 1 and B = 0 (`? × 0 = 0`) or the format
is 2 and A = 0 (`0 × ? = 0`), **any** answer is accepted.

So the emphasized numbers constrain: either addend / factor (+, ×); the subtrahend or the difference
(−); the divisor or the quotient (÷). All operands of the underlying fact are 0..12.

### 2.4 Answer entry  **[high unless noted]**

* Answer box starts showing "?"; up to **3** characters; only digits are accepted (others beep).
* Keys (codes as delivered by the game's key reader): Enter (0x0D) and 0x03 submit; Backspace (0x08),
  0x2E and 0x153 delete the last character; 0x63 ('c') clears the whole entry **[med for 0x2E/0x153/'c']**.
* Submitting an empty entry beeps and is ignored.
* Esc (or clicking the on-screen exit hot spot) in the **main game** abandons the rest of the set
  (the code-piece test in §2.5 is still applied to the answers given so far). In **DfS**, Esc pauses
  and offers the Exit Program confirmation instead.
* The typed digits are converted with `atoi` and compared to the expected answer.

### 2.5 Main-game drill (zapping a lit Telly)  **[high]**

* Number of questions `N = 2·T + 4` (4, 6, 8, 10). Needed to win a code piece: `N − 1` correct.
* The game clock keeps running while answering: while waiting for a key, every 240 centiseconds of
  real time advances the clock by one game minute (the hall rate is one minute per 328 cs, so the clock runs faster during drills) **[high]**. If midnight is reached the game is lost (§5.4).
* Per question:
  * Correct → show one of five praise strings chosen by `scaled(5)`: "That's correct!",
    "That's the answer!", "That's right!", "You're right!", "Good job!"; **+50 points**.
  * Wrong → "That's incorrect." for ~2.0 s, then the correct answer is written into the "?" slot in
    the highlight colour for ~3.0 s. No penalty.
* After the set: if correct ≥ N−1 → the next undiscovered secret-code slot (left to right) is
  revealed, its picture shown, **+100 points**, DIALOG_1003 hint (if hints on). Otherwise the text
  "No Code.   Catch me again!" is shown (~2 s).
* A Telly is only lit (and so only gives drills) while fewer than 4 code pieces have been collected;
  chance per Telly appearance per tier: 100 %, 70 %, 60 %, 50 % (field 4 above).

### 2.6 Drill for Skill session  **[high unless noted]**

* Uses the player's DfS customization record; set size `N = 5 · (cust[0] & 3) + 5` (5/10/15/20).
* Sets repeat indefinitely until the player leaves via the menu (Return to Sign In / Exit).
* Every question: same generation and feedback as §2.5, but **no points** and no code pieces.
  Two on-screen counters (each wraps from 9999 to 0): *questions asked* (incremented as each
  question is posed) and *correct answers*.
* An elapsed-time display (minutes 0..999 : seconds 00..59) runs while a question awaits input.
  **Quirk [med]:** inside the key-wait loop the "second" ticks every 74 cs, elsewhere every
  101 cs, so the displayed time runs ~35 % fast.
* After each set: if every answer was correct → "perfect set" celebration animation + sound;
  otherwise one of two "try again" animations chosen by `scaled(2)`.
* DfS has no clock, no Telly code, and does not touch the lifetime score.

---

## 3. The deduction puzzle

### 3.1 Data

* **Pictures:** 20 icons, ids 0..19. They are cels `78 + id` (0x4E..0x61) of ASEQ 200 in SSO1.DAT
  (the general sprite set; cels 0x4A–0x4D in the same set are the MIDNIGHT/PAUSE/clock tiles).
  Identification: 0 camcorder, 1 coffee mug, 2 radio tower,
  3 clipboard, 4 telephone handset, 5 lightning shield, 6 plug & cord, 7 desk microphone, 8 film reel,
  9 book, 10 movie camera, 11 zig-zag signal, 12 satellite dish, 13 TV set, 14 headphones,
  15 monitor/projector, 16 clock, 17 "ON AIR" sign, 18 director's chair, 19 tape reel.
* **Secret code:** 4 slots, each a picture id + "not yet captured" flag.
* **Room clues:** 5 rooms × 4 slots, same encoding. Room order everywhere: 0 NEWS ROOM, 1 GAME SHOW
  ROOM, 2 CARTOON ROOM, 3 EQUIPMENT ROOM, 4 SOUND ROOM.
* **Match rule index** (0, 1, 2), **hideout room** (0..4), **code pieces captured** (0..4).
* Persistent work arrays (live for the whole program run, not saved, not reset between games):
  `pool[20]` (initially 0..19), `patterns[10]` (initially the table below), candidate rows `cand[5][4]`.
* The **secret code** and the room clues live in the game-state block (chapter 03 §4.4). That block is
  zeroed when the sign-in screen is entered, but **not** between consecutive games of the same player
  ("play again" after a win, loss or midnight). It is also restored from a saved game on resume. So a
  code slot that the generator leaves unwritten (see §3.4) keeps the previous game's value from the
  same session, or 0 after a fresh sign-in. The "not captured" flag (bit 7) is ORed into every code
  and candidate slot each game (step 3); the picture id is always `value & 0x7F`.

Patterns table, index: value → AABB, AABC, ABAB, ABAC, ABBA, ABBC, ABCA, ABCB, ABCC, ABCD
as `[0,0,1,1] [0,0,1,2] [0,1,0,1] [0,1,0,2] [0,1,1,0] [0,1,1,2] [0,1,2,0] [0,1,2,1] [0,1,2,2] [0,1,2,3]`.
Permutation table: the 24 permutations of (0,1,2,3) in lexicographic order, `PERM[0..23]`.

### 3.2 Generation (new game, after the time re-seed; all draws RNG-A)  **[high]**

```
# step 1: shuffle the persistent picture pool
for n = 20 down to 1:  j = scaled(n); swap pool[j], pool[n-1]

# step 2: tier-specific candidates; cand[0] will be the hideout
T0 (rule 0):
    code = pool[16..19]
    cand[0..4] = copies of code
    for (c, changes) in (1,1), (2,2), (3,2), (4,3):
        pos = [0,1,2,3]; n = 4
        repeat changes times:
            j = scaled(n); swap pos[j], pos[n-1]
            k = scaled(16); cand[c][pos[n-1]] = pool[k]      # a picture NOT in the code
            n -= 1
    # => decoys differ from the code in exactly 1,2,2,3 distinct positions (replacement pictures
    #    may repeat among themselves)
T1 (rule 0):
    pics = pool[16..19]
    repeat d = scaled(24) until 2 <= d <= 21
    code[i]    = pics[PERM[d][i]]
    cand[0]    = code
    cand[1..4][i] = pics[PERM[d-2][i]], pics[PERM[d-1][i]], pics[PERM[d+1][i]], pics[PERM[d+2][i]]
    # => every room has the same 4 pictures; only the hideout has them in the code's order
T2 (rule 1):
    code    = pool[10..13]
    cand[0] = pool[10..13]           cand[1] = pool[9..12]
    cand[2] = pool[11..14]           cand[3] = pool[12..15]
    cand[4] = [pool[10], pool[9], pool[14], pool[13]]
    for each c in 0..4: full Fisher-Yates of cand[c]  (n=4..1: j=scaled(n); swap cand[c][j], cand[c][n-1])
    # => only the hideout holds exactly the code's 4 pictures (decoys share at most 3)
T3 (rule 2):
    k = scaled(9); pat = patterns[k]; swap patterns[k] <-> patterns[9]     (persistent!)
    picks = draw3()
    for v in 0..2, for i in 0..3: if pat[i] == v: code[i] = picks[v]
    picks = draw3()
    for v in 0..2, for i in 0..3: if pat[i] == v: cand[0][i] = picks[v]
    for c in 1..4:
        k = scaled(8); p = patterns[k]; picks = draw3()
        for v in 0..2, for i in 0..3: if p[i] == v: cand[c][i] = picks[v]
  draw3():   n = 20; repeat 3 times: j = scaled(n); swap pool[j], pool[n-1]; out.append(pool[n-1]); n -= 1
    # => the hideout has the code's repeat-pattern with freshly drawn pictures; decoys get patterns
    #    from slots 0..7, which never hold the code's pattern (it sits in slot 9)

# step 3: all code and clue slots get the "not captured" flag
# step 4: deal candidates to rooms
order = [0,1,2,3,4]; n = 5; repeat 5 times: j = scaled(n); swap order[j], order[n-1]; n -= 1
room[r] = cand[order[r]]  for r = 0..4;   hideout = the r with order[r] == 0
codePiecesCaptured = 0; matchRule = 0,0,1,2 for T0..T3
```

### 3.3 Match Rules (decoder "Match Rule" button → DIALOG_25000 + rule)  **[high]**

| Rule | Text | What the generator actually guarantees |
|---|---|---|
| 0 | "The hideout room has the same pictures and the same pattern as the secret code." | Hideout clues == code, slot by slot. Unique. |
| 1 | "…same pictures as the secret code, but a different pattern." | Hideout holds exactly the code's 4 pictures (as a multiset); other rooms do not. **Quirk:** the order is a random shuffle, so 1 game in 24 the hideout is in the *same* order. |
| 2 | "…different pictures from the secret code, but the same pattern." | Hideout has the same repeat-pattern (shape) as the code. **Quirks:** pictures are drawn independently, so ~40 % of the time the hideout shares ≥1 picture with the code; see 3.4 for rare ambiguity. |

The game never evaluates the rule itself: the decision is right iff the chosen room == hideout.
The "pattern" in rules 1/2 means the arrangement of repeated pictures (AABB, ABAC, …).

### 3.4 Known generator defects (reproduce or fix — implementer's choice)  **[high]**

* T3: after the first All-Star game of a program run, the ABCD pattern (4 distinct values) can be
  swapped into slots 0..8. If chosen for the code or a decoy, the slot whose pattern value is 3 is
  never written and keeps whatever picture was there before: for the code, the previous game's
  code in this sign-in session (0 after sign-in); for a candidate row, its value from any earlier game
  in this program run.
  In simulation ≈3 % of All-Star games then have a decoy with the same shape as the code (two
  "matching" rooms) or a code whose shape differs from the hideout's. Because the swap happens
  before decoys are generated, this can occur even in the first All-Star game.
* T2: see rule-1 quirk above (≈4.2 %).
* `pool` and `patterns` are permuted persistently across games in one run, so generation depends on
  history as well as on the RNG state.

### 3.5 Collecting clues and code pieces  **[high]**

* **Code pieces** are revealed strictly left-to-right (first uncaptured slot) when a drill set is won.
* **Clues:** when a math challenge in room r (the room the player is in) ends (solved *or* failed after 3 wrong tries), the next
  `CLUES[T]` uncaptured clue slots of room r are revealed left-to-right (4 at T0/T1, 2 at T2/T3).
  (A failed challenge still gives clues but no points/energy — DIALOG_1008.)
* Clues and code pieces are captured independently of whether the room is the hideout.

### 3.6 Which objects hold math challenges  **[med]**

There are 10 challenge objects in 5 pairs; pair p = objects (2p, 2p+1). The pairs sit in rooms
(see chapter 04 §3.4): pair 0 NEWS (Map, Bar Graph), pair 1 GAME SHOW (Prize board, Spinner), pair 2
SOUND (Great 8, Volume), pair 3 EQUIPMENT (Monitor, Equipment list), pair 4 CARTOON (Line Graph, VCR).
Note the pair order is *not* the room order used by the decoder. At new game:
* T0/T1: in each pair exactly one object holds a challenge: object `2p + (rnd_mod(1,100) & 1)`.
* T2/T3: all 10 objects hold challenges (hence "2 math challenges per room" at Expert+).
* Per object, a variant index `scaled(4)` (0..3) is drawn (RNG-A), and an operation is drawn with
  RNG-B: repeat `op = pm_range(0,3)` until op is enabled in `cust`; stored as word-problem op code
  `[0,2,3,1][op]` (word-problem op numbering 0 add, 1 divide, 2 multiply, 3 subtract — matches the
  BetaProblem menu order). Details of how these select an MTHT problem: see chapter 06.
* Object state: 0 none, 1 has challenge, 2 solved, 3 failed. Each object tracks wrong tries (0..3).

### 3.7 Decoder and "Decide"  **[high]**

Opening the decoder pauses the clock. Rooms are listed in room order; each row shows that room's 4
clue slots (uncaptured slots blank); the code row shows the 4 code slots.

**Decide is allowed** iff any of:
1. all 4 code pieces captured **and** at least one room has all 4 clues; or
2. at least 1 code piece captured **and** at least 4 rooms have at least one clue; or
3. all 5 rooms have all 4 clues **and** zapper energy is 0 **and** no energy is pending
   (anti-soft-lock: lets an out-of-energy player decide without code pieces).

(The help text / DIALOG_25003 / DIALOG_12100 state rules 1 and 2; an unused embedded string has an older
wording "all clues for all rooms and one piece" that the code does not implement.) Otherwise
DIALOG_25003 is shown.

Decide flow: the prompt "Click on the name of the room that you think is the Master of Mischief's
hideout." appears; the player picks a room (mouse, or Up/Down to move the highlight, Enter to choose,
Esc to cancel); a Yes/No confirmation (DIALOG_1017..1021, one per room) follows. On Yes:
* chosen == hideout → **win** (§5.3);
* otherwise → **loss**: identical outcome to running out of time (§5.4).

Decoder keyboard: Tab/Right next button, Left previous button, Up/Down move room highlight,
Enter activate focused button, Esc leave, D = Decide, M = Match Rule, S = leave.
Button hot spots (canvas coordinates): (33,312), (115,312), (198,312), each 66×54; room rows at
x=293, y=43+47·r, 177×41. **[med for button semantics]**

---

## 4. Math-challenge scoring (rules side only)  **[high]**

* Points on success = `base[T] − penalty[T] × wrongTries` → T0/T1: 300 / 200 / 100,
  T2/T3: 600 / 400 / 200 for 1st/2nd/3rd try. A 3rd wrong answer ends the challenge with 0 points,
  shows "That's not correct.  The correct answer is …", and still reveals the clues.
* After the 1st wrong answer a first hint is shown ("Read the question; study the <object>"), after
  the 2nd a second hint ("HINT: …") — see chapter 06 for hint text.
* Success also credits **2 energy units** as *pending* energy, collected at the energy supply machine
  (chapter 04). Energy starts at 12; it has no hard cap, but the HUD bar displays at most 24 units.
* Success praise: one of the same 5 strings, chosen with `pm_range(0,4)` (RNG-B).

---

## 5. Scoring, ranks, winning and losing

### 5.1 Lifetime score and rank  **[high]**

Rank = largest r with `threshold[r] ≤ lifetime` (unsigned compare):

| r | Rank | Threshold |
|---|---|---|
| 0 | Trainee | 0 |
| 1 | Junior | 5,000 |
| 2 | Senior | 30,000 |
| 3 | Pro | 60,000 |
| 4 | Expert | 120,000 |
| 5 | Master | 200,000 |
| 6 | All-Star | 300,000 |
| 7 | Champion | 500,000 |

Lifetime score is capped at 9,999,999. Points earned during a game accumulate in a 16-bit
**game score** (starts at 0) and are added to the lifetime score **only when the game is won**.

### 5.2 Point sources

| Event | Points |
|---|---|
| Correct drill answer (main game) | 50 |
| Code piece captured (drill set won) | 100 |
| Math challenge solved | 300/200/100 (T0–1) or 600/400/200 (T2–3) by try |
| Win: Energy Left | energyUnits × {100,150,300,400}[T] |
| Win: Time Left | minutesLeft × {25,35,50,60}[T] (0 if the Clock option was turned off) |
| Win: Winning Game | {1000,2000,3000,5000}[T] |

`minutesLeft = (11 − clock div 100) × 60 + (60 − clock mod 100)` where the clock is stored as
`hours×100 + minutes` on a 12-hour face (game starts at 900 = 9:00 PM, ends at 1200 = midnight),
e.g. 180 at the start.

### 5.3 Winning sequence  **[high]**

1. Game state saved; win animation.
2. "B O N U S" panel (slides over the room; layout in chapter 07 Part B §3.1): "Energy Left: <energy×rate>" — energy drains one unit at a time while points
   are added; "Time Left: <minutes×rate>" — the clock is run forward to midnight in steps of up to 15
   minutes while points are added; "Winning Game: <bonus>" — added in chunks of {50,100,150,250}[T].
3. Lifetime += game score (cap 9,999,999); if game score > the 10th best entry in `hiscore.dat`
   (top-10 single-game scores: name, score, rank-at-the-time), it replaces that entry and bubbles up.
4. Award animation: below 500,000 lifetime points the player zaps the Master of Mischief; at or above it he is turned into a TV set and beamed away (chapter 07 Part B §3.2).
5. Score summary: rank ladder (8 names with thresholds), "Game" = game score, "Total" = lifetime.
   If the player was Trainee at sign-in and is now Junior, New Player Hints are switched off.
   The saved-game flag is cleared and the user file + hiscore file are written.

### 5.4 Losing  **[high]**

Two ways: the clock reaches midnight (any clock advance that would pass 11:59 snaps to 12:00), or a
wrong Decide.
* Game score is subtracted back to 0 (no points reach the lifetime score; rank unchanged), energy is
  drained, the MIDNIGHT animation plays.
* Midnight during play additionally clears the saved-game flag, writes the user file and shows
  DIALOG_9136 "Out of Time …", then a new game for the same player starts.
* Wrong decision: clock is run to midnight in 10-minute steps, then the same ending screen.

### 5.5 Rank-dependent behaviour summary

| Aspect | Depends on |
|---|---|
| Drill questions per Telly (4/6/8/10), code threshold N−1 | tier |
| Telly lit chance (100/70/60/50 %) | tier |
| Default drill content (ops, numbers, formats) and default word-problem level | tier (unless customized) |
| Objects with challenges (1 or 2 per room), clues per challenge (4 or 2) | tier |
| Match rule (0,0,1,2) and puzzle construction | tier |
| Math challenge points, bonus rates, Winning Game bonus | tier |
| New Player Hints default | on for Trainee, off otherwise (rank at sign-in) |
| Drill for Skill default set size | tier (5/10/15/20) |

### 5.6 Player lists  **[med]**

* **Sign-in / Club Members list** (`ssowincd.dat`): up to 99 players (8-byte header + 100 × 15-byte
  index entries: name[9], lifetime score u32, slot u16). "See Rank…" shows DIALOG_200 with three
  columns Name / Rank / Score for the top 10 players by lifetime score.
* **`hiscore.dat`**: 10 × 15 bytes (name[9], game score u32, rank u16), best single winning games.
  It is kept in memory (empty at program start), updated after wins and written to disk, but never
  read back or displayed.
* DIALOG resource ordinal 1 is named `ALLSTAR_DLG` in the EXE's name table; it is merely the
  "You need a total of 500,000 points to move up in rank from All-Star to Champion!" rank message.

(File formats belong to the shell/chapter 03 §4.)

---

## 6. Timing constants

Short waits use `delay(n)` = busy-wait of `floor(floor(n·4/13)·100/73)` centiseconds (≈ n/237 s)
while pumping messages. Values used in drills: 0x78 → 0.49 s (between questions), 0xF0 → 1.00 s
(before and after the praise/score), 0x1E0 → 2.01 s ("That's incorrect." / "No Code" text),
0x2D0 → 3.02 s (correct answer shown / code piece shown). **[high]**

---

## 7. Suggested rule-state model (for the implementer)

```
GameState {
  gameSeed: u16;  gameScore: u16;  clock: u16 (hhmm, 900..1200);  wordLevel: u8 (0..3)
  energy: u8;  pendingEnergy: u8
  objState[10]: u8 (0 none,1 has challenge,2 solved,3 failed);  objOp[10]; objVariant[10]; objWrong[10]
  mathSeed[10]: u16 (1..10)
  roomClue[5][4]: {pic:0..19, captured:bool};  code[4]: {pic, captured}
  codePieces: u16;  matchRule: u16;  hideout: u16
}
```
(This mirrors the 99-byte state block the original saves; exact byte layout → chapter 03 §4.)

---

## Open questions

1. The Decide check also consults a state value that is never set anywhere (dead); if it
   were set, Decide would also be allowed with energy 0 and nothing pending. Probably dead/debug.
2. Per-tier field 11 (20/30/40/60) has no reader.
3. Drill-entry key codes 0x03, 0x2E, 0x63, 0x153: meaning depends on the key-reader translation
   (shell/chapter 04). 0x63 is assumed to be the 'c' key ("clear").
4. After a *wrong decision* no code path clears the saved-game flag on disk; if the game had earlier
   been saved (Return to Sign In) and resumed, the old save may remain resumable. Not verified.
5. (harmless) The clue-reveal step receives an unused argument; ignore.
6. The game's displayed text for the drill wrong answer is "That's incorrect." — the longer
   "That's not correct.  The correct answer is " string is used only for math challenges.

---
