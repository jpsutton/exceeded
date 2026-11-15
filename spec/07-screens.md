# 07 — Screen layouts and scripted sequences

Part A: HUD, hall/room composition, drill screen, decoder. Part B: intro, hideout/ending, bonus,
award and score screens, midnight. All coordinates are 512×384 canvas pixels.



---------------------------------------------------------------------------------------------------
# Part A

## Screens A — HUD, hall/room composition, drill-question screen, decoder

All coordinates are 512×384 canvas pixels unless marked "design units" (d.u.). The game converts
d.u. with `px_x = round(x·1.6)`, `px_y = round(y·1.92)` (round half up). Cel numbers without a
resource name refer to SSO1 ASEQ 200 (hall sprite set); cel ids ≥ 10000 mean SSO1 ASEQ 210, cel
id − 10000. "Delay n" means the scripted-delay helper (appendix A: `floor(floor(n*4/13)*100/73)` cs, roughly
`n·0.42` cs; e.g. 200 ≈ 83 cs, 480 ≈ 200 cs, 720 ≈ 300 cs).
Confidence: H = read from code/data, M = inferred, L = guess.

## 0. Drawing primitives (what the screen code uses)

| primitive | behaviour |
|---|---|
| stamp cel (cel, x, y, flags) | draws one cel with its top-left at (x, y), straight into the frame (no animation object). Flag 0x20 = mirrored horizontally. A stamped cel always persists: sprites save and restore what is under them, so draw stamps into both the frame and the background page. Flag 0x80 = clip the cel to the playfield (x 9..502); without it the cel is clipped only to the canvas (corrected in v1.13). |
| stamp picture (ASEQ id, x, y) | the same for cel 0 of any ASEQ (room pictures, panel, decoder). |
| fill rect, pixel | inclusive (x1,y1)–(x2,y2), one palette index |
| fill rect, d.u. | the same with all four coordinates converted from d.u. |
| text, pixel | pen (baseline) at (x, y + font ascent − font descent) — **corrected in v1.9**; earlier versions said y + ascent. Where this chapter or chapter 06 calls a text y "the top of the text line", read it as this routine input y (all text coordinates in chapters 06 and 07 use this convention; the low-level glyph renderer of chapter 02 §4.1 takes the baseline). `\r`/`\n` = new line. `^` prints glyph 0xA1. `~XXXYYY` (6 digits) moves the pen to (XXX, YYY). |
| text, d.u. | the same with x and y converted from d.u. |
| recolour rect | inside a rectangle, every pixel of colour A becomes colour B (used for the decoder highlight) |

Fonts (handles set at start-up): **F0** = NFNT 28625 (small bold sans, default); **FL** = NFNT 28568
(large serif, 24 px); **FS** = NFNT 28558 (serif, 16 px; two handles share it). Palette indices that are
used: 0 black, 4 (message-area blue), 88 (7,7,7) "screen black", 137, 211 (139,95,39) brown,
249 red, 251 yellow, 255 white.

## 1. In-game HUD on panel frame 1000 (H)

Rebuilding the full screen (done whenever a screen is entered or restored):
1. Fill the whole canvas with colour 0.
2. Stamp ASEQ 1000 at (0,0).
3. Redraw the clock, energy bar and score (below), then redraw any registered panel buttons.

Panel geography (from the picture): black viewport ≈ x 8–503, y 35–283; message box ≈ x 15–326,
y 297–378; status box at x ≈ 329–497, y 297–377 with the printed labels Time/Energy/Score. Sprites in
the hall and in rooms are clipped to x 9–502.

### 1.1 Clock
| element | cel | position |
|---|---|---|
| time box ": P.M." (also erases the old digits) | 77 | (402, 303) |
| hour tens (only drawn if the hour ≥ 10) | 64+d | (407, 308) |
| hour units | 64+d | (418, 308) |
| minute tens | 64+d | (434, 308) |
| minute units | 64+d | (445, 308) |
| PAUSE (first pause, Clock option off, decoder open) | 76 | (402, 303) |
| MIDNIGHT / red MIDNIGHT | 74 / 75 | (402, 304) |

Digits are 8×13 cels (64 = '0'). The colon and "P.M." are part of cel 77. At the start of each redraw
the box cel is restamped, then the 3 or 4 digits. When the pause count drops back to zero the normal
clock is redrawn over PAUSE. Midnight sequence: the cel alternates 74, 75, 74 … 10 times (the
first one is 74), with delay 200 (≈83 cs) and a looping sound, and it ends on 75. If the clock is drawn
at ≥ 12:00 outside the action game, cel 75 is stamped at (402,304).

### 1.2 Energy bar
* Erase: fill (403,327)–(480,345) with colour 0.
* If energy > 0: fill (403,327)–(402 + 3·min(energy,24), 345) with **colour 249 (pure red)**.
  The bar is 19 px tall and at most 72 px wide (x 403–474).

### 1.3 Score
Always 6 digits with leading zeros (the score is a 16-bit unsigned word, so 0–065535), using digit cels
64–73 at y = 353. The units digit is at x = 467, and each higher digit is 12 px further left:
467, 455, 443, 431, 419, 407. A non-zero change plays SND 1747. No font is used.

### 1.4 Message area and panel button
* While the action game runs, a **Decoder button** (SSO1 ASEQ 1007: cel 0 normal, cel 1 pressed) sits
  at (33, 312) in the message box. Pressing it shows cel 1 until the mouse is released, then opens the
  decoder. It is removed while the decoder is open and at midnight, and created again afterwards
  (it is created at game start and again on each room draw; a guard stops duplicates). (H/M)
* Text prompts use the message box. The generic prompt routine fills (19,300)–(294,374) with colour 255
  (white), draws a frame, and prints word-wrapped text in FS, colour 0 (black), starting at x = 29 with
  the right margin at x ≈ 289 and the first line at y ≈ 313. (M)
* The drill code clears the box with colour 4 over (17,300)–(326,374).
* "‹key› to Explore TV Station" is printed in FS, white, at d.u. (21,184) → px (34,353).
* No separate room-name text was found: rooms are named only by the door plates in the hall
  (cels 117–121) and by the labels printed on the decoder picture. (M)

## 2. Hall composition (H)

The hall picture is built in the background buffer in this order:
1. **Ceiling band**: fill (9,35), 494×33 (y 35–67), colour 211.
2. **Wall/floor slices**: 38 cels 137–174, each 13×223. Screen column c (1..38) gets cel
   `137 + (worldColumn mod 38)` at x = 9 + 13·(c−1), y = 58 (so y 58–280). The slices hold the wall,
   the columns and the floor, so there is no separate floor. Every area uses the same 38-slice
   pattern. A full redraw always uses slices 137..174 in order (see 4).
3. **Hot spots** of the current area, slots 0→3, at x = 9 + 13·col (col = slot column) and
   y = the slot's y field (in pixels):
   * type 1 (room door): door cel **134** (125×130) at y = 101, then the slot's name-plate cel
     (117 NEWS, 118 GAME SHOW, 119 CARTOON, 120 EQUIPMENT, 121 SOUND; 120×28) at y = 76.
   * type 6 (floor-end wall): cel 123 at y = 54, **mirrored** in areas 0 and 5 (left ends) and
     not mirrored in areas 4 and 9.
   * every other type (plain-door props 125–133, elevator 122 at y 68, energy machine 195 at y 110):
     the slot cel at the slot y.
   Hot spots are not drawn in rooms (their cel 203 is an invisible marker).
4. A full redraw sets the scroll to `area·38` (an area boundary), draws as above, and then, if the real
   scroll was different, scrolls programmatically by the difference (5). The final picture is
   therefore always correct.
5. **Edge strips**: cel 58 (7×252) at (6,34) and cel 59 (7×251) at (499,34). They are redrawn after the
   full draw and after every scroll step, and they hide the ragged edges of the viewport.

Scrolling by n columns (n·13 px):
* Copy the band of rows **y 68–281**, x 9–503, sideways by 13·n px. Rows 35–67 are never moved,
  because the ceiling band is uniform.
* Draw the n slices that are now exposed at the edge (world column = the new scroll position).
* View moving left: check the slots of area `scroll div 38`. View moving right: check the slots of area
  `scroll div 38 + 1`. Every hot-spot cel whose pixel span overlaps the exposed strip is redrawn at its
  position relative to the scroll, with the same cel/y rules, plus door cel 134 for type 1. When moving
  left, walls are always drawn mirrored; when moving right, never.
* Then redraw the edge strips. Sprites are moved by the gameplay code.


## 3. Room screen (H)

Rebuild the panel (§1), then stamp the room picture **SSO1 ASEQ 1001 + (area − 10)** at **(10,36)**:
NEWS 1001, GAME SHOW 1003, CARTOON 1004, EQUIPMENT 1005, SOUND 1006 (492×247 → x 10–501,
y 36–282). Then stamp **ASEQ 1022** (74×115, the hall seen through the open exit door) at
**(80,92)**. The Decoder button is created. Room objects are part of the picture, so no slot cels are drawn.

## 4. Drill-question screen ("Telly's belly") (H unless marked)

Setup:
1. Rebuild the panel (§1).
2. Stamp SSO2 **ASEQ 8600 at (13,36)** into both the frame and the background.
3. Drill for Skill only: stamp SSO2 **ASEQ 8605 at (326,297)**, which covers the Time/Energy/Score box
   with a Time/Problems/Correct box. Stamp digit '0' (cel 64) at (465,331) and (465,354).
4. Load SSO2 ASEQ 8604 as the current extra sprite set.
5. Telly drill only: load ASEQ 6002 and draw a **Station** button (6002 cel 7) at (33,312). Clicking it
   aborts the drill.
6. Play the TV-static animation (below).

SSO1 ASEQ 5500 (512×283 Telly close-up) was not found in the drill code. 8600 is the picture in use.
ASEQ 205 looks like an unused orange low-colour copy of it.

**Drawing model.** Unless stated otherwise every drawing below goes into the **frame only** (not
the background buffer) and is presented right away. Fills are inclusive pixel rectangles. "d.u.
fill (x1,y1)–(x2,y2)" means: compute w = x2−x1+1 and h = y2−y1+1 in d.u., then fill the pixel rectangle
with top-left (sx(x1), sy(y1)) and size sx(w) × sy(h), where sx(v) = round(1.6·v) and sy(v) = round(1.92·v),
round half up. (The far edge is therefore *not* sx(x2)/sy(y2).) Colour 88 is (7,7,7), the colour of the
TV interior in picture 8600 and in 8604 cel 12. Colour 0 is never used on the TV.

**8604 cel placement** (H): cel c is drawn with its top-left at (328,210) + offset(c): c0 (0,0); c1 (−1,−6);
c2 (−73,−82); c3 (−68,−83); c4 (−75,−86); c5 (−74,−79); c6 (−77,−87); c7 (−72,−85); c8 (−84,−95);
c9 (−168,−138); c10–12 (−197,−148). Cel drawings are plain stamps into the frame (no save/restore, no
erase of what was there before) unless an erase is listed.

**TV static** (H): 8 frames; frame i (i = 0..7) shows cel 10 if i is even and cel 11 if i is odd at (131,62),
each frame starting SFX 1752 (priority 10) again and lasting 8 cs; then cel 12 (the black TV) at (131,62).
No erase between frames. Cel 10 is 267 px wide and cels 11/12 are 266, so column x = 397 keeps cel 10's
pixels afterwards. The static is played at drill start, in Drill for Skill after each set (Part D), and
before "No Code" when a Telly drill fails. **It is not played between problems.**

**Button press** (H) — the claw on the red button:
| step | what is shown |
|---|---|
| 1 | Start a 16 cs timer. |
| 2 | Draw cel 0 (95×66, claw pressed down) at **(328,210)** → x 328–422, y 210–275, and present at once. |
| 3 | Start SFX 1705 (priority 2). |
| 4 | When the 16 cs have passed (counted from step 1), draw cel 1 (96×72, claw resting) at **(327,204)** → x 327–422, y 204–275, and present. |
Nothing is erased: cel 1 covers cel 0 completely, and their few transparent pixels coincide. Cel 1 looks
the same as the claw in picture 8600, so between presses the claw rests in that pose. The pressed pose
must really be presented for those 16 cs (do not wait without presenting). Render references:
out/screensA/drill_press_strip.png (dirty side).

**Problem loop** (both drills; a Drill-for-Skill set is one run of this loop):
1. Once, before the first problem: wait 49 cs.
2. For each problem: button press (above). Drill for Skill: Problems += 1 and redraw it. Wait 49 cs. Pose
   the question and read the answer (below). Correct or wrong handling (below). Clear the question area.
   Go straight on to the next problem's button press: **there is no wait between that clear and the next
   press.** The clear after a wrong answer is not presented on its own, so it shows up in the same update
   as the next press's cel 0.

**Question-area clear**: fill pixels x 145–364, y 80–159 with colour 88. This lies entirely inside the TV
interior, so all you see is the text disappearing. It covers the question line (font FL drawn at d.u. y 75
→ routine input y 144 → baseline 158 → ink rows 140–157) and the praise text. Everything that reaches
below y 159 (cursor, wrong-answer message, answer-field boxes) has its own erase, listed below.

**Question line** (font FL, colour 255): five parts `A op B = C`, op one of "+*-/". The layout is computed
in d.u. but **the text widths are the font's pixel widths, used as d.u. numbers without conversion** (so
the line is spread out by 1.6 horizontally). sp = pixel advance of a space, wX = pixel width of part X:
`W = wA + wOp + wB + wC + 4·sp + 7`, `left = 164 − trunc(W/2)`;
x(A) = left; x(op) = left+wA+sp; x(B) = left+wA+wOp+2sp; x("=") = left+wA+wOp+wB+3sp;
x(C) = left+wA+wOp+wB+4sp+7. Every part is drawn with the d.u. text routine at (x, 75), i.e. at pixel x =
sx(x). The unknown part (C, A or B, by format) shows the **3-character answer buffer**. Its width in the
layout is the width of that buffer string: "?" + two spaces at first; the typed digits padded with spaces
to 3 characters while typing; the correct answer during the red redraw. The layout is recomputed at every
redraw.
Example "1 + 11 = ?": d.u. x 107, 127, 148, 181, 195 → px 171, 203, 237, 290, 312; ink spans x 173–322.
**Redraw** = question-area clear, then the five parts. It happens when the question is posed and again after
every key the input loop handles.

**Answer input**:
* Field left fx = d.u. x of the unknown part; fy = 75.
* **Cursor**: horizontal line 1 px tall at pixel y = sy(fy + 9) = **161**. It starts at d.u. x = fx when the
  field is empty, otherwise at fx + floor(w·625/1000) + 1, where w = pixel width of the typed digits. Its
  length is sx(floor(width("?")·625/1000)) = **11 px** for FL. Drawn in 255 as soon as input waiting starts,
  then switched every 50 cs between 88 and 255. When a key arrives while it is in 255, it is redrawn in 88
  (erased) before the key is handled.
* Digit (at most 3): store it, redraw.
* Backspace (Backspace, '.', or the Delete key): d.u. fill colour 88 (fx, fy−4)–(fx + sp, fy+30) → pixel y 136–202, width
  sx(sp+1). Note that this box starts at the field's left edge. Then drop the last digit ("?" comes back
  if the field is empty) and redraw.
* 'c' (clear): the same box once per typed digit, the field becomes "?", redraw.
* Enter with at least one digit: answer. Enter with an empty field, and any rejected key: the rejected-key sound, nothing else.

**Correct** (font **FS**, not FL): pick one of "That's correct!", "That's the answer!", "That's right!",
"You're right!", "Good job!" at random. SFX 1744. Draw it in white with the pixel text routine at
x = 256 − trunc(w/2), y = 107 (w = its pixel width in FS; baseline 115, ink rows 104–117). Wait 100 cs.
Telly drill: +50 points; Drill for Skill: Correct += 1 (both with SFX 1747). Wait 100 cs. Question-area
clear. Then fill x (256−trunc(w/2)) … (256−trunc(w/2)+w), y 107–115 with colour 88 and present (this box is
already covered by the clear). Then the loop's clear runs again.

**Wrong**: SFX 1761. Draw "That's incorrect." in FS, white, pixel text at (200,190) (baseline 198, ink 187–197).
Wait 201 cs. Fill x 200 … 200+w, y 187–198 with colour 88 (w = width of the message in FS = 107 → x 200–307)
and present. Then, **without presenting in between**:
1. d.u. fill colour 88 (fx, 72)–(fx + w("000") + 2, 105), w("000") = pixel width in FL (39) → pixel
   x sx(fx) … sx(fx)+sx(w("000")+3)−1 (67 px), y **138–202**. For wide lines this box reaches past the TV
   glass onto the bezel at its lower right (x ≥ 372) and leaves a dark patch there. That is faithful.
2. Redraw the line (with its clear). The unknown part shows the correct answer in colour 249, the other
   parts in 255.
Present, wait 302 cs, then the loop's question-area clear (not presented on its own, see the loop).

**Result** (Telly drill, after the last problem):
* Success (correct ≥ required): question-area clear (only that; the message box is not touched, and no
  other rectangle is filled), then the code-piece reveal: for cels 3, 4, 5, 6, 7, 8, 9 in turn: start a
  20 cs timer; from the second frame on, restore the previous cel's rectangle (its position and size per
  the placement table) from the background buffer; draw the cel; present; start SFX 1709 (priority 2);
  wait for the timer. Then stop 1709 and play SFX 1710. Cel 9 (196×145 at (160,72)) stays. Then stamp the
  won symbol cel 78 + n (41×29) at (242,125), +100 points, wait 302 cs.
  Rectangles of cels 3–8: (260,127) 10×20, (253,124) 24×29, (254,131) 21×16, (251,123) 29×34,
  (256,125) 18×32, (244,115) 42×50.
* Failure: TV static, SFX 1711, "No Code.   Catch me again!" in FS (white) with the d.u. text routine at
  (102,70) (pixel x 163, input y 134), wait 201 cs.

**Nothing on the drill screen fills below pixel y 202.** No rectangle touches rows 203–283 at any time:
the claw, grille and button area change only through the button-press cels.
  * During the Telly drill the HUD clock keeps running (every 240 cs) in the normal HUD box.
* **Drill for Skill differences**: 8605 box instead of the HUD box; no Station button, no code piece.
  "‹Enter-key glyph 0x8B›" at px (200,202) followed by "to Continue" in FS (white), at x = 224 + glyph
  width. Counters (digit cels 64–73, right-aligned, no leading zeros):
  | counter | digits (units first) x | y |
  |---|---|---|
  | Time, minutes (0–999) | 433, 419, 405 | 308 |
  | Time, seconds (00–59, always 2 digits) | tens 451, units 465 | 308 |
  | Time colon | two 2×2 dots, colour 137, at (445,311) and (445,316) | |
  | Problems (0–9999) | 465, 451, 437, 423 | 331 |
  | Correct (0–9999) | 465, 451, 437, 423 | 354 |
  The Time counter advances one "second" each time its deadline passes. The code re-arms the
  deadline with +74 cs in the waiting loop and +101 cs while typing (M: probably meant to be 1 s).
  When minutes wrap past 999 the minute digits are cleared (fill colour 0, (405,308)–(433,308)).


## 5. Decoder screen (H unless marked)

Opening:
1. Pause the clock (PAUSE cel 76 in the clock box) and rebuild the panel (§1).
2. Stamp SSO2 **ASEQ 6000 cel 0 at (11,35)** (489×250 → x 11–499, y 35–284). Cel 1 is a variant
   that differs only in 512 px around (326–436, 91–99), the GAME SHOW ROOM label (unused?, L).
3. Load ASEQ 6002 (buttons). Three buttons, each 66×54:
   | slot | x, y | cels (even = dark/inset, odd = bright) | function |
   |---|---|---|---|
   | 0 | (33,312) | 6 / 7 Station | leave the decoder |
   | 1 | (115,312) | 2 / 3 Match Rule | shows the rank-dependent Match-Rule help dialog |
   | 2 | (198,312) | 4 / 5 Decide | Decide |
   The button with keyboard focus is drawn with its **even (dark) cel** and the others with the odd
   (bright) cel. On opening the focus is on Station (cels 6, 3, 5). Left/Right move the focus
   cyclically and Enter activates the focused button. Clicking also works. The SSO1 1007–1010
   buttons are not used here.
4. **Code pieces** (padlock): 4 slots at **x = 58 + 43·i, y = 198** (i = 0..3). For each collected piece:
   SND 1712, a "slot machine" spin through cels 78…97 (delay 5 each), cel 97, delay 60, cel 97,
   delay 80, then the final cel **78 + symbol**. Empty slots stay as drawn in the picture.
5. **Clues**: 5 room rows. Row r (0 NEWS, 1 GAME SHOW, 2 CARTOON, 3 EQUIPMENT, 4 SOUND) has
   4 clue slots at **x = 298 + 43·k, y = 55 + 47·r** (rows y 55, 102, 149, 196, 243). The room labels
   are part of the 6000 picture. For a collected clue the animation runs 20 steps: in step s every
   collected slot in the row shows cel `78 + ((pic + s) mod 20)`, with delay 5 per step. Then delay
   30, then the final cel **78 + pic** (pic 0–19; the 5×4 picture set is cels 78–97, 41×29). A row is
   animated, with SND 1712, only if its first clue slot is collected (M).
6. **Room highlight**: rectangle (293, Y_r)–(470, Y_r+41) with Y = 45, 90, 137, 184, 231. Highlight on
   recolours every colour-88 pixel in that rectangle to **249 (red)**, so the row outline and label
   turn red. Highlight off does the reverse. (Quirk: colour-88 pixels inside clue cels in that row
   change too.) Initially NO row is highlighted (Part C). Up/Down move the highlight cyclically over the 5 rows.
7. **Match Rule**: opens DIALOG 25000 + matchRule with SPEECH 1800 + matchRule (chapter 03). Then all three buttons are redrawn.
8. **Decide**: if the decide condition fails, show dialog 25003 ("You must collect all four pieces…").
   Otherwise show the prompt "Click on the name of the room that you think is the Master of
   Mischief's hideout." in the white message box (§1.4) and register a **Cancel** button
   (SSO2 ASEQ 8551) at **(212,340)**. The player then clicks a room row (or uses Up/Down + Enter).
   The highlight follows the choice, then the ending sequence starts. Cancel puts the decoder
   buttons back.
9. **Station / Esc**: stop the decoder sounds, remove the click handlers, restore the previous screen
   (the caller rebuilds the hall/room with a full redraw), and resume the clock.


## 6. Open questions
1. Question-area clear rectangle (145,80,220×80 px) does not cover the text line at px y 144–168;
   maybe the parts are cleared by the field boxes instead. Check against the real game.
2. Exact argument meaning of the message-box text routine (x 29 / right margin 289 / y 313).
3. Which handle is the key-cap font for the 0x8B glyph ("Enter" icon): both FS handles point to
   NFNT 28558 at start-up. One of them may be switched to NFNT 15001 elsewhere.
4. Use of SSO1 ASEQ 5500 and of 6000 cel 1 was not found.
5. The DFS seconds tick (74/101 cs) looks like a bug or a tuning value.

---------------------------------------------------------------------------------------------------
# Part B

## Scripted screens B — intro, door/hideout, bonus/score, midnight

All coordinates are 512×384 canvas pixels. "AO" = animated object (ASEQ instance, see chapter 02).
"fps n" = the AO's frame period is set to 1/n s (the engine stores period = ticksPerSecond / n).
z = draw order (higher on top). Confidence: **[high]** read directly from code; **[med]** inferred.

---

## 1. Intro / title / attract loop

### 1.1 Overall flow  [high]
```
preload SND 1727 (beam/zap SFX used by ASEQ 1700 scripts)       -- loaded once
load ASEQ 1700 (SSO1) as the "title AO"
loop:
    paint title card = ASEQ 1700 cel 0 (512×384) at (0,0) into the back buffer; present
    whole screen = one hot region (0,0)-(511,383); a click counts as Enter
    play intro.mid (TEXT 1724) looped
    wait up to 30 s for a key/click
        Esc  -> quit program immediately
        Space -> skip := true
        any other key -> remember "key pressed"
    stop music
    if not skip: play the HIDEOUT NARRATION (§1.2)
    if not skip:
        restart intro.mid; repaint title card cel 0; present
        ATTRACT PART A: ASEQ 1700 sequence 0 at fps 5 on the title card; wait for it to end
            (any key aborts the wait)
        if not aborted:
            paint ASEQ 1700 cel 27 ("Shady Glen Television Station" exterior) at (0,0)
            ATTRACT PART B: ASEQ 1700 sequence 1 (fps 5), wait for it to end (key aborts)
    if skip or a key was pressed during the title wait or the narration: leave loop
free 1724 / 1727; go to sign-in
```
So: **timeout with no key → narration + attract → back to title (attract loop)**; **any key → narration
+ attract once → sign-in**; **Space → straight to sign-in**. During the attract part Enter or Space
stops SND 1727 and sets skip (→ sign-in); key handling during the narration is in §1.2; Esc quits.

### 1.2 Hideout narration (the "story" intro)  [high for structure, med for art meaning]
Scene: the MoM's hideout room, SSO3 ASEQ 15000 cel 0 (514×386) painted at (0,0) into the background
(then the 15000 AO is discarded). SSO3 WAVEs 11000 and 11001 are preloaded at start and freed at end.

Actors (all SSO3):

| actor | ASEQ | fps | position (x,y) | z | notes |
|---|---|---|---|---|---|
| MoM body poses B0..B4 | 12001..12005 | 8 | (189, 209) | 20 | only one is shown at a time ("current body") |
| MoM talking heads H0..H10 | 11001..11011 | 9 (later 8, see callback) | (body.x−7, body.y−56) = (182,153) | 25 | only one shown at a time ("current head") |
| Telly-robot (holding disc) | 10001 | 8 | (420, 240) | 20 | seq 0 = idle; seq 1 = throws the disc |
| Telly's antenna head w/ sparks | 12008 | 10 (data) | (440, 240) | 25 | loops; its CALLBACK opcode drives a hook (below) |
| flying disc → splat | 12007 | 8 | (299, 261) | 30 | used once |
| MoM full body, startled | 12006 | 10 | (182, 153) | 25 | used once |

**Head follows body every frame:** head position = body position + (dx,dy) of the body's current
frame entry + (−7, −56), z 25. (The body poses bob/shift slightly; the head moves with them.)

**12008 callback** (fires whenever 12008's script executes CALLBACK, i.e. each loop): toggles the
current head's fps between 9 and 8, and re-positions 10001 at (12008.x + 20, 12008.y), or at
(12008.x + 19, 12008.y + 3) when 12008's current frame has a non-zero dy (in data all dy are 0, so
10001 effectively sits at (460, 240)) **[med — see Open questions]**.

Initial state: current head = H0 (not yet talking), current body = B0, both started; 10001 started
(seq 0); 12008 started.

**Stage machine.** stage s = 0..10, a "line finished" flag F, a sub-step index k, current speech id.
Each frame:
1. Body: whenever the current body AO has stopped (its script hit PAUSE/END), k += 1; if k < 4 and
   s < 11, look up `BODY[s][k]` and (re)start that body pose (0 = no change):

   | s | BODY[s][0..3] (1=12001 … 5=12005) |
   |---|---|
   | 0 | 0 0 0 0 |
   | 1 | 2 2 2 5 |
   | 2 | 1 2 5 0 |
   | 3 | 1 2 2 3 |
   | 4 | 2 2 5 0 |
   | 5 | 1 0 0 0 |
   | 6 | 5 0 0 0 |
   | 7 | 0 0 0 0 |
   | 8 | 1 2 2 5 |
   | 9 | 1 1 1 5 |
   | 10 | 1 3 0 0 |
   (Row s is consulted after line s−1 was started, i.e. row s accompanies narration line s−1.)
2. Speech: if a narration line is still playing → nothing. When it ends: free it, set F, swap the
   head to the idle head (H9 = 11010 while s < 8, H10 = 11011 afterwards) and start it.
3. When F is set and the current head AO has stopped: clear F, k := −1, then by s:

   | s | head | speech (SPEECH.DAT) |
   |---|---|---|
   | 0 | H0 11001 | 1862 |
   | 1 | H1 11002 | 1863 |
   | 2 | H2 11003 | 1864 |
   | 3 | H3 11004 | 1865 |
   | 4 | H4 11005 | 1866 |
   | 5 | H5 11006 | 1867 |
   | 6 | H6 11007 | 1868 |
   | 7 | **Telly interlude** (below), then F := 1 and head = H10 (no speech) | — |
   | 8 | H7 11008 | 1869 |
   | 9 | H8 11009 | 1870 |
   | 10 | end of narration | — |
   s += 1; start the head; load + play the speech line.
   At the very first frame no speech is playing, so the idle head H9 runs first, then line 1862.

So the 9 lines are strictly sequential, each starting when the previous line has finished **and**
the idle-head clip that follows it (11010/11011: ~6 frames then PAUSE) has stopped. Line timing is
therefore set by the WAVE lengths, not by fixed times.

**Telly interlude (after line 1868).** Blocking, in order:
1. Stop 10001 and 12008. Start 10001 sequence 1 at fps 10 (Telly throws the disc) and play SSO3 WAVE
   **11001** (priority 30). Wait until 10001 stops (it PAUSEs on the release frame).
2. Start 12007 at (299, 261) z 30 (disc flies up-left in 4 steps of (−21,−12), then splat cel 1 at
   (−107,−67) relative). Stop 10001, select its seq 0, fps 8, restart (idle). Restart 12008.
   Wait until 12007 ends.
3. Start 12006 at (182,153) z 25 (MoM startled, full-body), hide the current head and body, play
   SSO3 WAVE **11000** (priority 35). Wait until 12006 stops (PAUSE after 4 frames).
4. Hide 12006; restart the current body. Continue with line 1869.

**Keys during narration:**
* Enter: sets "key pressed" and halts the current head clip. If a line is playing it is stopped and
  the narration ends; if not, the stage machine simply advances. The attract part (§1.3) still runs
  once, then the loop exits to sign-in.
* Space: stop the line, end the narration, skip := true (no attract part; straight to sign-in).
* Esc: quit the program.
* Mouse click on the screen: stop the line, halt the head, end the narration, "key pressed". **[med]**

At the end every intro AO is destroyed and any playing line is freed.

### 1.3 Attract part A/B — ASEQ 1700 (SSO1) scripts  [high]
Sequence 0 (on the title card, fps 5): tower beacon cels 1–2 at (454,40)/(454,38); spinning ring cel 3
at (426,18); user opcode −500 a=1727 b=5 → **play SND 1727 at priority 5**; blast cel 4 at (415,13);
STAMP + lightning beam cel 7 (87×130) at (385,50); beacon cels 5,6 at (450,34),(440,29); STAMP +
beam at (320,178); STAMP + beam at (255,306) (the beam crawls down-left across the title in three
permanent segments); user −501 a=1727 → **stop SND 1727**; END.

Sequence 1 (on the exterior, cel 27, fps 5): −500 1727 pri 5; then 4 MULTI frames building a
diagonal beam from cel 8 tiles at (316,52),(298,118),(280,184),(269,223) together with the player
cels 9–12 running in at (205..217, 269..292); −500 1727 pri 6 (re-trigger); player cels 13–24 at
(205..243, 267..284) running/zapping toward the door; door cels 25,26 at (224,233); END.

**User opcodes (game hook during the intro):**

| opcode | as u16 | effect |
|---|---|---|
| −500 | 0xFE0C | play SND WAVE `a` with priority `b` (flags 0) |
| −501 | 0xFE0B | stop SND WAVE `a` |
| (key 0x0D Enter / 0x20 Space delivered to the same hook) | | stop SND `a` and set skip |

No text overlays are drawn in the intro; all text ("The Learning Company presents…", "Click mouse to
continue.") is baked into cel 0.

---

## 2.0 Which art the ending uses  [high]
SSO3 ASEQ 15000 / 10001 / 11001–11011 / 12001–12008 and WAVE 11000/11001 are used **only by the intro
narration** (§1.2), not by the Decide/hideout sequence. The "hideout room" art of the ending is the
chosen room's normal background (SSO1 ASEQ 1001/1003–1006) plus SSO1 ASEQ 215 (win) or 220 (lose)
with ESEQ 1000 / 1001.

## 2. Decide → hideout door → outcome

### 2.1 Common part (both outcomes)  [high unless marked]
Input: chosen room r (0 News, 1 Game Show, 2 Cartoon, 3 Equipment, 4 Sound) and correct = (r == hideout).
1. If correct: the game is saved to the player record (saved flag set; record + header written)
   so a crash during the ending does not lose the win **[med on intent]**.
2. Clock frozen; "decide in progress" flag set.
3. Hall: scroll the hall to the door of room r (the room door in hall area {1,3,6,7,8}[r]); redraw
   clock, score and energy. The player is placed at the door's column + 2, y 81, and turns to face the
   door (as when entering a room, chapter 04 §5.4); `enter.mid` (TEXT 1717) is loaded and started
   (once); the door opens (door cels 134→136) and the player walks through it. **[med on the exact
   player animations]**
4. Room interior: paint the room background (ASEQ 1001/1003/1004/1005/1006 for r = 0..4, 492×247)
   at (10, 36); paint ASEQ 1022 cel 0 (74×115, the doorway on the left wall) at (80, 92); paint the
   security camera ASEQ 7000 at (243, 42); present.
5. Load the outcome sprite set: **ASEQ 215** if correct, **ASEQ 220** if wrong. Install the ending
   user-opcode hook (§2.5).
6. The player (hall sprite set ASEQ 200 with ESEQ 1002 attached) plays ESEQ 1002 seq 0: walks
   down through the doorway at x 77 (y 96→156) then right to (154,156), PAUSE. Wait for it.
7. Stop and free enter.mid.
8. **Taunt** (random t = 0..7, `rand()*8/32768`):
   * Voice off: paint a speech bubble (215 cel 27 / 220 cel 13, both 192×132, tail bottom-left) at
     (154, 30); draw TEXT 500+t in the ending font, every line centred, the block centred in the box
     (188, 44, 118×86); present; wait 960 delay-units (≈ 4.0 s, see §5); erase the bubble by
     restoring the background rectangle (154,30,192×132).
   * Voice on: play SPEECH 1838+t (no bubble, no text).
9. Attach **ESEQ 1000** (correct) / **ESEQ 1001** (wrong) to the outcome set; load SFX list
   **INTS 1702** (correct) / **INTS 1703** (wrong) for the user opcodes. Play ESEQ seq 0 (−1) and
   wait: a 5-cycle loop of two small cels at (469,59)/(479,63) — the door's "knocking" light —
   each cycle playing list[0] = SND 1702 (knock). (1000: cels 19/20; 1001: cels 11/12.)
10. Outcome: correct → §2.2, wrong → §2.3.
11. Afterwards: hall sprite AO stopped, ESEQ detached, hook removed, SFX list freed, menus restored;
    **Continue prompt** (§4.2) at (199, 346) after a win, at (115, 322) after a loss; then
    "new game, same player".

### 2.2 Correct: the Master of Mischief is caught  [high]
1. Load ASEQ 1011 (a 307×115 overlay; fps 20, viewport-clipped) — kept until the end of the
   sequence and destroyed after the score screen. **[med: no sequence of it is started here; it
   is the score-screen "slide" banner]**
2. Result flag = win.
3. ESEQ 1000 seq 2 (−3): STAMP the opened door (215 cel 14, 30×171) at (474, 86) into the
   background, then 6 more knock-light cycles (SND 1702 at priority 5). Wait.
4. Set the viewport clip's right edge to 493. ESEQ 1000 seq 1 (−2), viewport-clipped: start the
   whirlwind SFX (−501 → list[1] = SND 1759, looping flag); the MoM spins out of the door as a
   tornado (cels 13,17,18, two frames each) from x 486 to 282 at y 159 (≈ 13 px per step), slows
   (cels 12, 11), stop SND 1759, PAUSE, then stands (cel 0 at (282,159)). Wait. Clip right edge
   back to 502.
5. **MoM's line** (random m = 0..7):
   * Voice off: bubble 215 cel 27 at (307, 48); TEXT 508+m centred in box (341, 62, 118×86);
     wait 700 units (≈ 2.95 s); erase the bubble rect (307,48,192×132). The standing MoM stays.
   * Voice on: hide the MoM; show a **talking head** ASEQ 1013+m (59×~51, one per line) at
     (294,159) z 10, fps alternating 8↔9 every frame, plus ASEQ 1021 (38×66, the body under the
     head) at (305,192) z 10; play SPEECH 1846+m. Wait until the head clip reaches its PAUSE **and**
     the speech has ended; free speech; destroy head + body; ESEQ 1000 seq 8 (−9): PAUSE then the
     standing MoM (cel 0 at (282,159)).
   TEXT 508..515 and SPEECH 1846..1853 / ASEQ 1013..1020 correspond 1:1 by m.
6. **Bonus screen** (§3.1).
7. Lifetime score update (chapter 05 §5.3).
8. **Award animation**, chosen by the *new* lifetime score:
   * lifetime < 500,000: player zaps (ESEQ 1002 seq 3: −503 → hall SFX list[0] = SND 1764 at
     priority 10; aim cels 47–49; beam cels 50–55 at (250,179)); then MoM is zapped (ESEQ 1000
     seq 3: cels 1/15 shaking, electrified cels 21–26 at (294,165), SND list[3]=1738 / list[4]=1739).
   * lifetime ≥ 500,000 (Champion): player aims (ESEQ 1002 seq 1, cels 47,48, PAUSE, 49); MoM gets
     the big zap (ESEQ 1000 seq 7: explosions cels 42–45, turns into a TV set cels 30–35 and
     colour-bar static 38–41, with SFX list[3]/[4]/[5]); player zaps again (ESEQ 1002 seq 4); the
     TV set is beamed away (ESEQ 1000 seq 9: cels 36,37,29, lightning 51–55, SFX list[5] = 1768 and
     list[6] = 1769, STAMPs, cels 47–50 along x 320..333 up to y 33/276).
   Each step waits for its clip to end.
9. **Victory**: load + play `victory.mid` (TEXT 1757) once; player celebrates (ESEQ 1002 seq 2:
   walks right x 154→410 at y 156, cartwheels back left (mirrored cels 34–39), cheers (cels 47–49)),
   wait; wait for the music to end; free it.
10. **Score summary screen** (§3.3).

### 2.3 Wrong room: the MoM wins  [high]
1. Result flag = lose; stop all SFX.
2. Simultaneously: player stands (ESEQ 1002 seq 5: 4 frames of cel 0 at (154,156)) and ESEQ 1001
   seq 1 (−2): lightning bolts (220 cels 0–3, at (202,81),(190,81),(143,81),(154,81)) with
   −500 → list[1] = SND 1762 priority 5, then the player drops through a trap door (cels 4–9 at
   x 154/166, y 170→248). Wait for the ESEQ clip.
3. Play SND 1760 (priority 5).
4. **MoM's gloat** (random g = 0..7):
   * Voice off: bubble 220 cel 10 (256×102) painted at (2, 88); TEXT 516+g centred in box
     (36, 113, 181×55); present. The bubble stays on screen (no wait, no erase).
   * Voice on: wait for SND 1760 to finish, then play SPEECH 1854+g (waited **[med]**).
5. Drain: energy is removed one unit at a time (delay 10 units ≈ 42 ms per unit, no SFX); then the
   clock is advanced by all remaining minutes in steps of 10 minutes (delay 20 units ≈ 84 ms per
   step, no SFX) — the hall clock display updates each step; the game score is set back to 0
   (score display redrawn, SND 1747).
6. **Midnight sequence** (§4.1).

### 2.4 Ending sprite sets (reference)
* ASEQ 215 (57 cels): 0–3 MoM standing/turning; 4–9 small sparks; 10–13 turning/whirl; 14 open door
  30×171; 15–16 MoM half-figures; 17–18 tornado; 19–20 knock light; 21–26 electrocuted/energy;
  27 speech bubble 192×132; 28 MoM with boxing gloves; 29 flat disc; 30–35 MoM → TV set;
  36 TV on stand; 37 TV wreck; 38–41 colour bars; 42–46 explosions; 47–50 rubble/discs; 51–55
  lightning; 56 MoM.
* ASEQ 220 (14 cels): 0–3 lightning strikes; 4–9 player falling into a hole; 10 wide speech bubble
  256×102 (tail top-right); 11–12 knock light; 13 bubble 192×132.

### 2.5 Ending user opcodes (hook active from step 2.1.5 to 2.1.11)  [high]
`list` = the INTS loaded in step 2.1.9 (1702 win: 1702 1759 1737 1738 1739 1768 1769 1703;
1703 lose: 1702 1762 1760). `hall` = INTS 1700 (preloaded hall SFX).

| opcode | u16 | effect |
|---|---|---|
| −500 | 0xFE0C | play SND list[a], priority b, flags 0 |
| −501 | 0xFE0B | play SND list[a], priority b, flags 1 (repeat/loop) |
| −502 | 0xFE0A | stop SND list[a] |
| −503 | 0xFE09 | play SND hall[a], priority b |
| −504 | 0xFE08 | play SND id a directly, priority b |
(The intro's hook only implements −500/−501, with `a` a direct SND id.)

## 3. Bonus panel, award, score summary

### 3.1 Bonus panel ("B O N U S")  [high for numbers/timing, med for colours]
It is **not a separate screen**: it is an overlay panel that slides down over the room scene,
while the MoM (ASEQ 215 + ESEQ 1000) stays visible and reacts. ASEQ 7010 (the clipboard) is **not**
used here (no code reference found).

Constants by tier T = rank div 2 (0..3): rateE = {100,150,300,400}, rateT = {25,35,50,60},
win = {1000,2000,3000,5000}, winChunk = {50,100,150,250}. minutesLeft = (11 − hour)·60 + (60 − min)
from the clock (0 if the Clock option is off).

Panel = SSO1 ASEQ 1011 cel 0 (307×115). Text: title font = the large UI font, line font = the normal
ending font. Palette indices: 0 black, 0xF9 red, 0xFB yellow, 0xFE cyan, 0xFF white.

Steps:
1. MoM reacts: ESEQ 1000 seq 5 (−6): cels 1,2,3,10 at (282,159), then a spray of sparks (MULTI of 7
   cels 4–9 across (243..410, 109..213)) with SND list[2] = 1737, PAUSE. Wait.
2. **Panel slides in**: play SND **1748** (priority 5); ASEQ 1011 seq 0: panel at x 102, y from −75
   to +30 in steps of 5 px per frame (fps 20 → ≈1.1 s), clipped so nothing is drawn above y 35;
   reaches (102,30), then PAUSE; the entry after the PAUSE, (102,35), is drawn in the same tick
   (see 02 PAUSE), so the panel **rests at (102,35)** and spans (102,35)–(408,149). Wait. Free 1748.
3. Draw the title "B O N U S" (string with spaces) in the large font, centred over x 109..404
   (width 296): let x0 = 109 + (296 − w)/2 + 1. Six shadow copies at (x0+i, 71−i), i = 0..5, then the
   face at (x0+6, 65). Then the value line in white, normal font, centred over x 138..372
   (x = 138 + (235 − w)/2), at y 112. Then wait 240 units (≈1.0 s).
4. **Energy Left**: line "Energy Left: " + energy·rateE (thousands commas). Title colours: shadow
   black, face white. Then, while energy > 0: remove 1 energy unit (HUD energy bar shrinks), SND
   **1703** (priority 2), wait 10 units; add rateE to the game score (HUD score updates, SND 1747),
   SND 1703, wait 10 units. (≈ 2×42 ms + sound per unit.)
5. MoM reacts again: ESEQ 1000 seq 4 (−5): spark spray twice with SND 1737, PAUSE. Wait.
6. **Time Left**: line "Time Left: " + minutesLeft·rateT. The panel's two key colours are swapped
   (yellow↔cyan); title shadow white, face black. Then, while minutes > 0: s = min(15, minutes);
   advance the clock by s minutes (HUD clock updates; SND 1703; wait 10), add s·rateT to the score
   (SND 1703; wait 10); minutes −= s. The clock therefore ends at 12:00 (midnight is not triggered —
   the clock is frozen at that point).
7. MoM reacts (ESEQ 1000 seq 4 again). Wait.
8. **Winning Game**: line "Winning Game: " + win. Panel colours become red/black; title shadow
   black, face yellow. Then add win in winChunk pieces (20 pieces in every tier), each with SND 1703
   and a 10-unit wait.
9. **Panel slides out**: SND **1749** (priority 5); ASEQ 1011 seq 1: y 30 → −75 in 5-px steps,
   same clip, END. Wait. MoM: ESEQ 1000 seq 6 (−7): cels 3, 2, PAUSE, 1. Wait. Free 1749.
(Clarification vs. the shell notes: 1748/1749 are the panel in/out sounds; the per-step tally
sound is 1703, plus 1747 from every score change.)

### 3.2 Award animation and victory (see §2.2 steps 8–9)
Chosen by the new lifetime score: < 500,000 → "zap the MoM" (ESEQ 1002 seq 3 + ESEQ 1000 seq 3);
≥ 500,000 → "turn the MoM into a TV and beam it away" (ESEQ 1002 seq 1, ESEQ 1000 seq 7, ESEQ 1002
seq 4, ESEQ 1000 seq 9). Then victory.mid + the player's celebration. ASEQ 225 is only used by the
score summary (§3.3).

### 3.3 Score summary ("Points / Rank")  [high for layout, med for colour names]
On entry: freeze the clock; clear the saved-game flag; if the player was a Trainee at the start of the game
and is now Junior or higher, turn New Player Hints off; write the record and the high-score file. Load ASEQ 225.
1. (v1.19: everything painted in steps 1–4 goes into the scene, i.e. both the frame and the background page; every "erase" in steps 6–8 restores the pixels that were under the erased cel, so the tower art and the blue markers reappear. Cel 16 has transparent corners.) Fill the whole 512×384 screen with palette 0 (black). Paint ASEQ 225 cel 0 (333×334: golden
   statue of the player on a TV set, beside the transmitter tower with 8 blue triangle markers) at
   (0,0). Draw a 2-px red (0xF9) line across the full width at y 336–337. Present.
2. Headings (normal ending font): "Points" in white, left-aligned so that its left edge equals the
   left edge of the widest threshold, i.e. x = 400 − width("500,000"), y 0; "Rank" in yellow at
   (427, 0); a yellow horizontal line from (306,14), 206 px long.
3. Rank ladder, 8 rows, rank i (0 Trainee … 7 Champion): threshold (thousands commas) in white,
   right-aligned to x 400; rank name in yellow at x 427; row y:

   | i | rank | threshold | text y | marker y |
   |---|---|---|---|---|
   | 7 | Champion | 500,000 | 17 | 16 |
   | 6 | All-Star | 300,000 | 78 | 77 |
   | 5 | Master | 200,000 | 126 | 125 |
   | 4 | Expert | 120,000 | 173 | 172 |
   | 3 | Pro | 60,000 | 215 | 214 |
   | 2 | Senior | 30,000 | 247 | 246 |
   | 1 | Junior | 5,000 | 277 | 276 |
   | 0 | Trainee | 0 | 306 | 305 |
4. Player box (on the cyan TV screen of the trophy), colour 0 (black): the player's name centred over
   x 45..188 at y 217; "Game" at (35, 248) with the game score right-aligned to x 197; "Total" at
   (35, 271) with the lifetime score (commas) right-aligned to x 197. Present.
5. Play SND 1764 (priority 3).
6. **Statue beam** (picture script, one frame per 30 units ≈ 0.13 s, each frame erases the previous
   one): ASEQ 225 cels 1, 2, 3, 4, 5 (90×35, the statue's zapper beam to the tower top) at (192, 0);
   then the last frame is erased.
7. **Tower beam**: a 2-px-wide red (0xF9) vertical line at x 256–257 is drawn downward, one pixel
   row per 1/100 s, through the gaps between markers: y 37–76, 98–124, 146–171, 193–213, 235–245,
   266–275, 297–304 (≈1.4 s total).
8. **Rank markers**: for i = 0 .. current rank: start SND 1755 (priority 2); blink the red marker
   (ASEQ 225 cel 16, 41×21) at (237, marker y[i]): on 0.13 s, off 0.13 s, on 0.13 s (it stays lit);
   stop SND 1755. Then present.
9. If the rank is **Champion** (7) an extra picture script runs (30-unit frames): cels 6,7,8 at
   (192,0),(243,0),(243,0), again 6,7,8, then cel 9 at (243,0); SND 1727 (priority 3); cel 10 at
   (243,2); cels 11, 12, 13 at (128,2); cel 14 (154×134 explosion) at (128,0); restore the area;
   cel 15 (gold MoM, electrified, 90×134) at (128,0); SND 1739 (priority 2).
10. Back in §2.1 step 11: the Continue button (ASEQ 1012) at (199,346), below the red line.

The embedded strings "<Esc-key glyph> to Go Back" and "<Enter-key glyph 0x8B> to Continue" exist
in a pointer table next to the score-screen strings but are **not referenced by any code** in the
Windows build (DOS leftovers; the Continue button replaces them).

## 4. Midnight and the Continue prompt

### 4.1 Midnight sequence  [high]
Used after a wrong decision and when the clock runs out in play.
1. Freeze the clock; disable menus; stop hall activity.
2. Load and start `midnite.mid` (TEXT 1735), once.
3. 10 times: draw hall-sprite cel 74 ("MIDNIGHT") on even passes / cel 75 (red "MIDNIGHT") on odd
   passes at (402, 304) over the clock box; present; wait 200 delay units (≈ 0.84 s). Total ≈ 8.4 s.
4. Wait until midnite.mid has finished; free it; re-enable menus.
When it is reached in normal play (not after Decide) the program then clears score and energy
*before* the flashes, clears the saved-game flag, writes the record, shows DIALOG 9136 and restarts
a new game (chapter 03).

Clock box reference (hall HUD, same coordinates): box cel 77 ": P.M." at (402,303); hour tens digit
(cel 64+d, only if ≥ 1) at (407,308), hour units at (418,308), minute tens at (434,308), minute units
at (445,308); "PAUSE" cel 76 at (402,303) when the clock option is off. Score: 6 digits (cels 64+d),
rightmost at x 467, each 12 px to the left, y 353. Energy bar: background cleared over
(403,327)–(480,345), bar filled with palette index 0xF9 from x 403 to 402+3·min(energy,24), y 327–345.

### 4.2 "Continue" prompt (ASEQ 1012)  [high]
ASEQ 1012 cel 0 is a 114×30 button reading "Continue" (key-cap style). It is painted at (x,y)
(default (115,322); after a win (199,346); after a loss (115,322)) and registered as a hot region
whose click = Enter. The program then waits: **Enter** (or click) → return (new game, same player);
**Esc** → the Exit confirmation (chapter 03); all other keys are ignored. The hot region is removed
afterwards.

## 5. Delay unit
The scripted waits use a delay routine with argument n: wait `floor(floor(4n/13)·100/73)`
hundredths of a second (≈ 4.215 ms per unit) while keeping animations and messages running.
Examples: 960 → 4.04 s, 700 → 2.94 s, 200 → 0.84 s, 20 → 0.08 s, 10 → 0.04 s.

---

## Open questions / uncertainties
* Intro: the 12008 callback's effect on 10001 (toggles head fps 8/9 and re-anchors Telly to 12008);
  exact on-screen meaning inferred from cel art only.
* Hall part of the door walk (animation ids 13/19, door slot table) belongs to chapter 04 (auto-walk); only
  the call sequence was traced.
* Palette-remap of the bonus panel was not traced in detail; "colours swap" is inferred
  from the arguments (0xFB/0xFE → 0xFE/0xFB → 0xF9/0).
* Text y coordinates are passed as-is to the text routine; whether they are top or baseline follows
  chapter 02 §4.1 (the pen y is the baseline).
* Voice-on wrong-room gloat: whether SPEECH 1854+g is awaited before the drain starts
  was not verified.
* One helper called during the rank-marker loop (argument = rank step) was not identified; it is probably a short delay or sound.
* ASEQ 7010 (clipboard) usage not found in these sequences.

---------------------------------------------------------------------------------------------------
# Part C — details added in v1.4 (these take precedence over Parts A/B where they differ)


Coordinates are 512×384 canvas pixels. Delay times are given in centiseconds (cs). The game's
scripted-delay unit n becomes `cs = floor(floor(4n/13)·100/73)`, measured on a 10 ms clock.
So 1→0, 5→1, 10→4, 30→12, 50→20, 60→24, 80→32, 240→100, 720→302 cs.
While a delay runs the game keeps pumping messages and refreshing the screen.
Cel numbers without a resource name are SSO1 ASEQ 200 cels. Pieces and clues use cels 78–97 (41×29).
Confidence: H = read from code, M = inferred.

### Behaviour

### 1. Decoder screen: opening and closing (H)

Before the screen opens (in the hall or a room, Tab or the Decoder button):
* The first time in a game, New Player Hint 14 appears (only if hints are on).
* The music stops (the current tune is remembered) and the Decoder button is removed.

Opening, in order:
1. Two sounds are preloaded: 1712 (the spin sound) and 1756 (the button click). All playing sound
   effects stop. A full-screen click region is registered. The button focus is set to Station.
2. The panel/HUD frame is rebuilt and the clock box shows PAUSE.
3. SSO2 ASEQ 6000 cel 0 is stamped at (11,35). It already shows the empty slots and the room labels.
4. Buttons come from SSO2 ASEQ 6002:
   * Station at (33,312) with cel 6 (dark, has the focus).
   * Match Rule at (115,312) with cel 3 (bright).
   * Decide at (198,312) with cel 5 (bright).
   The screen is then presented, so the player sees the decoder with empty slots.
5. **No room row is highlighted at first.** The first Down press highlights NEWS ROOM (row 0). The
   first Up press highlights SOUND ROOM (row 4). This corrects Part A §5 item 6.
6. **Code pieces**: 4 slots at x = 58 + 43·i, y = 198, handled in order i = 0..3.
   * At the start of every slot, collected or not, sound 1712 is started looping. If it is already
     playing, the request is ignored.
   * Collected slot:
     * Show cels 78, 79, …, 97 one after another at the slot, with a 1 cs delay after each.
     * Stop all sound effects.
     * Wait 24 cs. Draw cel 97 again (no visible change). Wait 32 cs.
     * Draw the final cel 78 + symbol.
   * Empty slot: nothing is drawn. The picture's empty slot stays as it is.
   So all collected pieces spin one after another, left to right. Each spin is about 20 frames,
   followed by a hold of about 0.56 s on cel 97 before the real symbol appears.
7. **Room clues**: rows r = 0..4 (NEWS, GAME SHOW, CARTOON, EQUIPMENT, SOUND), top to bottom.
   Row r has 4 slots at x = 298 + 43·k, y = 55 + 47·r.
   * A row animates only if its first slot is collected. Clues are always filled into a row's slots
     left to right, so this means "the row has at least one clue".
   * Animated row:
     1. Start sound 1712 (looping, ignored if already playing).
     2. 20 steps, s = 0..19. In each step, every collected slot k shows cel
        78 + ((pic_k + s) mod 20), all at the same time. Wait 1 cs after each step.
     3. Wait 12 cs.
     4. Draw the final cel 78 + pic_k in each collected slot.
   * After every row, animated or not: stop all sound effects, then a delay of 1 unit (0 cs).
   * Empty slots are never drawn.
8. **Every time the decoder opens, all collected pieces and clues spin again.** The game keeps no
   "already shown" state: on every open it redraws from the empty art and animates everything
   collected so far, not only the new items.
9. Sound summary: 1712 loops while the pieces spin and while each clue row spins, including the
   12 cs hold. It also blips briefly when an empty piece slot starts it and the next "stop all"
   cuts it off. There is no other opening sound or music.

Closing (Station button or Esc):
* The sounds 1712 and 1756 are stopped and freed.
* The click region is removed, the button areas are invalidated and ASEQ 6002 is freed.
* The caller then redraws the hall or room at the current scroll position, with all sprites, in
  one go. There is no transition or animation: it is an instant cut back.
* The remembered music restarts, the Decoder button is created again and the clock resumes.

### 2. After a won Telly drill reveals a code piece (H)

Sequence on the drill screen:
1. The question area and the message area are cleared.
2. **Reveal animation**: SSO2 ASEQ 8604 cels 3, 4, 5, 6, 7, 8, 9, anchored at about (328,210).
   * Each frame lasts 20 cs. The previous frame's area is erased, the new cel is drawn and
     presented, and sound 1709 is played (restart).
   * After the 7th frame (cel 9 stays on screen): sound 1709 is stopped and sound 1710 is played.
   * The whole animation takes about 1.4 s.
3. The next code piece is marked as collected, in slot order.
4. Its symbol cel 78 + symbol is stamped at (242,125).
5. **Score +100.** The HUD score counter updates, with the usual score sound 1747.
6. A fixed wait of 302 cs (about 3 s) follows. Keys do not cut it short.
7. Drill teardown, then back to the hall code:
   * If New Player Hints are on and hint 3 has not been shown this game, **dialog 1003** appears:
     "The piece of secret code you have captured has been placed in your decoder…".
   * The clock is paused and sounds are stopped while the hint is shown. The clock resumes after
     Continue.
   * The dialog appears over the still-visible drill screen.
8. The hall is redrawn in full, Telly is reset, the remembered music restarts and the Decoder
   button is created again in its normal state. Control returns to the hall.

**Nothing else changes:**
* The Decoder button does not flash.
* No other HUD element changes besides the score.
* The decoder itself is not updated until it is next opened. Then all items spin (see §1).

A write-only "got a code piece" flag is set, but nothing reads it.

On failure, sound 11 plays and "No Code.   Catch me again!" shows for 200 cs. Nothing else changes.

### 3. Bonus panel "B O N U S" (SSO1 ASEQ 1011) (H)

**Recolouring rule**
* The panel art has two "key" colours: index 251 (yellow, 4,769 pixels) and index 254 (cyan, 4,418
  pixels). Its other colours (25, 245, 226) never change.
* Recolouring is a palette-index substitution applied **to the whole panel image**, not to a
  rectangle. It changes the panel cel's pixel data, so the change persists, including during the
  slide-out. All substitutions in one step happen at the same time, so a swap works.
* Right after a recolour, the panel is redrawn in full. This also wipes the previous title and line
  text.

| moment | substitution (relative to the panel's current colours) | key colours that result (originally yellow / originally cyan) | title shadow | title face |
|---|---|---|---|---|
| Energy Left (after the slide-in) | none | yellow 251 / cyan 254 | black 0 | white 255 |
| Time Left (after the MoM's 2nd reaction finishes) | 251→254 and 254→251 (swap) | cyan 254 / yellow 251 | white 255 | black 0 |
| Winning Game (after the MoM's 3rd reaction finishes) | current 254→249 and current 251→0 | red 249 / black 0 | black 0 | yellow 251 |

Order inside each stage: recolour (stages 2 and 3), then the title, then the value line, then a
wait of 100 cs, then the count-up.

**Title "B O N U S"**
* Font: NFNT 28568 (large serif, 24 px).
* Let w be the text width and x0 = 109 + trunc((296 − w)/2) + 1.
* Six shadow copies are drawn at (x0 + i, 71 − i) for i = 0..5, in the shadow colour.
* The face is drawn at (x0 + 6, 65) in the face colour.
* All y values are the top of the text line.

**Value line**
* Font: NFNT 28625 (small bold sans). Colour white 255 in all three stages.
* Position: x = 109 + 29 + trunc((235 − w)/2), y = 112.
* Numbers use thousands commas.
* Lines:
  * "Energy Left: " + energy·rateE
  * "Time Left: " + minutesLeft·rateT
  * "Winning Game: " + win
* Each line is the total for its stage and is drawn once. It does not tick: the tally shows in the
  HUD (energy bar, clock, score).

**Constants and count-up** (tier T = rank div 2)

| T | rateE | rateT | win | winChunk |
|---|---|---|---|---|
| 0 | 100 | 25 | 1000 | 50 |
| 1 | 150 | 35 | 2000 | 100 |
| 2 | 300 | 50 | 3000 | 150 |
| 3 | 400 | 60 | 5000 | 250 |

minutesLeft = (11 − hour)·60 + (60 − minute). It is 0 if the Clock option is off.

* **Energy**: while energy > 0, repeat:
  1. Energy −1 (HUD bar), sound 1703, wait 4 cs.
  2. Score +rateE, sound 1703, wait 4 cs.
* **Time**: while minutes > 0, repeat with s = min(15, minutes):
  1. Clock +s minutes, sound 1703, wait 4 cs.
  2. Score +s·rateT, sound 1703, wait 4 cs.
  3. minutes −= s.
* **Win**: 20 times: score +winChunk, sound 1703, wait 4 cs.

Every score change also plays the HUD score sound.

### 4. Sign-in list rows (H)

**Layout**
* The whole sign-in screen uses one font: **NFNT 15001** (sans 12 pt with key caps, 21 px).
* Up to 6 rows. Row k (k = 0..5) has its top at y = 108 + 22·k.
* Text is drawn at y + 1 (the top of the text line).
* Columns:
  * Name at x = 97.
  * Rank title at x = 227.
  * Score right-aligned so that it ends at x = 387, with thousands commas.
* The rank title and score are drawn **only if the entry's score is ≥ 0**. Provisional entries
  (score −1 for typed, −2 for blank NEW PLAYER) show the name only.

**Colours** (palette index; the 16-colour index is in brackets)

| row | background | name | rank and score |
|---|---|---|---|
| normal (not selected) | background art restored over x 87..398, y−2..y+20 | white 255 [15] | white 255 [15] |
| selected, nothing typed | solid **black (0)** bar filled over x 87..399, y−2..y+20 (inclusive) | cyan 254 [14] | cyan 254 [14] |
| selected, while typing (real player found by prefix, or the provisional row) | the same black bar | the first n characters (n = typed length) in **yellow 251 [11]**; the rest of the name in cyan 254, starting at x = 97 + width of the yellow part | cyan 254 (real player only; the provisional row has none) |

**Highlight and redraw**
* The selection highlight is only the black bar plus the cyan/yellow text. There is no outline and
  no inverse cel.
* The provisional row is the selected row, so it shows the typed text in yellow on black.
* A blank NEW PLAYER entry holds an invisible placeholder character, so it shows an empty black bar.
* Unselected rows have no box. Their area is restored from the background art, and they are drawn
  in white.
* When the list has fewer than 6 rows, the slot below the last row is restored to the background.
* When only the selection changes, only the old and new rows are redrawn. Scrolling redraws all
  rows.

### Open questions
* The ASEQ 1011 pixel data is changed in memory. If the resource stays cached, a second win in the
  same session might start from red/black art. Not verified whether the panel ASEQ is reloaded.
* The meaning of the 0x40 flag passed when the decoder stamps its cels was not identified. It
  probably has no visible effect.
* The decoder code contains two calls to an empty drawing routine. They were probably
  meant to draw split "rolling" half-cels and do nothing in the shipped game.

### C.5 Additions in v1.5
* **Music during the ending.** Per-frame background-music selection (04 §14.2) is switched OFF when
  the Decide door sequence starts and stays off until the next game starts. So `enter.mid` plays
  undisturbed, followed by the outcome music. The same switch is off during elevator rides and the
  room-entry walk.
* **The bonus panel is an animated object** (SSO1 ASEQ 1011: sequence 0 slides it down and rests at (102,35),
  sequence 1 slides it up, sequence 2 holds it). It sits above the room sprites in z-order. Its title
  and value line are drawn onto it after it is drawn (post-draw), so whenever another sprite (for
  example the room's security camera) is erased or moves over it, the overlap rule of 02 §5.3 redraws
  the panel with its text intact. Sliding out erases it by restoring the background, which leaves no
  remnants. The recolouring of Part C §3 changes the panel cel's pixels.
* **Decide prompt highlight.** Entering Decide keeps the decoder's current room highlight (none
  right after opening). Down from "none" highlights NEWS ROOM; Up from "none" highlights SOUND ROOM;
  otherwise Up/Down move cyclically. Enter or a click on a room row chooses it. Enter with no room
  highlighted, or Esc, leaves Decide without a decision. The chosen room then gets its Yes/No dialog
  (1017 + room).
---------------------------------------------------------------------------------------------------
# Part D — Drill for Skill

Coordinates are 512×384 canvas pixels. Times are in centiseconds (cs); "delay n" is the scripted-delay
unit (`cs = floor(floor(4n/13)·100/73)`: 20→8, 30→12, 40→16, 120→49, 240→100, 480→201, 720→302).
Cel numbers without a resource name are SSO1 ASEQ 200 cels. "Drill screen", "TV static", "button
press", the question line, praise and wrong-answer feedback are as in Part A §4; this part gives what
differs or was missing. Where this part and Part A §4 differ, this part wins.
Confidence: H = read from code/data, M = inferred, L = guess. Everything is H unless marked.

## D.1 Entering Drill for Skill

1. Drill for Skill is reached **only from the sign-in screen** (DRILL FOR SKILL button, or Enter while
   that button has the focus). Nobody is signed in during the session, and nothing about it is ever
   saved.
2. Starting customisation: the **tier defaults** (chapter 05 §2.1) for tier = rank ÷ 2, where rank
   is derived from the lifetime score held in the program's single in-memory player record. That
   record is not cleared at sign-in: after a fresh launch it is empty (rank Trainee → tier 0: 5
   problems per set, + and −, numbers 0–5, format `a+b=?`); after another player's game it still
   holds that player's score (M: practical effect). Recommended: always use tier 0. The DfS block
   stored in the player file is neither read nor written by Drill for Skill.
3. New Player Hints for the session = (that rank == Trainee). The hint-shown flags are cleared.
4. The menu bar switches to the **Drill** set (§D.7). No rank dialog, no music.

## D.2 Screen composition (in this order)

1. Rebuild the panel/HUD (Part A §1) and present. Stop all sounds and music. Force NumLock **on**
   for the whole drill (it is switched off around the Exit confirmation and when the program leaves).
2. Preload SFX 1705, 1744, 1761, 1711, 1709, 1710, 1756.
3. Stamp SSO2 ASEQ 8600 at (13,36) into the frame; present the whole screen; stamp 8600 into the
   background as well.
4. Stamp SSO2 **ASEQ 8605** (174×80, "Time / Problems / Correct" labels with three black fields) at
   **(326,297)** into the frame only. It covers the HUD clock, energy and score.
5. Stamp SSO2 **ASEQ 8540** (124×72 numeric keypad "ENTER 7 8 9 / C 4 5 6 / 0 1 2 3") at
   **(140,301)** into frame and background. (This keypad is on the Telly drill screen too.)
6. Load SSO2 ASEQ 8604 as the extra sprite set. There is **no Station button**.
7. TV static (Part A §4: 8 frames alternating 8604 cels 10/11, 8 cs each, SFX 1752 priority 10, then
   cel 12).
8. Time := 0:00; Problems := 0; Correct := 0. Stamp digit cel 64 ("0") at (465,331) (Problems) and
   (465,354) (Correct). The Time field stays empty until its first tick (§D.5).
9. Start the first set.

## D.3 One set

At the start of each set: build the fact deck from the **current** customisation and set
N = 5·(byte0 & 3) + 5 (5/10/15/20) — chapter 05 §2.2. Correct-in-this-set := 0. Then N times:

1. Wait 49 cs.
2. Button press: 8604 cel 0, SFX 1705 (priority 2), 16 cs, 8604 cel 1.
3. Problems += 1 and redraw it (§D.5). It becomes visible with the question.
4. Wait 49 cs.
5. Pose the question and read the answer (chapter 05 §2.3–2.4, Part A §4). On the first question of
   the session, New Player Hint dialog 1009 appears ≈ 3 s after the question unless the player has
   started typing (only if hints are on; once per session).
6. Correct: praise text + SFX 1744, wait 100 cs, **Correct += 1 with SFX 1747** (priority 2) and
   redraw, wait 100 cs, erase. Wrong: exactly as Part A §4 (SFX 1761, "That's incorrect.", 201 cs,
   red answer, 302 cs). No points either way.
7. Clear the question area.

There is **no TV static between questions** and no prompt between questions. A set cannot be cut
short (Esc does not abort it, §D.6).

## D.4 End of a set

Runs right after the last question has been cleared.

1. Stop the Time ticks. Show **PAUSE**: stamp cel 76 (80×21) at (402,303) over the Time field (it
   becomes visible with the next screen update).
2. **Perfect set** = every answer of this set was correct.
   * Perfect: if Music is on, start `victory.mid` (SND TEXT 1757) once. Play **script P** (below).
     If Music is on, then wait until victory.mid has ended (≈ 10.5 s; the script lasts 8.5 s, so the
     final pose is held ≈ 2 s longer).
   * Otherwise: k = scaled(2). k = 0 → WAVE **1734** (3.9 s) with **script A**; k = 1 → WAVE **1718**
     (1.9 s) with **script B**. The WAVE is treated as music: it plays only if **Music** is on, and
     then plays even when Sound Effects are off (SFX are switched on for the jingle and restored
     afterwards). Start the WAVE (priority 2, once), play the script, then (Music on) wait until the
     WAVE has finished. With Music off the try-again animation is silent.
3. **Continue button**: stamp SSO1 ASEQ 1012 cel 0 (114×30 "Continue") at **(193,180)**, i.e. inside
   the TV picture. A click anywhere on the canvas counts as Enter. Present. Wait for **Enter or Esc —
   both continue** (here Esc does *not* open the Exit confirmation). All other keys are ignored.
   Keys pressed during the animation stay queued and can end the wait at once (M). The menus work
   while waiting. Remove the click region. There is no "Enter to Continue" text.
4. TV static (as at the start). It covers the final pose and the Continue button.
5. Remove PAUSE: fill (403,304)–(480,322) with colour 0. Time ticks resume; the Time digits come
   back at the next tick.
6. Start the next set (§D.3). Problems, Correct and Time keep counting for the whole session.

### D.4.1 Script playback
A script is a list of frames `(cel, col, hold)`. For each frame, in order:
1. Erase the previous frame (exception noted in script P) by restoring its rectangle from the
   background buffer (the cel's own width × height at its position).
2. Draw the cel (SSO1 ASEQ 200) with its top-left at x = COL[col], y = 79, **unmirrored**.
3. Present, then hold for `hold × 12 cs`.
The last frame is never erased (the TV static later covers it).
COL: 20→128, 22→141, 24→153, 26→166, 28→179, 30→192, 32→205, 34→217, 36→230, 38→243, 40→256,
42→269, 44→281, 46→294, 48→307.
Cel sizes: 0, 1 = 75×104 (walk); 34 = 101×121, 35, 36 = 77×123, 37–39 = 101×76 (jump/flip);
47 = 112×105, 48 = 110×104, 49 = 112×104 (arms raised, M); 18 = 65×100 (standing pose).
Hold is 1 for every frame except the last (hold 10).

**Script P (perfect, 62 frames, 852 cs):**
| frames | cels | col | |
|---|---|---|---|
| 1–15 | 0, 1, 0, 1, … (frame 1 = cel 0) | 20, 22, …, 48 | walks right |
| 16 | 0 | 48 | |
| 17–28 | 34, 35, 36, 37, 38, 39, 34, 35, 36, 37, 38, 39 | 46, 44, …, 24 | flips to the left |
| 29–35 | 34, 35, 39, 38, 37, 36, 34 | 22, 24, …, 34 | flips to the right |
| 36 | 0 | 34 | |
| 37–41 | 47, 48, 49, 48, 47 | 28 | |
| 42 | 0 | 34 | |
| 43–47 | 47, 48, 49, 48, 47 | 28 | |
| 48–49 | 0, 0 | 34 | |
| 50–54 | 47, 48, 49, 48, 47 | 34 | frame 50 erases frame **34**'s rectangle, not frame 49's (data quirk; a sliver of frame 49 may show for one frame) |
| 55 | 0 | 34 | |
| 56–60 | 47, 48, 49, 48, 47 | 34 | |
| 61 | 0 | 34 | |
| 62 | 18 | 34 | final pose, hold 10 |

**Script A (try again k = 0, 21 frames, 360 cs):** frames 1–12 cels 34, 35, 36, 37, 38, 39, 34, 35,
36, 37, 38, 39 at cols 20, 22, …, 42; frame 13 cel 0 col 42; frames 14–19 cels 34, 35, 36, 37, 38, 39
at cols 42, 40, 38, 36, 34, 32; frame 20 cel 0 col 32; frame 21 cel 18 col 32 (hold 10).

**Script B (try again k = 1, 25 frames, 408 cs):** frames 1–5 cels 47, 48, 49, 48, 47 col 34; 6 cel 0
col 34; 7–11 cels 47, 48, 49, 48, 47 col 28; 12 cel 0 col 34; 13–17 cels 47, 48, 49, 48, 47 col 34;
18 cel 0 col 34; 19–23 cels 47, 48, 49, 48, 47 col 28; 24 cel 0 col 34; 25 cel 18 col 34 (hold 10).

## D.5 Counters and Time display

All digits are hall cels 64 + d (8×13), stamped into the frame, right-aligned, no leading
zeros. Positions are the left edges of the digits, units first.

| field | digits x (units, tens, hundreds, thousands) | y | updated |
|---|---|---|---|
| Problems (0–9999) | 465, 451, 437, 423 | 331 | after the button press of each question |
| Correct (0–9999) | 465, 451, 437, 423 | 354 | after each correct answer (with SFX 1747) |
| Time minutes (0–999) | 433, 419, 405 | 308 | each tick; drawn **only when minutes ≥ 1** |
| Time seconds (00–59) | tens 451, units 465 (always two digits) | 308 | each tick |
| Time colon | two 2×2 squares, colour 137, at (445,311) and (445,316) | | each tick |

* **Ticks** happen only while an answer is being typed: while waiting for a key, a tick fires when
  74 cs have passed since the last one; after each key is handled, a tick fires if the deadline has
  passed and the next one is set 101 cs later. The display therefore runs fast (≈ 1.35×), and the
  first wait of every question ticks at once because the deadline went stale during the feedback.
  A tick adds one second (59 → 0 carries into minutes) and presents the screen. The very first tick
  shows " :01" (no minute digits).
* PAUSE (§D.4 step 1, menu commands, dialogs, hints, Esc) stops the ticks and shows cel 76 at
  (402,303); resuming fills (403,304)–(480,322) with colour 0.
* Wrap-arounds (unreachable in practice; the original's own clears are broken — a reimplementation
  should simply clear the field): Problems/Correct 9999 → 0 (the original's clear rectangle is
  off-canvas, so "9990" would show, M); minutes 999 → 0 (the original clears only the row y 308).

## D.6 Input during a question

* Keys (chapter 05 §2.4): digits (max 3); Enter submits; Backspace/Delete remove the last digit;
  `c` clears the entry; other keys request a beep (SFX 1702, which is not loaded, so probably
  silent, M).
* **Esc**: pause, then the Exit confirmation with no player signed in (DIALOG **23013**). Yes → quit
  the program. No → resume; the typed entry is kept.
* **Keypad** (ASEQ 8540 at (140,301), clickable while a question waits for input). Pressing the mouse
  anywhere on the keypad plays SFX 1756 (priority 2). While the button is held, the key under the
  pointer shows a 3-pixel frame inside its rectangle (M: colour). Releasing on the same key types it:

  | key | left, top, width × height | types |
  |---|---|---|
  | 7 8 9 | x 204 / 225 / 246, y 305, 15 × 17 | the digit |
  | 4 5 6 | same x, y 329, 15 × 17 | the digit |
  | 1 2 3 | same x, y 353, 15 × 17 | the digit |
  | 0 | 183, 353, 15 × 17 | 0 |
  | C | 144, 330, 15 × 17 | `c` (clear) |
  | ENTER | 144, 305, 54 × 18 | Enter |

* No other click areas (no Station button).

## D.7 Menus and leaving

Menu set **Drill**. Every command pauses (PAUSE shown) and resumes afterwards.

| id | item | behaviour |
|---|---|---|
| 1300 | File ▸ Return to Sign In | DIALOG 23007; Yes → back to the sign-in screen (everything allocated for the drill is released). Nothing is saved. |
| 1302 | File ▸ Exit | DIALOG 23013; Yes → quit. Same for the window close box. |
| 1400 | Options ▸ Sound Effects | as in the game (22000 / 22001) |
| 1401 | Options ▸ Music | as in the game (22002 / 22003) |
| 1403 | Options ▸ Problem Customization… | DIALOG 220 (§D.8) |
| 1500 | Help ▸ Playing Drill for Skill… | help dialog 12600 (1 page) |

The session has no other exit: it runs sets until the player chooses Return to Sign In or Exit
(or answers Yes to the Esc confirmation). Leaving must also stop the Time ticks (the original forgets
to, see D.9).

## D.8 DIALOG 220 "Problem Customization"

* Opens on a working copy of the current customisation. Initial state: radio 121–124 = byte0 & 3
  (5/10/15/20 problems per set); checkboxes 101–104 = operations (+, ×, −, ÷), 105–117 = numbers
  0–12, 118–120 = formats `3 + 5 = ?`, `? + 5 = 8`, `3 + ? = 8` (bit layout: chapter 05 §2.1).
* Radio buttons only select the set size (unlike DIALOG 221 they do not load defaults).
* **Reset**: the working copy becomes the tier defaults for (rank ÷ 2) as in §D.1 step 2; all controls
  are refreshed. The dialog stays open.
* **OK**: the working copy is rebuilt from the controls (all other bits cleared; byte0 low bits = set
  size index) and validated, first failure wins: no operation → DIALOG 140; no number → 140; no
  format → 140; the only operation is × (or the only one is ÷) and the only number is 0 → DIALOG 141.
  On failure the dialog stays open. Otherwise it closes and the copy becomes the current
  customisation (in memory only; never saved).
* **Cancel**: no change.
* When changes take effect: the **format** choice is read for every new question, so it applies from
  the next question, even within the current set. **Operations, numbers and set size** apply from the
  **next set** (the deck and N are built at the start of each set). The current set is not restarted
  and its counters are not reset.

## D.9 Original-program bug (do not reproduce)

Return to Sign In leaves the drill's "Time is running" flag set. If a game is started afterwards, the
first Telly drill of that game would tick the Drill-for-Skill timer and draw the elapsed time digits
and colon over the HUD clock (y 308, x 405–465) until that drill ends. Clear the flag when leaving
Drill for Skill.

## D.10 Corrections to other chapters (these override the earlier text)

1. Part A §4: the "‹Enter› to Continue" text at (200,202) is never drawn. Between Drill-for-Skill sets the
   ASEQ 1012 Continue button at (193,180) is used (§D.4).
2. Part A §4: TV static is not shown after each problem. It is shown only at drill start, after each
   DfS set, and before "No Code" when a Telly drill fails.
3. Part A §4 (both drills): the numeric keypad SSO2 ASEQ 8540 is stamped at (140,301) and is clickable
   (§D.6 table). This answers open question 15.
4. 03 §2.7 and the audio table entry for 1757: `victory.mid` plays only after a **perfect** DfS set and
   only if Music is on. Other sets play WAVE 1734 or 1718, also only if Music is on (§D.4).
5. 03 §2.2 pseudo-code: RunDrill() does not return on Esc. Esc opens the Exit confirmation; DfS ends only
   through Return to Sign In or Exit (§D.6, §D.7).
6. 05 §2.6 "uses the player's DfS customization record" and 03 §2.3 "reset to the defaults for level 0":
   replaced by §D.1 step 2 (tier defaults; the stored DfS record is never read or written).
7. 03 sound table: add 1705 (drill button press), 1747 (also the DfS Correct counter), 1718 and 1734
   (DfS try-again jingles), 1756 (keypad click).
8. §4.2 / Continue prompts using ASEQ 1012: the click region is the whole canvas, not the button (M).
9. 03 NumLock note: NumLock is forced **on** during drills (not off).

**v1.17 adjustments to §D.3:**

* §D.3 step 1 "Wait 49 cs" → "Before the **first** problem of the set only: wait 49 cs." For problems
  2..N the button press follows the previous problem's clear (step 7) at once.
* §D.3 step 2: as Part A §4 "Button press": the 16 cs run from just before cel 0 is drawn. SFX 1705
  starts after cel 0 is shown. Cel 0 must be presented before the wait.
* §D.3 step 6 (Correct): the praise text is in **FS** (Part A §4). The erase is the question-area
  clear followed by the praise box fill.
* §D.3 step 7 after a wrong answer: this clear becomes visible together with the next press's cel 0.

**v1.17 corrections to Part A §4 / Part B (already applied in Part A §4 above):**

1. A §4 "TV static (at start and after each problem)": not after each problem (D.10 #2 already says so;
   the A §4 bullet still says otherwise).
2. A §4 "Button press: cel 0, delay 40, sound, cel 1": the order is timer start → cel 0 (presented) →
   sound → wait out the remaining time of the 16 cs → cel 1. (Not "wait 16 cs after the sound".)
3. A §4 question line "its width in the layout is the width of one space": the slot width is the width of
   the 3-character buffer string, recomputed at every redraw. Also the statement must say plainly that
   pixel widths are added to d.u. positions without conversion.
4. A §4 "(M: this rectangle ends above the text line; check against the real game)": wrong. With the v1.9
   baseline rule the line's ink is rows 140–157, inside the clear (y 80–159).
5. A §4 Backspace "erases the last digit": the box starts at the field's left edge (fx), and the whole line is
   then redrawn.
6. A §4 Correct "in the current font (FL)": it is FS. The order "delay 240 … delay 240, then erase its box" also
   needs the question-area clear before the box erase.
7. A §4 Wrong: the clear after the 302 cs wait was missing.
8. Part D §D.3 step 1: the 49 cs wait comes only before the first problem of a set.
9. Part B (won-drill sequence, step 1) "The question area and the message area are cleared": only the
   question area is cleared. Also "SSO1 ASEQ 8604" there should be SSO2 ASEQ 8604.
