# 04 — Action game, world, and game loop

Scope: the real-time part (hall, rooms, player, Telly, LiveWire, zapper, energy machine, clock), the
main loop and its state transitions, per-rank difficulty, the RNG, and the vestigial debug/beta menus.
Drill questions, decoder logic, word problems (MTHT), save files and graphics formats are specified by
other documents; they are only referenced here.

Confidence: **H** = read directly from code/data; **M** = inferred from code with some interpretation;
**L** = guess.

All tables quoted here are read from data at run time (BINA 5000) or are listed in full in this chapter /
Appendix A (`10-appendix-tables.md`).

---------------------------------------------------------------------------------------------------

## 1. Units and coordinate system (H)

* **Time base**: every timer uses `now_cs = timeGetTime() / 10` (centiseconds, 32-bit).
* **Horizontal unit = "column"**. The playfield is 38 columns wide. A column is ~12.8 px: sprites are placed
  at `x_px = COLPX[col]` with `COLPX[i] = round(i*12.8)` (table below; negative columns mirror:
  `COLPX[-i] = -COLPX[i]`). Background/hot-spot drawing and scrolling use exactly 13 px per column
  (`col*13 + 9`). The reimplementation should keep both conventions to match the original look.
  `COLPX = 0,13,25,38,51,64,77,89,102,115,128,141,153,166,179,192,205,217,230,243,256,269,281,294,307,320,333,345,358,371,384,397,409,422,435,448,461,473,486,499,512`
* **Vertical unit**: object y is stored in "design units"; screen `y_px = round(y*192/100)`
  (helper rounds half up). A similar helper `round(v*160/100)` exists for some x values in UI code.
  These factors (1.6 = 512/320, 1.92 = 384/200) are a legacy of the 320×200 DOS original: design units
  are 320×200-space values scaled to the 512×384 canvas.
* **Playfield rectangle** (canvas coordinates, 512×384 canvas): x 9..503 (494 px wide), y 35..283
  (248 px high). Everything else is HUD.
* Object x positions are in playfield columns *relative to the left edge of the visible screen*; the
  hall additionally has a scroll offset (see §3).

## 2. Random number generator (H)

The game uses the Borland C RTL generator, unmodified:

```
srand(seed16): state = seed16 (32-bit, high word 0)
rand():        state = state * 0x015A4E35 + 1   (mod 2^32)
               return (state >> 16) & 0x7FFF
```
Helpers built on it:
* `random(lo, hi) = lo + rand() % (hi - lo + 1)` (inclusive).
* `scaled(n) = (rand() * n) / 32768` (0..n-1) — used for shuffles/selection.

Seeding sequence:
1. Program start: `srand(low16(time(NULL)))`.
2. New game (not a continued one): `seed = rand()` (0..32767) is the **game seed**, stored in the save block
   (offset 0 of the 0x63-byte game record), then `srand(seed)`.
   (A beta hook would use a fixed seed if a "seed override" flag were set; the flag is never set in retail.)
3. With the game seed active, exactly 10 values are drawn: `problemPick[i] = scaled(10) + 1`, i = 0..9
   (one per math-challenge object; consumed by the word-problem module).
4. **Immediately afterwards the generator is reseeded with `low16(now_cs)`.** Everything else — which object
   in each room holds a challenge, word-problem variants, decoder picture shuffle, hideout room, energy
   machine location, all real-time AI — comes from this time-based stream. So the game seed does *not*
   reproduce a layout by itself (it only fixes the 10 problem picks). (H)
5. Continuing a saved game: `srand(savedSeed)`; the world layout (challenge flags, clues, hideout) comes from
   the save record, but the energy machine is re-placed using this stream (it is not saved). (H/M)
6. The main loop calls `rand()` once per frame and discards it, and the AI draws numbers every frame, so
   real-time behaviour is not reproducible from the seed.

New-game draw order (all after step 4): problem variants for 10 objects (question `scaled(4)` on this generator; operation by repeating RNG-B
`pm_range(0,3)` until an allowed one is found — chapter 05 §3.6) → challenge-object choice (§9.4) →
decoder setup (20-picture Fisher-Yates-like shuffle using `scaled(i)`, 5-room permutation, hideout) →
energy machine slot (§4.5).

## 3. World layout (H)

### 3.1 Areas
The world is 16 **areas**, each a 38-column screen with up to 4 **hot-spot slots** (BINA resource 5000 in
SSO1.DAT, bytes 0x000-0x37F, 16 × 4 × 14 bytes; copied to RAM at program start and edited at run time).

| area | role |
|---|---|
| 0-4 | Hall, floor 1 (5 consecutive screens, one continuous scrolling corridor of 190 columns) |
| 5-9 | Hall, floor 2 (same) |
| 10 | NEWS ROOM |
| 11 | unused room slot (only an exit door; unreachable) |
| 12 | GAME SHOW ROOM |
| 13 | CARTOON ROOM |
| 14 | EQUIPMENT ROOM |
| 15 | SOUND ROOM |

Room index (0..4, decoder/name order NEWS, GAME SHOW, CARTOON, EQUIPMENT, SOUND) → area: `10,12,13,14,15`.

Hot-spot record (7 × int16): `type, col, y, field3, cel, field5, field6`. (`y` is a canvas pixel y, unlike object y.)
* `type`: −1 empty, 0 plain (locked) door, 1 room door (hall) / exit door (room), 2 elevator, 6 floor-end wall,
  9 energy supply machine (placed at run time), 11..20 math-challenge object.
* `col`: left column within the area. `y`, `cel`: where/what to draw (design-unit y, cel id in the hall/room
  sprite set). For type-1 hall doors cel 134 (the closed door, 125×130) is drawn at y 101; the name plate is the slot's own cel (117–121) at y 76 (corrected in v1.13).
* `field3`: used as the hot spot's right end (`width = field3 − col`). For room objects this is a real column
  (width 3); for hall entries the values are pixel-like (e.g. 294) so the zone effectively extends to the next
  hot spot (see §3.3). field5/field6: unused by the action code (field5 equals `COLPX[col]` for hall doors).

### 3.2 Hall contents (world column = area*38 + col)

| area | slot0 | slot1 | slot2 |
|---|---|---|---|
| 0 | wall @0 | **elevator** @6 | door @23 (EM candidate) |
| 1 | door @3 | **NEWS door** @14 (cel 117) | door @25 (EM) |
| 2 (start) | door @3 | door @14 | door @31 |
| 3 | door @4 | **GAME SHOW door** @15 (cel 118) | door @25 (EM) |
| 4 | door @4 (EM) | **elevator** @18 | wall @36 |
| 5 | wall @0 | **elevator** @6 | door @24 (EM) |
| 6 | door @4 (EM) | **CARTOON door** @20 (cel 119) | (empty) |
| 7 | door @4 (EM) | **EQUIPMENT door** @17 (cel 120) | door @30 |
| 8 | door @5 | **SOUND door** @11 (cel 121) | door @24 (EM) |
| 9 | door @5 (EM) | **elevator** @18 | wall @36 |

Elevators link area 0↔5 (left ends) and 4↔9 (right ends). EM = energy-machine candidate slot (§4.5).

### 3.3 Hot-spot detection (H)
Player occupies columns `[x, x+6)` for this purpose. Only the slots of the *current* area are tested
(hall: area = playerWorldCol / 38; room: current room area). For each slot in order 0..3 with type ≠ −1:
`start = col` (hall: `area*38+col`, compared with player world col; room: compared with screen col),
match if `start <= px < start+width` or `start > px and px+6 > start`. **The last matching slot wins.**
Because hall widths are huge, in practice the hall hot spot is "the last door/elevator whose start column is
less than player+6 within this screen". Actions then auto-walk the player to the exact spot, so zones are
deliberately generous.

### 3.4 Rooms (H)
Each room area: slot0 = exit door (type 1, cols 5..13). Challenge objects (cel 203 = invisible marker; the
objects are part of the room picture), y=60, width 2-3:

| room | objects (id @col) | challenge type (object index = id−11) |
|---|---|---|
| NEWS (10) | 12 @15, 11 @26 | 0 Map, 1 Bar Graph |
| GAME SHOW (12) | 13 @16, 14 @24 | 2 Prize is Right, 3 Spinner |
| CARTOON (13) | 19 @15, 20 @33 | 8 Line Graph, 9 VCR/tapes |
| EQUIPMENT (14) | 18 @16, 17 @34 | 6 Monitor, 7 Equipment list |
| SOUND (15) | 16 @16, 15 @24 | 4 Great 8, 5 Volume |

(Type names from the beta "Object" menu order; mapping of index→type: M, cross-check with chapter 06.)
Rooms do not scroll. Player screen column is clamped to 1..33 everywhere.

### 3.5 Scrolling in the hall (H)
The hall view keeps the player between screen columns 6 and 26. Each frame, before moving, if the movement
`n = |dx|` would take the player left of col 6 (moving left) or to col ≥ 26 (moving right), the view scrolls
by the excess (`6−(x−n)` or `x+n−26` columns) and the player's, Telly's and the trick's screen x are shifted
back by the same amount. Scrolling stops at the floor ends: not left of `floorStartArea*38` (areas 0/5) and
not beyond scroll = `endArea*38 − 1` (areas 4/9); the player then walks toward the screen edge. Scrolling
shifts the existing bitmap by `n*13` px and draws new background columns; hot-spot cels entering the view
are drawn as they appear.

Start of a new game: area 2, scroll = 76 columns (area 2*38), player screen col 18 (world col 94), y 81,
facing right, standing.

## 4. Game objects

### 4.1 Object table (H)
Six objects, 18 bytes each (initial values from BINA 5000 bytes 0x70E-0x779):

| # | object | init x | y | active | hit offset | hit width |
|---|---|---|---|---|---|---|
| 0 | zap bolt | 0 | 0 | 0 | 0 | 13 (unused) |
| 1 | LiveWire | 20 | 113 | 0 | 1 | 9 |
| 2 | room prop: rotating security camera (cel 10003..10007) | 19 | 24 | 0 | 0 | 4 |
| 3 | player | 20 | 81 | 1 | 1 | 4 |
| 4 | Telly | 0 | 77 | 0 | 1 | 6 |
| 5 | Telly's trick | 0 | 0 | 0 | 0 | per type |

Fields: x (column), y (design units), current cel, two draw params, active flag (1 active, −1 = erase and
deactivate next frame, 0 off), hit offset, hit width.

**Collision is 1-dimensional (x only)**: `a = xA+offA`, `b = xB+offB`;
`gap = (a <= b) ? b − (a + widthA) : a − (b + widthB)`; touching when `gap <= 0`. If either object is inactive,
gap = 39. Y is never compared; vertical avoidance works only through the jump state's invulnerability.

### 4.2 Animation states (H)
Shared table of 35 states (BINA 5000 bytes 0x380-0x70D, 26 bytes each: frame count, current frame, up to 9
cel ids, tick counter, ticks per frame). Advancing an object one step: `x += dx; y += dy;` draw the current cel;
decrement tick counter; when it reaches 0 reload it and `frame = (frame+1) % count`. The player's dx also
updates the world column. The full raw table is in Appendix A. Meaning:

| state | frames | cels | use |
|---|---|---|---|
| 1 | 1 | 0 | player stand |
| 2 | 8 | 1-8 | player walk |
| 3 | 5 | 9-13 | player turns to face the back wall (approach/enter), used with dy −2 |
| 4 | 5 | 14-18 | player turns back to the front, dy +2 |
| 5 | 4 | 19-22 | player goes through a door/elevator, dy −4 |
| 6 | 4 | 23-26 | player comes out of a door/elevator, dy +4 |
| 7 | 3 | 47-49 | player zap pose |
| 8 | 8 | 34,35,36,37,38,39,35,34 | **jump/flip** (invulnerable) |
| 9 | 9 | 27,28,29,28,27,30,31,32,33 | player falls (hit by a high trick) |
| 10 | 4 (2 ticks) | 100-103 | Telly walk |
| 11 | 4 (2 ticks) | 104-107 | Telly idle |
| 12 | 6 | 50-55 | zap bolt |
| 13 | 3 | 10000-10002 | LiveWire crawl |
| 14 | 8 (4 ticks) | 10003-7 ping-pong | room prop |
| 15 | 5 | 9,10,11,12,203 | player turns to energy machine |
| 17-20 | 5-6 | 105,113,114,… | Telly throw animation for trick types 22-25 (state = type−5) |
| 22-25 | 2-3 | 115-116 / 183-185 / 189-190 / 193-194 | trick projectiles (types 22-25) |
| 26 | 7 | 40-43,43,43,43 | player shoved (hit from behind) |
| 27 | 8 | 99×4,30-33 | player knocked down |
| 28 | 9 | 44,45,46,44,45,46,44,35,35 | player gets up |
| 29 | 5 | 14-18 | player turns back from energy machine |
| 30 | 4 | 10008,10009 ×2 | player shocked by LiveWire |
| 31 | 5 | 10010-10014 | LiveWire disappears (zapped) |
| 32 | 5 | 108-111 | Telly dematerialises |
| 33 | 5 | 111-108 | Telly materialises |
| 34 | 8 | 100/112 alternating | Telly flashing (zapped while unlit) |

## 5. Player (H unless marked)

### 5.1 Controls
Polled each frame with async key state:

| input | effect |
|---|---|
| Left / Right arrow | face that way and walk (state 2) |
| Up | action (§5.3) |
| Up+Left or Home | flip left |
| Up+Right | flip right (no single-key equivalent) |
| Space | zap |
| Tab (char) | decoder (clock paused) — shows hint 14 first time |
| Esc (char) | "exit program?" confirmation (clock paused) |

Mouse: while the left button is held inside the playfield, the cursor shape defines the command. Let
`pl = COLPX[playerX]`, `pr = pl + spriteWidth`, `yT = round(91*1.92) = 175`:
mouse left of the player: above yT → flip-left cursor; yT..yT+50 → walk-left; below yT+50 → walk-left if
facing right, **zap** if facing left. Mirror image to the right. Over the player and above yT → Up cursor.
Otherwise no command. (Cursors: GROUP_CURSOR 128–134, see chapter 03 §6.3.)

### 5.2 Movement state machine
Variables: `state`, `remaining` (frames), `dx` (relative to facing), `dy`, `facing` (±1).

`setState(s, dur, dx, dy)`: ignored if the current state is > 2, `remaining > 0` and `s < 9` (actions can't be
interrupted except by hits ≥ 9). `dur = −1` means "the state's frame count". Any state other than 1 sets the
"player has moved" flag (enables Telly; suppresses hint 0).

Per frame (input handling):
```
if left:  facing = -1; if state != 2: setState(2, 1, 1, 0)
if right: facing = +1; if state != 2: setState(2, 1, 1, 0)
if flipRight: facing = +1; setState(8, -1, 2, -12)
if flipLeft:  facing = -1; setState(8, -1, 2, -12)
if up: action()
if space: zap()
if !left && !right && state == 2 (first frame only): setState(1, 1, 0, 0)
```
Per frame (update, only if state ≠ 0):
```
target = x + dx*facing
if target <= 1: dx = x-1  elif target >= 33: dx = 33-x        (screen col range 1..33)
if dx == 0 and state == 2: setState(9, -1, 0, 0); remaining -= 1; bump sound
      # walking into screen col 1/33 (e.g. a floor end) makes the player trip and fall (state 9)
if state == 8 and remaining == framecount/2: dy = -dy          (apex)
advance anim of object 3 by (dx*facing, dy)
if state > 2:
   remaining -= 1
   (state 9 at remaining==3: thud sound)
   if remaining < 1:
      if state in (9, 27): state 28 (get up) with sounds
      elif state in (26, 28) or state == 8: landing sound, state 1
      else: state 1
background music re-chosen per frame (§14.2); there is NO footstep sound
```
Speeds: **walk = 1 column per frame** (~12.8 px × ~14 fps ≈ 180 px/s). **Jump** (Up near Telly/trick):
state 8, dx 0, dy −12 for 4 frames then +12 for 4 frames → apex 48 units ≈ 92 px, 8 frames ≈ 0.56 s.
**Flip** = same arc with dx 2 → 16 columns forward. The player is **invulnerable to every collision while in
state 8**. Collision box: columns `[x+1, x+5)`.

### 5.3 Up / action
```
if state > 2: ignore
spot = hotspot()                         (§3.3)
if spot not in (door=1, elevator=2) and (Telly active or trick active): jump(); return
if spot in (1,2) and (touching Telly or touching trick): jump(); return
switch spot:
  2: elevator ride (§5.5)
  1: hall -> enter room (§5.4); room -> exit to hall
  9: energy machine (§4.5)
  default (plain door, wall, nothing, room object):
     facing = right; play state 3 with dy -2 (step toward back wall)
     if in room and spot is object 11..20:
         idx = spot-11
         if challengeFlag[idx] == 1: pause clock; hint 10 (calculator off) / 11 (on);
             remove LiveWire if present; run math challenge (word-problem module);
             redraw room; resume clock; maybe respawn LiveWire (§6.2)
         if challengeFlag[idx] & 2: hint 7 (solved, flag 2) or hint 8 (attempted, flag 3)
     play state 4 with dy +2 (step back); restore facing
```
So in rooms Up never jumps (Telly does not exist there); it approaches objects.

Scripted sequences (approach, doors, elevator, energy machine) run synchronously, one animation step per
7 cs, with no input; they call the normal object update with Telly spawning disabled.

### 5.4 Entering and leaving rooms (room doors)

Notation, as in §5.5: `scroll` is the world column at the left edge of the screen. `px(col) = COLPX[col]`.
`ypx(y) = round(y·1.92)`: 81→156, 79→152, 77→148, 75→144, 73→140, 71→136, 67→129, 63→121,
59→113, 55→106. "Step" means one update of all moving objects followed by a present.
"Paced step" = the step starts a 7 cs deadline, updates and presents, then waits for the deadline.
Player cels are SSO1 ASEQ 200. Player art faces right, so cels are unmirrored while facing right.

**Door geometry (hall).** Each room door is the type-1 slot of its hall area. Its world column is
`doorWorld = area·38 + slot.col`. On screen the closed door cel **134** (125×130) is at
**doorX = 9 + 13·(doorWorld − scroll), doorY = 101**, and the name plate (117–121) is at (doorX, 76).
Cels 135 (half open) and 136 (open, black interior) use the same rectangle. They are opaque except at the
edges, so each one fully replaces the previous one.

**Door animation DA(dir, playerCel)**: 3 steps.
```
for s = 0,1,2:
   door cel = 134+s (opening)   or 136−s (closing)
   stamp the door cel at (doorX, 101) into the scene        (it persists, like scenery)
   Telly and trick take one normal AI/movement step (no spawning, no collisions)
   draw the player with the fixed cel playerCel at (px(x), ypx(y)), unmirrored, over the door
   draw Telly (+ belly overlay) and the trick over everything
   present
```
* Opening uses playerCel **13** (back view, last cel of the turn). Closing uses playerCel **18**.
* Step 0 re-stamps the cel that is already showing (134 when opening, 136 when closing), so only
  steps 1 and 2 change the picture.
* The original puts **no delay** between these 3 steps, nor before the next scripted step: they run at
  render speed. Recommended: **7 cs per step** (not specified by the original).
* Nothing is clipped against the door frame. The player is never hidden by the door: he is always drawn
  on top of it. The door is never drawn with the viewport clip, but in practice it is always fully inside
  the playfield (doorX ranges from 61 to 321).
* No sound.

**Rooms have no door animation.** The room's exit door is the permanently open doorway picture
(SSO1 ASEQ 1022 at (80,92), chapter 07 Part A §3), and the player is drawn over it.

**Hot-spot zones** (from §3.3; the last matching slot wins):

| room | hall area | slot col | Up works at local column | walk target (local) | room area / picture |
|---|---|---|---|---|---|
| NEWS | 1 | 14 | 9–19 | 16 | 10 / 1001 |
| GAME SHOW | 3 | 15 | 10–19 | 17 | 12 / 1003 |
| CARTOON | 6 | 20 | 15–37 | 22 | 13 / 1004 |
| EQUIPMENT | 7 | 17 | 12–23 | 19 | 14 / 1005 |
| SOUND | 8 | 11 | 6–17 | 13 | 15 / 1006 |

Local column = player world column − area·38.
In a room, the exit door (slot 0, columns 5..12) is the hot spot for player x = 1–9 (NEWS, CARTOON)
or 1–10 (GAME SHOW, EQUIPMENT, SOUND). Further right, the room's first object wins.

#### 5.4.1 Entering a room (hall → room)
Trigger: Up while the hot spot is a room door, the player state is ≤ 2, and the player is not touching
Telly or the trick (otherwise Up jumps, §5.3). The whole sequence is synchronous. It takes no input and
checks no collisions. Telly is never spawned.

1. **Sound.** Stop all sounds, including the music. Turn music updates off. Start MIDI **1717 `enter.mid`**
   once (2.80 s long).
2. **Auto-walk.** target = slot.col + 2 (local column). n = |local − target|. The player walks n steps
   (state 2, walk cels from cel 1, one column per step) toward the target **without changing facing**.
   If he faces away from the target, he walks backwards with mirrored walk cels. The normal hall-scroll
   rule applies: walking left keeps him at screen column ≥ 6, walking right at ≤ 26, and the view
   scrolls instead. Telly and the trick keep moving. The original has no delay between these steps.
   Use **7 cs per step**, as in normal walking.
   Result: the player stands at screen column `pc = doorCol + 2`, where `doorCol = doorWorld − scroll`.
3. Facing = right. **State 3**: 5 paced steps, cels 9, 10, 11, 12, 13 at y 79, 77, 75, 73, 71.
   (dy is applied before each step is drawn.)
4. **Door opens**: DA(open, cel 13). The player stays in his back-view cel 13 at y 71.
5. **State 5**: 4 paced steps, cels 19, 20, 21, 22 at y 67, 63, 59, 55. He walks away into the black
   doorway and gets smaller. The open door (136) stays visible behind him. He is not clipped or hidden.
6. **Hold.** Nothing is updated or redrawn: the last picture stays frozen (cel 22 at y 55 in the open
   doorway; Telly and the trick frozen where they are) **until `enter.mid` has finished**. That is
   2.80 s after it started, about 2.1 s of hold after a short walk. If music is off or the tune is not
   playing, there is no hold.
7. **Switch to the room.** Telly and the trick are removed (they are not erased: the screen is rebuilt).
   Save the player's hall world column and screen column. The scroll is not changed.
   Area = room area. Player x = 6 (exit-door column 5 + 1). y stays **55**. Facing stays right.
   Stop all sound effects. Rebuild the screen: panel, clock, energy and score, the room picture at
   (10,36), the doorway 1022 at (80,92), and the Decoder button. Select the room sprite set (ASEQ 210).
   Draw the player cel **23** at (77, 106). Switch the room prop on (camera, ASEQ 210 cel 3, at (243,46)).
   **Roll for the LiveWire now** (§6.2; with the player at x 6 it starts at x 30 moving left).
   Present. (The original goes straight on to step 8, so this picture is visible only for an instant.)
8. **State 6**: 4 paced steps, cels 23, 24, 25, 26 at y 59, 63, 67, 71 (px x 77). He steps out of the
   doorway towards the viewer. Then **state 4**: 5 paced steps, cels 14, 15, 16, 17, 18 at y 73, 75, 77,
   79, 81. There is no pause between the two states.
   During these 9 steps the room prop animates. A LiveWire, if one spawned, crawls one column per step
   (30 → 21) with its crackle (1731) on every step. No collisions are checked.
9. Facing right, state 1 (standing). Cel 0 is drawn on the next normal frame. Stop and free `enter.mid`.
   Show **hint 5** if it has not been shown yet. It appears over the picture with cel 18 at y 81.
   Then, if a LiveWire exists, play its crackle 1731 once. Music updates on: the room tune (§14.2)
   starts on the next normal frame.

#### 5.4.2 Leaving a room (room → hall)
Trigger: Up on the room's exit-door hot spot (same conditions; there is never a Telly in rooms).

1. **Auto-walk** to x = 6: n = |x − 6| steps, the same rules as above (facing unchanged, so
   backwards walking is possible; no delay in the original, so use 7 cs). The LiveWire and the room prop keep
   moving. The room music keeps playing.
2. Facing = right. **State 3** (5 paced steps, cels 9–13, y 79→71). Then **state 5** (4 paced steps,
   cels 19–22, y 67→55). He walks into the doorway picture. There is no door animation in the room.
3. Immediately (no hold): stop all sounds, including the music. Back to the hall: restore the saved world
   column and screen column. The scroll is unchanged. Rebuild the hall at that scroll, with the panel and
   HUD (chapter 07 Part A §2). The LiveWire and the room prop are switched off.
4. Stamp the **open door cel 136** at (doorX, 101). Draw the player cel **23** at (px(pc), 106).
   Present (again visible only for an instant).
5. **State 6**: 4 paced steps, cels 23–26, y 59→71, in front of the open (black) doorway. Music updates
   are on, so the hall tune (level1/level2) starts with the first of these steps.
6. **Door closes**: DA(close, cel 18). The door goes 136 → 135 → 134 behind him. The player is
   drawn in cel 18 at y 71, on top.
7. **State 4**: 5 paced steps, cels 14–18, y 73→81.
8. State 1, facing **right**, at screen column pc (= doorCol + 2), world column doorWorld + 2, y 81.
   There is no Telly (it was removed on entry). Normal spawning resumes.

#### 5.4.3 Plain doors (type 0), walls, empty spots
Up there: if Telly or a trick exists anywhere, the player **jumps** (§5.3). Otherwise he remembers his
facing, faces right, plays state 3 (5 paced steps, cels 9–13, y 81→71) and then state 4 (5 paced steps,
cels 14–18, y 71→81), and his old facing is restored. If he faced left, he therefore pops back to
facing left on the next frame. There is no walk, no door cel change and no sound. Plain-door props
(cels 125–133) are never animated.

#### 5.4.4 Telly, trick and LiveWire during door sequences
* Hall, entering: Telly and an in-flight trick keep their normal AI and movement during the walk, the turn, the door
  steps and state 5. Telly can still stop, turn or throw. They are drawn over the player and the door.
  No new Telly spawns. No collision is checked (no hits, no crash). During the `enter.mid` hold they are
  frozen on screen. They are removed when the room is built. Their sounds: the stop-all at step 1 cuts
  everything. Telly's hum is requested again on each of its updates, and a trick launched during the
  sequence starts its loop. The stop at step 7 cuts them.
* Room, leaving: the LiveWire keeps crawling, with its crackle, until step 3. It is then removed without
  touching the player.
* Hall, arriving: no Telly or trick exists.

#### 5.4.5 Sounds (summary)
| moment | sound |
|---|---|
| Up accepted at a hall room door | stop all (and music); MIDI 1717 `enter.mid` once |
| during the entry sequence | only Telly hum / trick loops from the AI; **no door or footstep sounds** |
| end of state 5 | hold until `enter.mid` ends; then stop all effects |
| room arrival steps | LiveWire crackle 1731 per step if it spawned |
| end of arrival | stop/free `enter.mid`; hint 5 (speech) if first time; crackle 1731 once if LiveWire; room music from the next frame |
| leaving, after state 5 | stop all (and music) |
| hall arrival | hall music from the first state-6 step; **no door sound** |

#### 5.4.6 Worked examples (pixel coordinates)
* **NEWS** (area 1, door world 52). Start a game (scroll 76, player world 94), walk left. At world 56
  the player is at screen column 6 with scroll 50. Up → 2 walk steps left (facing left, mirrored cels 1, 2),
  each scrolling the view by 1 → scroll 48, screen column 6, world 54. doorCol = 4 → door at (61,101), plate
  (61,76). Player x = 77.
  Turn: cel 9 (77,152) … cel 13 (77,136). Door steps: 134/135/136 at (61,101) with cel 13 at (77,136).
  Through: cel 19 (77,129), 20 (77,121), 21 (77,113), 22 (77,106). Hold. Room 1001: cel 23 at (77,106),
  then 23 (77,113), 24 (77,121), 25 (77,129), 26 (77,136), 14 (77,140) … 18 (77,156).
  Return: door 136 at (61,101), cel 23 at (77,106); state 6 to (77,136); close with cel 18 at (77,136);
  cels 14–18 down to (77,156). Final: world 54, screen column 6, scroll 48, facing right.
* **CARTOON** (floor 2, area 6, door world 248). After the elevator the player is at area 5 (scroll 190,
  screen column 12, world 202). Walk right: he reaches screen column 26 at world 216, then the view scrolls.
  Stop at world 245 (scroll 219). Up → 5 steps right, each scrolling by 1 → scroll 224, screen column 26,
  world 250. doorCol = 24 → door at (321,101), plate (321,76). Player x = COLPX[26] = 333.
  Turn cels at (333, 152…136). Door steps with cel 13 at (333,136). Through to cel 22 at (333,106).
  Room 1004: arrival at (77,106) → (77,156) as above. Room music `room2.mid` (1746).
  Return: door at (321,101), player at x 333.

---

**Corrections (v1.13) to earlier text**

* §3.1: "For type-1 hall doors an extra sign cel 0x86 is drawn at y=0x65" → cel 0x86 = **134 is the
  closed door** (125×130) at y 101 (pixels). The sign/name plate is the slot's own cel (117–121) at y 76,
  as in chapter 07 Part A §2.
* §5.3, last paragraph: "Scripted sequences … run synchronously, one animation step per 7 cs" → the
  turn/through/come-out states are paced at 7 cs. The auto-walks and the room-door animation steps have
  **no** delay in the original (recommended 7 cs). The `enter.mid` hold is a frozen screen.
* Old §5.4: "door-opening animation … state 5 (dy −4); door sound" → there is **no door sound**.
  `enter.mid` starts before the auto-walk.
  "Exit: … back to the saved hall position, door closing animation, states 6 and 4" → wrong order.
  The hall door is first shown **open**, then state 6, then the door closes, then state 4.
  "Player appears … states 6 and 4 … LiveWire may spawn … Hint 5" → the LiveWire roll happens
  **before** states 6/4, and hint 5 comes after them.
* §14.7 "Scripted door animations: room door (open 134→135→136, close 136→135→134, 3 steps): door first,
  player on top" → correct. Add: the player cel is fixed (13 when opening, 18 when closing),
  Telly/trick are drawn over both, and the steps have no delay.
* Chapter 07 Part A §0 primitive table: "Flag 0x80 = also write into the background buffer" is not
  what the flag does. 0x80 = clip the cel to the playfield viewport (x 9..502). Without it the cel
  is only clipped to the canvas. The scene is a single buffer with save-under sprites (see §5.5), so
  anything stamped into the scene persists whether or not the flag is set.

### 5.5 Elevator

Trigger: Up while the current hall hot spot is the elevator (type 2), and the player is not touching Telly or
the trick (§5.3). Elevators: areas 0 and 5 at slot column 6 (left ends), areas 4 and 9 at slot column 18
(right ends). The whole ride runs synchronously. There is no input and no collision checking. Telly is never
spawned during the ride.

Notation:
* `scroll`: world column at the left edge of the screen.
* `L = scroll − area·38`, where area = the area of the player's world column.
* `px(col) = COLPX[col]`.
* `ypx(y) = round(y·1.92)`: 55 → 106, 71 → 136, 81 → 156.

**Elevator picture position.** The elevator frame cel 122 (222×172) is at x = 9 + 13·(slotCol − L),
y = 68 (spec 07 Part A). The door cels 175–179 (148×120 each) share one rectangle at
**doorX = 9 + 13·(slotCol − L) + 37, doorY = 105**: the frame origin plus (37, 37), centred inside the frame.
Cel 179 is the open cab (interior). 178 is the fully closed door panels, identical to the doors painted in
cel 122. 177, 176 and 175 are the panels progressively more open, and are transparent where open.
Panels are always drawn over 179.

Door-animation step D(dir, playerCel, playerOnTop), 4 steps, each step **9 cs** (start to start):
```
step s = 0..3:
  draw 179 at (doorX, doorY)
  if not playerOnTop: draw player cel playerCel at (px(x), ypx(y)), unmirrored
  panel: opening (dir −1): 177, 176, 175 for s = 0,1,2; none for s = 3
         closing (dir +1): 175, 176, 177, 178 for s = 0..3
  draw the panel at (doorX, doorY)
  if playerOnTop: draw the player cel
  draw Telly / trick if present (over everything)
  present; wait until 9 cs after the step started
```
The door cels go into the **background** (they persist: erasing a sprite later shows the door state
underneath). Draw order within the playfield is therefore **cab 179 < player < panel** when
playerOnTop = false, and **cab 179 < panel < player** when true. The cels are clipped only to the canvas,
but in practice they always lie inside the playfield. Nothing is clipped against the frame: the player is
hidden only by the panels.

Sequence:
1. **Approach.** target = slotCol + 7 (local column). The player walks (state 2, 1 column per step) toward the
   target **without changing facing**. If he faced away he moves backwards with mirrored walk cels. The normal
   hall-scroll rule applies, and Telly/trick keep moving (AI only, no spawning, no hits). The original puts no
   delay between these steps, so on period hardware it ran at render speed. Use 7 cs per step, like normal
   walking (recommended; not specified by the original).
   Result: at the left ends the player stands at screen column 13 − L, with L in 0..7 (usually 0 →
   x 166). At the right ends he is at screen column 26 with scroll = area·38 − 1 (x 333). A flip overshoot
   can make it column 25 with scroll = area·38.
2. Face right. State 3 (cels 9–13, dy −2): 5 steps of 7 cs, y 81 → 71.
3. Stop all sounds (including music). Music updates off for the rest of the ride. Play **1713** (doors open,
   priority 5).
4. **Departure open**: D(open, cel 13, player on top). The player stays in his back-view cel 13 at y 71
   (136 px) in front of the opening doors. The last step leaves the open cab (179) showing.
5. State 5 (cels 19–22, dy −4): 4 steps of 7 cs, y 71 → 55 (106 px). He walks into the open cab, which stays
   visible behind him.
6. Play **1714** (doors close, priority 6).
7. **Departure close**: D(close, cel 22, player under the panels). The panels close over him. At step 3
   (178) he is completely hidden.
8. Remove Telly and the trick (erase them; Telly's sounds stop).
9. New area: 0 → 5 and 4 → 9 (**up**), 5 → 0 and 9 → 4 (**down**).
10. **Vertical slide** (silent; HUD and everything outside x 9..503 / y 35..283 untouched). Snapshot the
    current playfield (old floor: closed elevator, no visible player, no Telly). Build the new floor
    off-screen as a full hall draw at scroll = newArea·38, with no sprites. That gives the closed elevator
    cel 122 at the same x as the old one at the left ends, and 13 px further left at the right ends.
    8 steps, 9 cs each, presented after each step:
    * **Up**: the picture moves **down**. Each step shifts the playfield content down and inserts the next
      strip of the new floor at the top, taken from the new floor's bottom upward. Strips are 31 px for
      steps 1–7 and **32 px for step 8**. So the new floor comes in from **above** and the old floor
      leaves at the bottom. Quirk: the total is 249 px, so the new floor ends up 1 px too low. Its row r
      shows at y r+1, and the top row (y 35) shows the 1-px panel row just above the playfield. This stays
      until the next full hall redraw. Reproducing it is optional. Conformance captures of the arrival may
      differ by this 1-px shift.
    * **Down**: the picture moves **up** 31 px per step, and the new floor comes in from **below**, its strips
      taken top-down. Total 248 px, exact.
11. scroll = newArea·38. Player screen column = 12 at the left ends (x 153), 24 at the right ends (x 307).
    World column = scroll + column. y stays 55. Facing is still right.
12. Stop all sounds. Play **1713** (priority 5).
13. **Arrival open**: D(open, cel 23, player under the panels). He appears inside the cab at y 55 (106 px),
    partly hidden by 177 and 176, slightly less by 175, and fully visible in the open cab at step 3.
14. Facing right (unmirrored). State 6 (cels 23–26, dy +4): 4 × 7 cs, y 55 → 71. Then state 4 (cels 14–18,
    dy +2): 5 × 7 cs, y 71 → 81. The open cab stays visible behind him the whole time.
15. Play **1714** (priority 6).
16. **Arrival close**: D(close, cel 18, player on top) at y 81 (156 px). The doors close behind him. The final
    closed panel 178 stays in the background and looks the same as the elevator picture.
17. Facing = right at the left ends, left at the right ends. State 1 (stand). Music updates on again, so the
    next frame starts the new floor's music (§14.2). The next normal frame draws the standing cel, mirrored
    at the right ends.

Clock: the game clock is not advanced or redrawn during the ride. When the main loop resumes, the usual
"deadline passed → +1 minute" check adds **at most one minute**, and the rest of the ride time is not counted.

Durations: departure about 135 cs + approach, slide 72 cs, arrival about 135 cs.

Worked examples (frame 122 x / door x / player x):

| case | scroll | L | frame x | door (x,y) | player |
|---|---|---|---|---|---|
| area 0 or 5 departure, typical | 0 / 190 | 0 | 87 | (124,105) | col 13 → x 166 |
| area 0 departure, approached from the right (scroll 8 → 7 during the walk) | 7 | 7 | −4 | (33,105) | col 6 → x 77 |
| area 4 or 9 departure | 151 / 341 | −1 | 256 | (293,105) | col 26 → x 333 |
| arrival in 5 or 0 | 190 / 0 | 0 | 87 | (124,105) | col 12 → x 153 |
| arrival in 9 or 4 | 342 / 152 | 0 | 243 | (280,105) | col 24 → x 307 |

Player y during door steps: departure open 136 px, departure close 106, arrival open 106, arrival close 156.

**Corrections (v1.11) to earlier text in this chapter and chapter 07**
* §5.5 "elevator door opens (3 steps)": wrong. It takes 4 steps of 9 cs (3 panel cels + a final open-cab step).
  Closing also takes 4 steps.
* §5.5 "Telly/trick removed" (listed before the close): they are removed only **after** the departure doors
  have closed. Until then Telly keeps moving and can throw, but nothing can hit the player.
* §5.5 "faces right at the left end, left at the right end": true only at the very end. During the whole
  arrival the player faces right (unmirrored).
* §5.5 "slides vertically in 8 steps of 31 px (one direction for up, the other for down)": up is 7×31 + 32
  and the picture moves down (new floor from the top). Down is 8×31 and the picture moves up (new floor from
  the bottom).
* §5.5 did not say that y is not reset (it stays 55) and that the auto-walk does not turn the player.
* §8 "It **runs** during … all scripted walks/animations": wrong for the elevator. The clock is frozen during
  the ride, and at most one minute is added afterwards.
* §14.7 "erase every active object (restore its last rectangle from the clean background page)": in the
  original, each sprite saves what was on screen under it when drawn and restores that on erase. The visible
  consequence is that scripted door cels behave like background. The simplest equivalent is the rule above:
  draw the door cels into the background page as well as on screen.
* Spec 07 Part A "elevator 122 at y 68" is correct. The door cels do **not** share its origin; they sit at
  +37,+37.

## 6. Enemies

### 6.1 Telly the robot (hall only) (H)
Telly state variable: 0 none, 3 walking, 4 stopped, 5 throwing, 7 zapped-unlit (flashing), 8 materialising,
9 dematerialising, −1 remove.

**Spawn** (checked every frame while no Telly, only in the hall, only during normal play, only after the
player has moved once, and only when no trick is in flight):
`if random(1,1000) < SPAWN[g]` →
```
lit = (codePieces < 4) and random(1,100) > UNLIT[g]
if player.x + 3 > 20: Telly.x = -3, dir = +1 (enters from the left)
else:                 Telly.x = 35, dir = -1 (enters from the right)
r = random(0,100)
if r < 50 or (area%5 == 0 and dir == +1) or (area%5 == 4 and dir == -1):
     state 8: Telly.x = player.x - (POPIN[g] + 8) * dir   (appears near the player), pop-in sound,
              materialise anim (state 33) for 4 frames, then state 3
else state 3 (walks in from the screen edge), walking sound loop starts
```
`area` = scroll/38 here. Belly-lit flag shows as an overlay cel and equals `lit` except while throwing.

**Per frame** (g = rank group):
```
if in hall and state >= 3:                       # chase: turn around once 8 columns past the player
   if dir == -1 and Telly.x + 8 < player.x: reverse
   if dir == +1 and Telly.x - 8 > player.x: reverse
if state < 5:
   if state == 4 and random(0,100) < RESUME[g]: state = 3
   elif random(1,100) < STOP[g] and 6 <= Telly.x <= 18: state = 4
if state == 4 and random(1,100) < THROW[g] and no trick: start throw:
   state = 5, trickType = random(22,25), lit hidden during throw
speed = SPEED[g] if state == 3 else 0
state 3: anim 10, x += dir*speed;   state 4: anim 11
state 5: anim (trickType-5); when its frame == RELEASE[type] launch trick; when the anim wraps to frame 0
         -> state 4 and lit restored
state 7: anim 34 for 9 frames, then state 9
state 9: anim 32 for 5 frames, then removed
state 8: anim 33 for 4 frames, then state 3
if state >= 3 and (x < -3 or x > 35): removed
```
RELEASE (types 22..25) = 2, 0, 3, 3.

**Trick projectile** (object 5): `type` 22..25 → `(y, hit width)` = (81,5), (91,2), (115,4), (118,3);
start x = Telly.x−4 (types 22/24) or Telly.x (23/25) when moving left, Telly.x+4 when moving right;
speed = `random(SPEED[g], SPEED[g]+1)` columns/frame in Telly's direction; anim state = type; looping
sound per type (SND WAVE 1700, 1727, 1706, 1750). Near the edges speed is cut to 2 if it would overshoot;
removed at x ≤ 1 or x ≥ 33. Tricks keep flying after Telly leaves.

**Collisions with the player** (checked once per frame after drawing; none while player is in state 8;
priority trick > Telly > LiveWire):
* **Trick hit**: splat cel over the player (210/212/209/211 for types 22-25) for ~83 cs with a sound;
  types 22-23 → shoved/knocked (state 26 if facing the same way Telly moves, dx +2; else state 27,
  dx −2); types 24-25 → fall (state 9, dx −1). Trick removed, and **Telly is removed too** (unless it is
  already flashing or dematerialising). **Clock +15 minutes.** Hint 1.
* **Telly crash** (only while Telly state < 7): splat cel 211, crash sound; player state 26/27 (dx +2);
  Telly → state 9 (vanishes). **Energy −1.** Hint 2.

**Zap result on Telly** (see §7): lit → drill questions (clock keeps running) then Telly removed,
success → code piece + hint 3; unlit (or currently throwing) → Telly state 7 (flash, then vanish), sound.

### 6.2 LiveWire (rooms only) (H)
Spawned on room entry and after each math challenge: `if random(1,100) < LIVEWIRE[g]`:
if player.x > 20 → x = 3, moving right; else x = 30, moving left. Moves 1 column/frame (anim 13),
reverses when x < 3 or x > 25, crackle sound each frame. Hit box `[x+1, x+10)`.
Touching the player: LiveWire removed, player state 30 (shock) with dx −2 (pushed backwards),
**energy −1**, hint 6. Zapped: state 31 for 5 frames then removed. Not respawned until the next
room entry / challenge.

### 6.3 Telly interactions, frame by frame (H)

This section overrides any less exact statement elsewhere in this chapter about these animations.

Notation. "Frame" = one iteration of the main loop (≈7 cs). Cel numbers are SSO1 ASEQ 200; ≥ 10000 means
ASEQ 210 cel − 10000. Every cel is drawn with its **top-left** at the object position: `x_px = COLPX[col]`
(negative columns mirrored, §1), `y_px = round(y × 1.92)`. Mirroring keeps the same top-left. Telly and the
trick projectiles are drawn as stored while moving left and mirrored while moving right. The player is drawn
as stored when facing right and mirrored when facing left. Columns are screen columns; when the hall
scrolls, Telly, the trick and the player shift together (§3.5).

Frame order (reminder, §9.2): (1) commands (the jump and the whole zap run here), (2) erase objects, scroll,
Telly logic or spawn check, player, trick, LiveWire, room prop, draw, (3) wait for the deadline and present,
(4) collisions, including the splat freeze, (5) clock.

Fixed positions used below (y in design units → px): Telly 77 → **148**; belly overlay 97 → **186**;
standing player 81 → **156**; zap bolt (player y + 12) 93 → **179**; splat 52 → **100**; trick 22 / 23 / 24 /
25: 81 / 91 / 115 / 118 → **156 / 175 / 221 / 227**.

**Animation phase is global.** Each animation in the shared state table (§4.2) keeps one current-frame
index that is used by whoever plays that animation. For Telly's animations it is **never reset**: not at
spawn, not on a state change. Advancing means "draw cel[current], then current = (current+1) mod count"
(with the tick rule in §4.2). In practice:
* the materialise (33), dematerialise (32) and throw (17–20) animations always run whole cycles, so they
  start at their first entry, except after an interruption;
* walk/idle (10/11) continue from where the previous Telly left them;
* the flash animation (34) has 8 entries but is played 9 times, so successive flashes begin on alternating
  cels (100 first, then 112 the next time, and so on);
* a throw interrupted by a zap leaves its animation mid-cycle, so the next throw of the **same type** starts
  mid-sequence, and its release and end frames shift (it ends when the index wraps to 0). Keep this for
  fidelity.

### 6.3.1 Appearing and walking

**Walk-in spawn** (state 3). Spawn frame: Telly at x = 35 (dir −1, drawn as stored) or x = −3 (dir +1,
mirrored), with no movement on this frame. It shows the walk cel of the current walk phase, plus the belly
overlay if lit. The hum (1754, prio 2) starts. From the next frame on, see "Walking".

**Materialise** (state 8). Telly x = P − (POPIN+8)·dir, where P is the player's column **after** this frame's
scroll shift and **before** the player moves this frame. Example: rank group 0, dir +1, the player at 25
walking right, the view scrolls 1 → P = 24 → Telly x = 24 − 16 = 8 (px 102).
The pop-in sound (1753, prio 2) plays on the spawn frame **only**. The hum starts on the next frame, because
Telly's per-frame logic asks for it every frame.

| frame | cel | belly | note |
|---|---|---|---|
| spawn (F0) | 111 (dust puff) | no | 1753 |
| F1 | 110 (closed TV box) | no | hum 1754 from here on |
| F2 | 110 | no | |
| F3 | 109 (tumbling) | no | |
| F4 | 108 (hopping up) | no | state becomes 3 **after** this cel is drawn |
| F5… | walk cel (current walk phase) | **yes if lit** | first walking frame; moves |

Telly does not move while materialising. It cannot be zapped (target only in states 3–5) and does not hurt
the player (crash only in states 3–5).

**Walking** (state 3): every frame, first x += dir × SPEED, then the walk cel is drawn (100–103; each cel is
held for 2 frames, so the cycle is 8 frames). Belly overlay (lit only): cel 204 + (index of the walk cel drawn
in this frame), i.e. 100↔204 … 103↔207.
**Stopped** (state 4): idle cels 104–107, 2 frames each, no movement; belly cel 208.
Belly position: (COLPX[Telly x], 186), with Telly's mirror state. Example: Telly x = 1 → cel at (13,148),
belly at (13,186). The overlay lies inside Telly's 89×108 rectangle.
Turning (the 8-column chase rule, §6.1) happens in every state ≥ 3, including throwing, flashing and
(de)materialising. It flips the mirror state at once.

### 6.3.2 Throwing and trick flight

Throw start (on a frame where the throw roll succeeds): state 5, the type is rolled, and the belly is hidden.
**On that same frame** the throw animation is advanced, so F1 below is the frame of the decision. In each
throw frame, in this order: advance the animation (draw cel[i], i = i+1 mod n); **if i is now 0**, the throw
ends (state 4, belly shown again on this very frame, with cel 208); **if i now equals RELEASE[type]**
(2, 0, 3, 3 for types 22–25), the trick is launched.

| type (projectile, hit word) | F1 | F2 | F3 | F4 | F5 | F6 | trick launched on |
|---|---|---|---|---|---|---|---|
| 22 lightning bolts (115/116, BLAST! 210) | 105 | **113** | 114 | 113 | 105 + belly | – | F2 |
| 23 sparks (183–185, CRASH! 212) | 105 | 180 | 181 | 182 | **105** + belly | – | **F5 (last frame)** |
| 24 sound waves (189/190, ZAP! 209) | 105 | 186 | **187** | 188 | 186 | 105 + belly | F3 |
| 25 disc (193/194, BOOM 211) | 105 | 186 | **191** | 192 | 186 | 105 + belly | F3 |

The frame after the throw is a normal stopped frame (idle cel, belly 208). Telly does not move while throwing.

**Launch** (in Telly's logic; the trick is then moved and drawn later **in the same frame**):
speed v = random(SPEED, SPEED+1) × dir, fixed for the whole flight; start column x0 = Telly x − 4 (types 22
and 24, dir −1), Telly x (types 23 and 25, dir −1), Telly x + 4 (any type, dir +1). The first drawn
position is therefore x0 + v. The looping sound starts at launch (22 → 1700, 23 → 1727, 24 → 1706,
25 → 1750; prio 3).
Worked example: type 23, Telly at 7 moving right, rank group 0 → x0 = 11; with v = 1 the sparks are drawn
at col 12 (px 153, y 175) on F5, then 13, 14, …
**Flight**, every frame: x += v, draw the next projectile cel (1 frame each: 22: 115,116; 23: 183,184,185;
24: 189,190; 25: 193,194). The trick uses Telly's **current** mirror state (it flips if Telly turns) and keeps
its own speed and direction. After being drawn at x ≤ 1 or x ≥ 33, it is removed (erased next frame,
sound stopped). Edge slow-down applies only to tricks moving right: if 33 − x < v then v = 2. (The
corresponding test for leftward tricks never triggers.) Tricks keep flying after Telly is gone, until they
hit, leave the screen, or the area changes, or a drill ends (§6.3.7).

### 6.3.3 Jumping and flipping over Telly or a trick

The jump is set up in the commands phase, so its first cel is shown on the same frame. 8 frames, 1 per frame:

| jump frame | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| cel | 34 | 35 | 36 | 37 | 38 | 39 | 35 | 34 |
| y (units) | 69 | 57 | 45 | 33 | 45 | 57 | 69 | 81 |
| y px | 132 | 109 | 86 | 63 | 86 | 109 | 132 | 156 |

Jump: dx 0. Flip (Up+Left/Right, Home): +2 columns per frame in the facing direction. No take-off sound.
On frame 8 the landing sound (1726, prio 2) plays and the state returns to standing **within that frame**,
so that frame's collision check is live again. A trick still overlapping the player at landing hits him
then. Invulnerable only while the jump state lasts.

### 6.3.4 Being hit: trick hit, Telly crash, splat

Collisions are tested after the frame has been presented (§4.1 boxes: player [x+1, x+5), Telly
[x+1, x+7), trick [x, x+w)). Priority: trick, then Telly, then LiveWire. Telly only counts in states 3, 4, 5.
Materialising, flashing and dematerialising Tellys are harmless. Walking into Telly and being walked into
are the same event. "Hit from behind" = the player faces the same way Telly is moving.

**Splat sequence** (replaces the §14.4 "Sequence" bullet). The whole game is frozen during it:
1. Erase all action objects, and remember the background under the splat rectangle.
2. **Redraw all action objects exactly as they were just presented** (player, Telly, belly, trick).
3. Draw the splat cel **on top**, never mirrored, clipped to the playfield. Then present. Play 1723 (prio 3).
4. Wait ≈ 83 cs (blocking).
5. Restore the background under the splat and redraw the objects **without presenting**. The splat stays
   visible until the next frame is presented.
Splat position: trick hit → (COLPX[player x], 100); crash → (COLPX[min(Telly x, player x) + 1], 100).
Examples: player at 19 → (243,100); crash with Telly 6 and player 12 → (89,100).
Cels: 22 → 210 BLAST!, 23 → 212 CRASH!, 24 → 209 ZAP!, 25 → 211 BOOM, crash → 211 BOOM. **No splat or
word appears when Telly is zapped.**

**Trick hit**, after the splat (its loop sound keeps playing during the freeze):
* Telly is removed at once (erased next frame, hum and pop-in sounds stopped), unless it is flashing or
  dematerialising. The trick is removed (sound stopped). Clock +15 minutes. Hint 1 is queued. No energy loss.
* Player reaction (all dx are per frame, relative to the facing; the facing does not change):
  * types 22/23, hit from behind → **shoved**: cels 40,41,42,43,43,43,43, dx +2 (forward, 14 columns).
    On the 7th frame: landing 1726 (prio 2), then standing.
  * types 22/23, hit from the front → **knocked down**: cels 99,99,99,99,30,31,32,33, dx −2 (backwards,
    16 columns). On the frame that draws 33: 1719 (prio 5) + 1721 (prio 4), then get-up.
  * types 24/25 → **trips and falls**: cels 27,28,29,28,27,30,31,32,33, dx −1 (backwards, 9 columns). Thud
    1720 (prio 3) on the frame that draws 30. On the frame that draws 33: 1719 + 1721, then get-up.
  * **get-up** (dizzy): cels 44,45,46,44,45,46,44,35,35, dx 0. On the 9th frame: landing 1726, then standing.
  The first reaction cel is drawn on the frame after the freeze. Input is ignored until standing again.

**Telly crash**, in order: 1704 (prio 2) → splat 211 → player reaction → 1719 (prio 3) → Telly
dematerialises (§6.3.5; the hum starts again during it) → hint 2 queued → energy −1.
Player reaction: hit from behind → shoved (40–43 sequence above). From the front → knocked down
(99… sequence, then get-up). In **both** cases dx = **+2 relative to the facing**: when the player faces
Telly, he slides 16 columns *forward*, through the vanishing Telly. This is an original quirk; keep it.

### 6.3.5 Flashing and dematerialising

**Flash** (state 7, after an unlit zap): 9 frames, cels alternating 100 and 112 (the orange "electrified"
Telly; global phase, see above). No movement, no belly, no hum (1754 is not requested in state 7).
After the 9th flash frame: dematerialise.
**Dematerialise** (state 9, after the flash or a crash): 5 frames: **108, 109, 110, 110, 111** (hop, tumble,
shrink to a box, box, dust puff). On the 6th frame Telly is removed (not drawn; hum and pop-in stopped).
The hum is requested on each dematerialise frame. No belly.

### 6.3.6 Zapping (replaces the §7 "Zap" bullet)

Only if the player state is < 3 (standing or walking). The zap is a **synchronous sequence in the commands
phase**: nothing else moves or animates (Telly, trick, clock display), and no collisions happen.
Each step below is "set a deadline 4 cs ahead, draw, present, wait for the deadline".
1. The target is fixed at the start: Telly if it is walking, stopped or throwing, with span
   [Telly x + 1, Telly x + 7]. Otherwise the LiveWire, if one exists.
2. **Pose**, 3 steps: player cels 47, 48, 49 at (COLPX[px], 156), mirrored when facing left, where
   px = player x − 4 when facing left, and player x when facing right. All three cels use this px.
3. Sound: 1764 (prio 5) if energy > 0, else 1765 (prio 5, "empty").
4. **Bolt**, 6 steps, cels 50–55 at y 179 with the player's mirror state, drawn **over everything**. Each bolt
   cel stays on screen only until the next step's present. **The pose cel 49 stays visible under the bolt**.
   Bolt column b: facing right, b = player x + 8 for all 6 steps. Facing left, b = player x − 7, then after
   each step b −= 5, 3, 0, 0, 0, 0, giving **x−7, x−12, x−15, x−15, x−15, x−15**.
   Hit test at each step, before drawing: hit if the span start or the span end is within [b, b+12].
   No bolt is drawn when energy is 0, or when b ≥ 40. The steps still take their time.
   Worked example: player at 18 facing left → pose at px 179 (col 14). Bolt at cols 11, 6, 3, 3, 3, 3 =
   px 141, 77, 38, 38, 38, 38 (cel 50 is 51 px wide, 51 is 99, 52–54 are 148, 55 is 154), so the cone's
   narrow end stays at the wand while it widens leftwards. Telly at 1 (span 2..8) is hit on step 2.
   Facing right at 18 → bolt at col 26 = px 333.
5. Energy −1 if it is > 0. The player goes back to his column and is standing (cel 0 is drawn by the next
   update).
Duration ≈ 9 × 4 cs ≈ 36 cs. An empty zap takes the same time, with no bolt drawn.
6. Result (only if energy is still > 0 **after** paying — the last unit never hits):
   * hit, Telly's belly **currently shown** (lit and not throwing) → drill (§6.3.7);
   * hit otherwise (unlit, or lit but mid-throw): stop 1754, play 1763 (prio 2), Telly flashes (§6.3.5)
     starting on this same frame's update. An unreleased trick is never thrown;
   * room, hit, LiveWire crawling: stop 1731, play 1733 (prio 5), LiveWire vanish cels 10010–10014,
     1 per frame, then removed.

### 6.3.7 Lit Telly: hall → drill → hall

There is **no** transition animation (no zoom, no splat, no Telly animation). Right after step 5 of the zap:
the music stops (the tune is remembered), the Decoder button is removed, **all sounds stop**, and the drill
screen is built and shown. That is a panel rebuild, then the Telly-belly picture (ch. 07 Part A §4), then
TV static: 8 steps of ≈ 8 cs alternating the two static cels, with 1752 (prio 10), then the blank screen.
The game clock keeps running (drill rate). Result: success → code reveal (07 §4) and then hint 3 is
shown **over the drill screen**. Failure → "No Code. Catch me again!".
Return: the hall is redrawn completely at the current scroll. **Telly and any trick in flight are removed**,
silently and without animation, on success and on failure. Then one object update without spawning (the
player is drawn standing, facing as before), the Decoder button comes back, the screen is presented, and
the remembered tune restarts. No "CRASH!"/"BOOM" or wreck is shown; Telly is simply gone.

### 6.3.8 Draw order and erasing (summary)

Bottom to top within a frame: scenery and HUD (background) → zap bolt object slot → LiveWire → room prop →
player → Telly → belly overlay → trick. The belly is drawn only while shown, right after Telly. It is
erased together with Telly's rectangle. Sprites are erased by restoring what was under them.
Pose and bolt during a zap, and the splat, are drawn over the frozen, fully drawn scene. The bolt is
removed after each present, the splat after the freeze. Nothing else draws on top of the splat.

**Corrections (v1.14) to earlier text in this chapter**
* §6.1 "state 8: anim 33 for 4 frames, then state 3" — 5 materialise cels are shown (one on the spawn frame
  plus 4). The 4th one after spawn is 108, and the belly appears only from the first walking frame.
* §6.1 "when its frame == RELEASE[type] launch trick" — it means the index **after** advancing. For type 23
  (RELEASE 0) the trick leaves on the last throw frame.
* §6.1 "Near the edges speed is cut to 2 if it would overshoot" — only for tricks moving right.
* §6.1 / §9.3 "lit → drill questions … then Telly removed" — the trick in flight is removed too.
* §4.2 rows 32/33 "108-111" / "111-108" — the exact lists are 108,109,110,110,111 and 111,110,110,109,108.
* §7 "Bolt … shown for 6 frames" — these are 4-cs steps of a frozen, synchronous sequence, not game frames.
  The shift is applied **after** each step, and the pose cel 49 stays visible throughout.
* §14.4 "erase all six action objects, save the area under the splat, draw it, present" — omits that the
  objects are redrawn before the splat is drawn. As written, it produces an empty hall under the word.
* §6.1 "Telly crash … player state 26/27 (dx +2)" — correct, but it should say "relative to the facing,
  for both states". A player facing Telly therefore slides toward it.

## 7. Zapper and energy (H)

* Energy ("pepper") is a byte, **start 12**. HUD bar shows `min(energy,24)` × 3 px.
* `addEnergy(d)`: ignored if it would go below 0; otherwise applied, bar redrawn; if energy ≤ 5 queue hint 12.
* **Zap**: see §6.3.6 (frame-exact sequence; the game is frozen during it).
* **Energy supply machine — placement.** One per game. At new game and at continue, pick
  `CAND[random(0,8)]` from the slots (area, slot) = (0,2) (1,2) (3,2) (4,0) (5,2) (6,0) (7,0) (8,2) (9,0)
  (if a machine was placed earlier in this program run, first restore that slot's original record).
  The chosen slot becomes type 9, y 110, cel 195, right end `col + 19`. Hot-spot zones (local columns,
  last match wins as usual): (0,2) 18–37, (1,2)/(3,2) 20–37, (5,2)/(8,2) 19–37, (4,0) 0–12, (6,0) 0–14,
  (7,0) 0–11, (9,0) 0–12. Drawn like any non-door hot spot: cel 195 (131×129) at
  **(9 + 13·(machineWorld − scroll), 110)**, machineWorld = area·38 + slot.col.

* **Using it** (Up with hot spot 9, player state ≤ 2; if Telly or a trick exists Up jumps instead, so
  neither exists during the sequence). The clock is not paused; nothing else in the hall moves.
  1. Remember the current facing.
  2. **Approach**: target = local column `slot.col + 3`. Walk toward it 1 column per step, d steps,
     **without turning** (moonwalk if facing away), normal scroll rule; same pacing as the room-door
     approach (7 cs per step recommended). After it let `ms = machineWorld − scroll` (always 3..23),
     `X = 9 + 13·ms` (= the machine's x), player column = `ms + 3`, player x = `COLPX[ms+3]`.
  3. Facing := right. **State 15**, 5 paced steps (7 cs), dy −2 per step: cels **9, 10, 11, 12, 203** at
     y 79, 77, 75, 73, 71 (px 152, 148, 144, 140, 136). Cel 203 is the player seen from behind with
     his arm out, pressing the machine (77×136); it stays on screen, unchanged, through step 5.
  4. Remember the scene rectangle X..X+131 × 91..130 as it is now (the beam frames are drawn on this).
  5. **If credit > 0**: energy += credit, credit := 0, and the HUD energy bar is redrawn **and shown
     now**, before the first beam frame (if the new energy is ≤ 5, queue hint 12 as addEnergy always
     does). Animation = 9 frames, cels **196, 197, 196, 197, 196, 197, 196, 197, 196**; sound **1707**.
     **Else**: queue hint 4 (only if not already queued or shown this game). Animation = 4 frames, cels
     **199, 200, 201, 199**; sound **1708**. Energy unchanged.
  6. Start the sound: priority 2, **looped**.
  7. For each animation frame: start a **12 cs** deadline; draw the cel (131×39) at **(X, 91)** onto the
     scene — it spans y 91–129 and lies over the top of the machine (its horn tips, y 110–129) and the
     wall just above; the 196/197 beam crackles between the two horn tips, 199–201 are sparking horn
     tips; present; put the remembered rectangle back into the scene (not shown yet); wait for the
     deadline. Nothing else is updated or redrawn during these frames: the player remains as cel 203
     at y 71 (it never overlaps the cel rectangle). Fill = 108 cs, refusal = 48 cs. Cels 198 and 202 are
     not used.
  8. Stop the sound (so only the first ≈1.1 s of 1707 / ≈0.5 s of 1708 is heard).
  9. **State 29**, 5 paced steps (7 cs), dy +2: cels **14, 15, 16, 17, 18** at y 73, 75, 77, 79, 81
     (px 140 … 156), unmirrored. The first step's present is also the first picture without the last
     beam/refusal cel (the machine top looks as before).
  10. Facing := the facing remembered in step 1; state 1. The next normal frame draws stand cel 0 at
     y 81, **mirrored if the player had been facing left** when he pressed Up.
  11. Hint 4 (refusal), if queued, is shown by the normal queued-hint rule (first frame with no Telly,
     no trick, player state ≤ 2), i.e. right after the return.
  Timeline from the end of the approach (T): state-15 pictures at T, T+7, …, T+28; at T+35 HUD update
  (fill), sound start and frame 0; frame i at T+35+12i; fill: sound stop and first state-29 picture at
  T+143, standing at T+178; refusal: state 29 from T+83, standing at T+118.
  Worked examples: machine (3,2) (world 139) reached walking right from world 137 / scroll 111 → scroll
  116, ms 23: machine (308,110), beam (308,91), player x 333, cel 203 at (333,136).
  Machine (1,2) (world 63) reached walking left from world 69 / scroll 63 → scroll 60, ms 3: machine
  (48,110), beam (48,91), player x 77; ends facing left (cel 0 mirrored at (77,156)).
  Machine (4,0) (world 156) from world 164 / screen col 20 (scroll 144, walk left 5, no scroll): ms 12,
  machine (165,110), beam (165,91), player x 192.

* **Credit**: starts at 0 in a new game and is part of the saved game. Each **solved** math challenge
  adds `ENERGY_CREDIT[g]` (= 2 at every rank) at the moment it is solved; failed attempts add nothing.
  It is paid out (all of it) at the next machine use, as above. There is no upper cap on energy (only
  the bar caps at 24).

**Corrections (v1.20) to earlier text**
* 04 §7 "9-frame fill animation, cels 196/197, sound" / "refusal animation (cels 199-201, 4 frames)":
  no position (must be (machine x, 91)), no frame time (12 cs), no order of the energy add (before the
  animation), no facing restore. Superseded by the text above.
* 04 §5.3 "Scripted sequences (… energy machine) run … one animation step per 7 cs": true for states
  15/29, not for the beam/refusal frames (12 cs, nothing else updated).
* 04 §4 state table rows 15 and 29: missing "used with dy −2" (15) and "dy +2" (29), like rows 3/4.
* 04 §3.4 / 07 Part A "cel 203 = invisible marker": only true for the room sprite set; in the hall set
  (ASEQ 200) cel 203 is the visible "player pressing the machine" picture used by state 15.
* 04 §14 rows 1707/1708 "started as the … animation begins": correct; add that the HUD update (fill)
  happens at the same moment and that hint 4 is queued then.

* Energy 0 is not fatal; it only disables zapping. Leftover energy scores `energy × ENERGY_BONUS[g]` at the
  end (chapter 05).

## 8. Game clock (H)

* Stored as `HHMM` decimal in a word (save record offset 4). **Start 900 (9:00 PM), end 1200 (midnight).**
* Action loop: **+1 minute every 328 cs (3.28 s)** → 180 minutes ≈ 9 min 50 s of real play.
* During Telly's drill questions the drill loop advances it **every 240 cs (2.4 s)**.
* `addMinutes(n)`: clock += n; if minutes ≥ 60 carry into hours; if value > 1159 clamp to 1200.
  Redraw digits (hour tens only if ≥ 10). A chime plays when the hour changes, and at 11:50.
  If the clock reaches 1200 while the game is in progress: score and energy zeroed, midnight animation
  (clock cels alternate 10×, ~83 cs each, sound), the saved game is discarded, "Out of Time" dialog
  (9136), then a new game for the same player starts. At exactly 9:02 with the player never having moved, hint 0 is shown.
* **Pause** is a nesting counter: on the first pause the remaining time to the next tick is saved; on the
  last resume the deadline is restored. The clock is paused during: math challenges, the decoder, every hint
  dialog, every menu command (except the sign-in menu), the Esc confirmation and other dialogs.
  It **runs** during drill questions (faster, see above) and all scripted walks/animations.
* **Clock option off** (Options ▸ Clock, irreversible for the game): time never advances, midnight never
  happens, no time bonus.
* Telly trick hit: +15 minutes.

## 9. Main loop and screen state machine

### 9.1 Program level (H)
```
init (load BINA 5000 into world tables, sprites, srand(time))
loop:
   choice = signInScreen()
   mode = startMode(choice)      # computes rank group, resets hint flags
   if mode == PLAY:  newOrContinueGame(); clockDeadline = now+328; actionLoop()
   if mode == DRILL: drillForSkillLoop()
   exit if quit
```
`actionLoop` runs `frame(key)` forever; leaving the game is an abort back to the program loop, to one of two
restart points: **sign-in** (Return to Sign In) or **new game for the same player** (after midnight and after
every finished game, win or lose). Exit quits the program.

### 9.2 One frame (H)
```
frame(key):
  show at most one queued new-player hint (only if no Telly, no trick, player state <= 2)
  update mouse cursor shape
  if key == Esc: pause; exit-confirmation; resume; return
  if key == Tab: pause; hint 14; decoder (decide may end the game); resume; return
  read mouse button -> command flags; poll keyboard -> command flags
  apply commands (§5.2); rand() (discarded)
  updateObjects(): clear dirty flags; erase objects; hall scroll; Telly AI/spawn; player update;
                   trick update; LiveWire update; room prop update; draw objects
  wait until now >= frameDeadline
  present (hall or room blit)
  collisions (§6.1)
  if now > clockDeadline: addMinutes(1); clockDeadline = now + 328
  frameDeadline = now + 7          # ~70 ms + processing -> ~13-14 frames/s
```
Timing detail: input is polled and objects are updated at the **start** of an iteration, immediately after the
previous iteration ended; the iteration then waits for its deadline before presenting. The frame deadline is not
reset when a game starts, so the first iteration presents immediately (its deadline has already passed). The
clock deadline is set to now + 328 when the game starts.

### 9.3 Screens and transitions
```
            +---------------- Esc: exit? (paused) -----------------+
            v                                                       |
 SIGN-IN -> HALL <--(door/exit)--> ROOM --(Up on flagged object)--> MATH CHALLENGE (paused) --> ROOM
            |  ^ (elevator: slide transition, other floor of HALL)         (LiveWire re-roll)
            |  +-- zap lit Telly --> DRILL QUESTIONS (clock runs) --> HALL (+code piece on success)
            |  +-- Up at energy machine (scripted) --> HALL
            +--- Tab (from HALL or ROOM) --> DECODER (paused) --Decide--> ENDING
 ENDING: walk to chosen room door; correct -> win sequence -> BONUS panel (energy, time, win points)
         -> score summary -> Continue -> NEW GAME (same player);  wrong -> lose sequence -> Continue -> NEW GAME
 Midnight (any time the clock is running) -> OUT OF TIME dialog -> NEW GAME (same player)
 Menu Return to Sign In -> SIGN-IN;  Exit -> quit
```
Telly and tricks exist only in the hall; LiveWire and the room prop only in rooms.

### 9.4 Math-challenge flags (per game) (H)
`challengeFlag[10]`: 0 none, 1 has a challenge, 2 solved, 3 attempted/failed. New game:
rank groups 0-1 (Trainee..Pro): for each room pair (0,1),(2,3),(4,5),(6,7),(8,9) exactly one object,
chosen by `random(1,100) & 1`, gets flag 1. Rank groups 2-3 (Expert..Champion): all 10 objects get flag 1.
Solving: flag 2, points `MC_POINTS[g] − MC_PENALTY[g] × wrongTries` (see chapter 05), energy credit +2.
Failing: flag 3 (clues still given).

## 10. Difficulty constants

Rank group `g = rank / 2` (Trainee/Junior 0, Senior/Pro 1, Expert/Master 2, All-Star/Champion 3). The
action game always uses the player's rank; Problem Customization changes only drill/word-problem content.

| constant | g0 | g1 | g2 | g3 | meaning |
|---|---|---|---|---|---|
| MC_POINTS | 300 | 300 | 600 | 600 | math challenge base points |
| MC_PENALTY | 100 | 100 | 200 | 200 | minus per wrong try |
| SPAWN | 10 | 15 | 30 | 40 | Telly spawn chance per frame: `random(1,1000) < SPAWN` |
| THROW | 20 | 25 | 30 | 35 | per-frame throw chance while stopped (%) |
| UNLIT | 0 | 30 | 40 | 50 | Telly lit if `random(1,100) > UNLIT` (and < 4 code pieces) |
| SPEED | 1 | 2 | 2 | 2 | Telly walk speed (col/frame); trick speed = random(SPEED, SPEED+1) |
| POPIN | 8 | 8 | 6 | 4 | materialise distance = POPIN+8 columns from player |
| LIVEWIRE | 20 | 30 | 40 | 50 | LiveWire chance per room entry/challenge: `random(1,100) < LIVEWIRE` |
| WIN_BONUS | 1000 | 2000 | 3000 | 5000 | winning-game points |
| ENERGY_BONUS | 100 | 150 | 300 | 400 | points per energy unit left |
| TIME_BONUS | 25 | 35 | 50 | 60 | points per game minute left |
| (unknown) | 20 | 30 | 40 | 60 | field 11; no use found |
| ENERGY_CREDIT | 2 | 2 | 2 | 2 | energy per solved challenge |
| STOP | 60 | 50 | 40 | 30 | per-frame chance (%) Telly stops when 6 ≤ x ≤ 18 |
| RESUME | 15 | 12 | 11 | 9 | per-frame chance (%) a stopped Telly walks on (`random(0,100)`) |
| challenges per room | 1 of 2 | 1 of 2 | 2 | 2 | |

Fixed (all ranks): start 9:00, end 12:00, 328 cs/minute (240 cs in drills), frame 7 cs, energy start 12,
zap cost 1, Telly crash −1 energy, LiveWire −1 energy, trick +15 min, walk 1 col/frame, flip 2 col/frame ×
8 frames, jump ±12 units × 4+4 frames, Telly turn distance 8 columns, throw zone cols 6..18.

## 11. New Player Hints (H)
Dialog id = 1000 + n; each shown at most once per game, only if hints are enabled; the clock is paused
while shown. Some are queued (shown when the hall is calm), others immediate.

| n | trigger |
|---|---|
| 0 | 9:02 and player never moved |
| 1 | hit by trick (queued) | 
| 2 | Telly crash (queued) |
| 3 | code piece captured |
| 4 | energy machine without credit (queued) |
| 5 | entering a room |
| 6 | LiveWire hit (queued) |
| 7 / 8 | after solving / failing a challenge |
| 9 | drill answer entry (timed, from drill module) |
| 10 / 11 | starting a math challenge, calculator off / on |
| 12 | energy ≤ 5 (queued) |
| 14 | first decoder use |

## 12. Debug / beta features (H)
* Menus 800 (Debug: Show Hot Spots, Channel Usage, Screen Elements, Wait Times) and 5000-5700 (BetaTest:
  Show MTM Info, No LiveWire, No Robot, Unlimited Pepper, Give All Prizes, Show Game Seed, Show NUMs, Do MTM
  test; BetaSeed 1-10; BetaProblem; BetaQuestion; Beta Object; BetaMTM; BetaGameSeed Random/Set; BetaRank)
  exist as resources only. The menu bars are built from three descriptor lists (sign-in: 500,600; game:
  1000,1100,1200; drill: 1300,1400,1500) and none include them; no command handler for ids 800-803 or
  5000-5709 exists. **They are dead in the retail build.** ("Pepper" = zapper energy, "MTM" = math
  challenge.)
* A seed-override pair of variables (flag + seed) is read at new game but never written.
* INI `CheatKeys` (and `NoBlanket`) are read with GetPrivateProfileInt into globals; `CheatKeys` is never
  read afterwards (no effect). A reimplementation may ignore both.

## 13. Open questions
1. Hall hot-spot `field3` values (e.g. 294) are used as widths, giving very wide zones; this looks deliberate
   (the game auto-walks to the exact spot) but intent is unconfirmed.
2. (Resolved) Cel ids ≥ 10000 refer to ASEQ 210 (SSO1) cel `id − 10000`; < 10000 refer to ASEQ 200. The
   room prop (cels 10003-10007) is therefore the rotating **security camera** high on the room wall, and
   10008/10009 are the electrocuted-player frames — consistent with the graphics catalogue.
3. Exact cursor-to-command mapping when the mouse is over the player but low (no command?) — M.
4. Field 11 of the tier table (20/30/40/60) has no known use.
5. Delay helper used in scripted sequences converts its argument as `cs = floor(floor(n*4/13)*100/73)`
   (e.g. 200 → 83 cs, 23 → 9 cs, 10 → 4 cs). Reproduce as a table of resulting centiseconds.
(Resolved during review: the object index → challenge type mapping is confirmed by the word-problem
data; the energy machine position is indeed not saved and is re-rolled on resume.)

## 14. Sounds, overlays, mirroring and draw order (action game)

Confidence: H = read directly from code; M = inferred. Sound ids are SND.DAT WAVE resource ids unless
marked "MIDI" (SND.DAT TEXT resource naming a file in MIDI/). Everything below is H unless marked.

### 14.1 Sound playback rules (needed to read the tables)

* A sound-effect request = (id, priority, loop flag).
  * If that id is **already playing**, the request is **ignored** (no restart). Every action-game call
    uses "no restart". Consequence: a one-shot requested **every frame** behaves like a loop that is
    re-triggered on the first frame after it ends (gap of up to one frame).
  * Otherwise a channel is allocated by priority (higher number wins); the sound plays once, or looped
    if the loop flag is set, until stopped by id.
* "Stop all sounds" stops every effect channel **and** the background music.
* Two requests (clock chime, zap) additionally check the "sound effects enabled" category switch; all
  others only check the global sound switch (M: in practice the same option).
* Timed waits in scripted sequences take an argument n converted to centiseconds as
  `ticks = floor(n*4/13); cs = floor(ticks*100/73)`: n=10 → 4 cs, 23 → 9 cs, 30 → 12 cs, 200 → 83 cs.

### 14.2 Background music (what looked like a per-frame "footstep" choice)

There is **no footstep sound**. Every frame in which the player object is updated (normal play and every
scripted step), if music updates are enabled, the game requests the looping background MIDI:

| where | condition | MIDI |
|---|---|---|
| hall | world column / 38 < 5 (areas 0-4 = floor 1) | 1728 `level1.mid` |
| hall | otherwise (areas 5-9 = floor 2) | 1729 `level2.mid` |
| room | same test on (saved hall world column + walking done in the room) | 1745 `room1.mid` |
| room | otherwise | 1746 `room2.mid` |

In effect News Room and Game Show Room (floor-1 doors) use room1; Cartoon, Equipment, Sound use room2.
Request semantics: if the requested tune is already current, nothing happens; otherwise the old tune is
stopped and the new one started looped. If the MIDI player reports it is no longer playing, the
"current" marker is cleared so the next frame restarts it.
Music updates are switched **off** for the whole elevator ride and room-entry sequence and re-enabled at
their end; the "stop all sounds" at the start of those sequences also kills the current tune.

### 14.3 Sound-effect table (Q6 / Q10)

| event | id(s) | prio | loop | trigger / timing |
|---|---|---|---|---|
| **Bump** (walking into screen col 1/33 → fall, state 9) | 1720 | 4 | no | on the frame the fall starts |
| **Thud** (fall, state 9) | 1720 | 3 | no | on the update where the fall's remaining-frame counter reaches 3 (≈6th frame of the fall; ignored if 1720 still playing). Every state-9 fall (bump, tricks 24/25) |
| **Get-up start** (end of fall 9 or knocked-down 27) | 1719 **and** 1721 | 5 / 4 | no | on the frame those states end, as get-up (state 28) begins |
| **Landing** | 1726 | 2 | no | on the frame state 26 (shoved), 28 (get-up) or 8 (jump/flip) ends and the player returns to standing |
| Jump / flip take-off | — | | | **no sound** (only the landing) |
| Shock end (30), approach/turn/door states | — | | | no sound |
| **Zap, with energy** | 1764 | 5 | no | after the 3-step zap pose, just before the bolt appears |
| **Zap, empty** (energy 0) | 1765 | 5 | no | same moment; no bolt drawn |
| Zap hit, Telly lit | — | | | no effect sound: music stopped, **all sounds stopped**, drill questions start (drill has its own sounds, e.g. MIDI 1757 `victory.mid`). Music resumes afterwards |
| **Zap hit, Telly unlit** (or lit but throwing) | stop 1754, then 1763 | 2 | no | at the zap result; Telly starts flashing (state 7) |
| **Zap hit, LiveWire** | stop 1731, then 1733 | 5 | no | at the zap result; LiveWire vanish anim (5 frames) starts |
| **Telly pop-in** (materialises near player) | 1753 | 2 | no | once, on the spawn frame |
| **Telly hum / walk** | 1754 | 2 | no, re-requested | on the spawn frame for a walk-in spawn, then **every Telly update frame in every state except 7 (flashing)** — also while stopped, throwing, materialising, dematerialising → continuous hum |
| Telly flashing (state 7) | (1763 above) | | | hum cut at flash start, not re-requested during the flash |
| Telly dematerialise (state 9) | — | | | no own sound (hum continues until removal) |
| **Telly removed** (off-screen, vanished, zapped lit, area change, splat) | stop 1754 and 1753 | | | on removal |
| **Trick launch + flight** | type 22 → 1700, 23 → 1727, 24 → 1706, 25 → 1750 | 3 | **yes** | started on the frame the trick is released (throw anim reaches its release frame); one sound covers launch and flight; stopped when the trick is removed (screen edge, hit, area change) |
| **Trick hit (splat)** | 1723 | 3 | no | same for all four types; see §3 |
| **Telly crash** (Telly walks into player) | 1704, then 1723, then 1719 | 2 / 3 / 3 | no | 1704 immediately; then the frozen splat with 1723 (83 cs); then 1719 as the player is shoved/knocked |
| **LiveWire crackle** | 1731 | 2 | no, re-requested | every frame while the LiveWire crawls; also once at the end of the room-entry sequence if a LiveWire spawned |
| **LiveWire shock** (touches player) | stop 1731, then 1732 | 5 | no | on contact |
| **Energy machine fill** (credit > 0) | 1707 | 2 | **yes** | started as the fill animation begins; stopped after its 9 steps (≈12 cs each) |
| **Energy machine refusal** (no credit) | 1708 | 2 | **yes** | started as the refusal animation begins; stopped after its 4 steps (≈12 cs each); hint 4 armed |
| **Elevator doors open** | 1713 | 5 | no | departure: after "stop all sounds", as the door-opening animation starts; arrival: as the door-opening animation starts on the new floor |
| **Elevator doors close** | 1714 | 6 | no | departure: after the player has stepped in, as the door closes; arrival: after the player has stepped out, as the door closes |
| Elevator ride (vertical slide) | — | | | silent (music off) |
| **Room door, enter** | MIDI 1717 `enter.mid` | | once | everything stopped, jingle started at the start of the auto-walk; after the player passes through the door the game **waits for the jingle to end**, then stops all sounds and builds the room |
| Room door, exit | — | | | silent (all sounds + music stopped as the player passes through; hall music resumes on the next updates) |
| Room door / elevator door cel animations themselves | — | | | no extra sounds beyond the above |
| **Clock chime** | 1725 | 3 | no | whenever the clock's hour changes, and when minutes are added and the clock then reads 11:50 |
| **Midnight** | MIDI 1735 `midnite.mid` | | once, waited | midnight sequence |
| **Score change** | 1747 | 2 | no | whenever a non-zero amount of points is added |
| Hint dialogs | — | | | stop all sounds, then the hint's speech; no effect sound |

WAV lengths (for reference): 1700 2.75 s, 1704 0.55, 1706 1.79, 1707 3.34, 1708 1.06, 1713 1.01,
1714 0.39, 1719 0.45, 1720 0.84, 1721 1.2, 1723 0.4, 1725 1.07, 1726 0.1, 1727 1.01, 1731 2.19,
1732 0.78, 1733 0.98, 1747 0.5, 1750 1.68, 1753 0.49, 1754 1.33, 1763 1.08, 1764 0.32, 1765 0.38.

### 14.4 Splat / hit effect (Q10)
* Cels (SSO1 ASEQ 200): trick types 22, 23, 24, 25 → **210, 212, 209, 211**; Telly crash → **211**.
* Position: x = pixel of a column: trick hit → the player's column; Telly crash → min(Telly col,
  player col) + 1. y = 52 units (≈100 px). Never mirrored.
* Sequence: see the splat freeze in §6.3.4 (objects are redrawn before the word is drawn).
* Afterwards **Telly is removed unless it is flashing (7) or dematerialising (9)**. For a trick hit Telly
  therefore disappears instantly. For a crash the code then sets Telly to state 9, so it plays its
  5-frame dematerialise animation (hum restarting until removal).

### 14.5 Telly's lit-belly overlay (Q7)
* Drawn only while the "lit" flag is on, immediately after Telly's own cel, on frames where Telly was
  updated.
* Cel: walking (state 3): **204 + current walk frame index** (walk cels 100-103 ↔ overlay 204-207, same
  index as the cel drawn that frame); stopped (state 4): **208**.
* Position: same x as Telly (same column→pixel), y = Telly's y **+ 20 units** (the sum is scaled ×1.92,
  ≈ +38 px), same mirror state as Telly.
* Hidden: unlit Tellys; during the throw animation (restored when it ends); during pop-in/materialise
  until Telly starts walking; from a zap result or removal onward.
* **Telly art faces LEFT natively**: drawn as-is while moving left, mirrored while moving right.
* **Trick projectiles** face left natively; each frame they use Telly's *current* mirror state (a trick
  flips if Telly turns round while it flies — quirk). No overlay.
* **LiveWire** faces left natively (mirrored when crawling right). Room prop never mirrored.

### 14.6 Mirroring in the generic object draw (Q8b)
* A mirrored cel is drawn **in the same rectangle**: top-left stays at the object's (x, y); only each
  row's pixels are reversed. **No x adjustment** (neither cel width nor a fixed amount).
* **Player art faces RIGHT natively** (mirrored when facing left). Zap bolt cels 50-55 face right and use
  the player's mirror state.
* Only game logic applies facing-dependent offsets: zap pose drawn 4 columns further left when facing
  left; bolt x = player+8 (right) or player−7 then shifted −5, −3, 0, 0, 0, 0 per frame (left);
  bolt y = player y + 12.

### 14.7 Draw / erase order (Q9)
Slots: 0 zap bolt, 1 LiveWire, 2 room prop (camera), 3 player, 4 Telly, 5 trick.
* Each frame: (1) erase every active object (restore its last rectangle from the clean background page)
  in slot order 0→5, freeing slots marked for removal; (2) scroll; (3) logic in the order Telly AI,
  Telly spawn check, player, trick, LiveWire, room prop, then collisions; (4) draw every object updated
  this frame in slot order **0, 1, 2, 3, 4, Telly belly overlay, 5** (later = on top).
  Bottom→top: bolt, LiveWire, room prop, player, Telly, belly, trick.
* The zap bolt is actually drawn by the synchronous zap sequence over the frozen scene (on top of
  everything), then erased.
* Scenery — hot-spot props, plain doors, elevators, energy machine, name plates — and the HUD (clock,
  score, energy bar) are in the background page: **all objects draw over them**; erasing restores them.
* Scripted door animations: room door (open 134→135→136, close 136→135→134, 3 steps): door first,
  **player on top**. Elevator: 4 steps of 9 cs. Each step draws cab cel 179, then the panel (opening 177, 176, 175, then none; closing 175, 176, 177, 178). All door cels are drawn at the elevator frame's origin + (37, 37), i.e. (9 + 13·(slotCol − L) + 37, 105), and are kept in the background. Departure open: player (cel 13) on top. Departure close: player (cel 22) between cab and panel. Arrival open: player (cel 23) between cab and panel. Arrival close: player (cel 18) on top. Details §5.5.

---------------------------------------------------------------------------------------------------

### 14.8 Open questions (sounds/draw)
* Sound meanings are from context only (not listened to).
* Whether the belly overlay can extend outside Telly's cel rectangle (erase uses Telly's rect only).
