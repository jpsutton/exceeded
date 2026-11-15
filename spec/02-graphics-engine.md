# 02 — Graphics data formats and rendering engine

Scope: ASEQ / ESEQ / RRGB / BINA / INTS resources in SSO1/2/3.DAT, NFNT fonts and cursors in
SSOWINCD.EXE, the TLC Keycaps font in SSO.FON; and the animation / screen-element / blit / palette
engine.

All multi-byte integers are **little-endian** unless stated otherwise. "s16" = signed 16-bit.

---------------------------------------------------------------------------------------------------

## 1. Summary of the display model (read this first)

* The game draws to a 512 × 384 pixel, 8-bit indexed canvas. It does **not** run at 640 × 480
  internally. The main window covers the whole desktop. The 512 × 384 picture is shown 1:1 (no
  scaling), centred in the client area, and the margins are filled with black. On a 640 × 480 desktop
  this gives a black border.
* There are two full-size 512 × 384 8-bpp buffers:
  * the **background buffer**: static scenery;
  * the **work buffer**: background plus sprites. This is the one shown on screen.

  Sprites are erased by copying back from the background buffer, then redrawn into the work buffer.
  Changed areas are tracked in a dirty map of 16 × 16 pixel tiles (32 × 24 tiles). Only dirty tiles
  are copied to the window.
* The game uses one palette: RRGB 128 in SSO1, entries 10..245. Entries 0..9 and 246..255 are the
  Windows static colours. The other 30 RRGB resources (SSO2, SSO3) are never loaded by the Windows
  executable (see §3).
* Every on-screen picture is a frame of an animated object (AO) that plays a script from an ASEQ
  resource. On each tick, an AO whose timer is due turns its current frame into one or more screen
  elements (SEs). An SE is one cel at one position with a z value. SEs are kept in a list sorted by
  z, and the renderer draws them from low z to high z.
* Time base: 100 ticks per second (timeGetTime()/10). An ASEQ frame lasts `100 / fps` ticks, using
  integer division: 10 fps gives 10 ticks (100 ms), and 8 fps gives 12 ticks (120 ms).

---------------------------------------------------------------------------------------------------

## 2. ASEQ — animation sequence resource

ASEQ resources are in SSO1 (36), SSO2 (26) and SSO3 (31). Resource IDs are unique across the three
files, and the loader searches all open DAT files. Resource sizes are rounded up to a multiple of
512 bytes, so ignore trailing padding.

### 2.1 Header

| off | type | name | meaning |
|---|---|---|---|
| 0 | u16 | `nseq` | number of sequences (scripts) |
| 2 | u16 | `ncel` | number of cels (images) |
| 4 | u16 | `fps` | frame rate. The engine's frame period is `100 / fps` ticks. Observed values: 10 (SSO1, SSO3) and 8 (SSO2). If the value is ≥ 0xFF the engine assumes the file is big-endian (Mac) and byte-swaps it. All shipped files are little-endian. |
| 6 | u16 | `w3` | unknown; 1 in SSO1, 15 in SSO2, 10 in SSO3. The Windows code never reads it. Possibly a Mac-era "default delay"/version. |
| 8 | u16 | `w4` | always 1. If it is > 8, the engine allocates `w4` words for the per-AO SE index array; otherwise it uses an inline array of 8. It looks like "the maximum number of simultaneous cels". |
| 10 | u32 × `ncel` | `celoff[]` | offset of each cel, relative to `celbase` (see below) |
| … | u32 × `nseq` | (reserved) | zero in the file. The engine overwrites these with pointers to each sequence at load time. |
| … | sequences | | `nseq` times: `u16 nent` followed by `nent` × 8-byte entries (§2.3) |
| `celbase` | cels | | cel data, located through `celoff[]` |

So `celbase = 10 + 4*ncel + 4*nseq + Σ(2 + 8*nent_i)`.

### 2.2 Cel (image) format

| off | type | meaning |
|---|---|---|
| 0 | u16 | `type`: 4 in every cel except ASEQ 12007, which uses 1. The 8-bit blitter does not read it (see Open questions). |
| 2 | u16 | `width` |
| 4 | u16 | `height` |
| 6 | … | RLE pixel data, rows from top to bottom |

The RLE data is a stream of byte codes. Each row is coded on its own, and no run crosses a row
boundary. Decoding of a row stops exactly when `width` pixels have been produced.

```
for y in 0 .. height-1:
    x = 0
    while x < width:
        c = next_byte()
        if c == 0x00:                 # transparent skip
            n = next_byte();  x += n                      # leave destination pixels alone
        elif c == 0xFF:               # solid fill
            colour = next_byte(); n = next_byte()
            put n pixels of `colour`; x += n
        else:                         # literal run, 1..254 pixels
            copy the next c bytes as pixels; x += c
```

* Transparency comes only from `00 n` skips. Every pixel value is opaque, including 0.
* Pixel values are palette indices. Observed range: 10..245 for nearly all art, which is exactly the
  game palette. A few cels use 0..9 / 246..255 (ASEQ 1011, 20000, 8605, 8534, 8604), meaning the
  Windows static colours (black, white, pure red/yellow/cyan …).
* There is no hotspot inside a cel. Positions come from the sequence entries: the top-left corner of
  the cel is placed at `AO.x + entry.dx, AO.y + entry.dy`.
* Horizontal mirroring is available per frame entry (§2.3, flag bit 0). The engine then draws the
  row from right to left starting at `x + width - 1`.
* The engine has three 8-bit blitters: bottom-up DIB, top-down DIB, and a left/right-clipped variant.
  All three decode exactly the format above. A 4-bpp (16-colour) blitter also exists; see Open
  questions.

The decoder checked every cel in all 93 ASEQs: every row ends exactly at `width`, and none
overruns.

### 2.3 Sequence entries (the animation script)

Each entry is 8 bytes: `s16 a, s16 b, s16 c, s16 d`.

* If `c ≥ 0` the entry is a **cel frame**: draw cel `c` at offset `(a, b)` from the AO position.
  Bit 0 of `d` set means draw the cel mirrored horizontally. In data, `d` is otherwise 0.
* If `c < 0` the entry is an **opcode**. Opcodes run immediately, within the same tick, until a cel
  frame is reached. Filler fields are usually −1.

Opcode table. "Next" means advance to the next entry and keep
interpreting. Unlisted values from −23 to −499 never occur.

| c | name | effect |
|---|---|---|
| −1 | END | Stop the AO (state = stopped), remove its SEs from the screen, set loop counter = 1. Nothing more happens until the game restarts the AO. |
| −2 | LOOP | Loop counter += 1; jump to entry 0 of the current sequence. |
| −3 | MULTI n=a | The next frame consists of the next `a` consecutive cel entries, drawn together. Each becomes its own SE; all share the AO's z. Used in the intro (ASEQ 1700). |
| −4 | PAUSE | AO state = "paused" (0x10): the AO stops advancing until the game resumes it. Index moves past the opcode and interpretation **continues in the same tick**: the next cel entry is drawn as the paused frame (then the AO waits). Example: ASEQ 1011 seq 0 `…(102,30) PAUSE (102,35) END` shows the panel at rest at y 35. Many SSO2/SSO3 sequences start with PAUSE, meaning "show the first frame and wait". |
| −5 | GOTO-LABEL a | If label `a` is the cached label, jump to the entry after it. Otherwise scan the sequence up to the END entry and jump to the **last** entry with `c == −7` **or** `a`-field == `a`. This is literal behaviour; the intended meaning is "goto label a". |
| −6 | SETPOS | `AO.x = a` unless `a == 0x8000`; `AO.y = b` unless `b == 0x8000`. When an AO starts, the leading SETPOS entries also set `AO.z = max(d, 0)`. Next. |
| −7 | LABEL a | Remember label `a` and the position after it (used by −5 and −20). Next. |
| −8 | MOVEX | `AO.x += a`. Next. |
| −9 | MOVEY | `AO.y += a`. Next. |
| −10 | SELECT-SEQ mode=a | 1: switch to a random sequence `rand15()*nseq/32768`. 2: switch to the next sequence, wrapping around. 3: switch to sequence `b`. 4: switch to sequence `b + rand15()*d/32768`. The switch restarts interpretation at entry 0 of the new sequence. `rand15()` is the C runtime `rand()` (0..32767). |
| −11 | JUMP a | Index = `a`. |
| −12 | CALLBACK | Call the AO's script callback, if one is set. Next, then end processing of this AO for this tick (no frame is drawn). |
| −13 | LINK-SHOW | Start or show the linked AO. |
| −14 | LINK-STOP | Stop the linked AO. |
| −15 | NOP | Next. |
| −16 | LINK-HIDE | Hide the linked AO. |
| −17 | LINK-POS | Linked AO position = this AO position + `(a, b)`. |
| −18 | TEXT | If the AO has attached text: redraw that text, and if `a ≠ 0` also destroy the AO. |
| −19 | SETCOUNT a | Repeat counter = `a`. |
| −20 | REPEAT | Repeat counter −= 1. If the result is ≠ 0, jump to just after the last LABEL. |
| −21 | DELAY a,b | Add `a + rand15()*(b−a)/32768` ticks (1/100 s) to the next-frame time. Next, then end this tick. |
| −22 | STAMP a | Stamp the next frame permanently into the background buffer: it is drawn into both buffers and never erased. If `a ≠ 0`, the AO's attached text is also stamped. No catch-up. Used by the intro to build up the transmitter tower. |
| ≤ −500 | USER code=c | Passed to the game's global AO hook as `(ao, c, a, b, d)`. Return 0: advance to the next entry. Non-zero: do not advance (the same entry is examined again). Bit 0 set: additionally rewind the index to where this tick's interpretation started. In all cases interpretation **continues in the same tick** (the tick does not end); a hook that keeps returning non-zero must change the AO's state itself. The game's sound-cue hooks return 0 (M). Observed: −500 and −501 with `a` = a SND.DAT WAVE id (for example 1727 in the intro), i.e. sound cues; and `−500` with a=0/5 in the ESEQ talk loops. The semantics are game-side. |

**Conventions in the data.** Most ASEQs end their single sequence with `−1`, or with
`(-1,-1,-1,-1)` for single-cel still images. Multi-sequence ASEQs often end with a last sequence that
simply lists every cel once (`(0,0,i,0)` for i = 0..ncel−1). This looks like a "cel picker" the game
uses to show a specific cel.

### 2.4 Playback algorithm (per tick)

```
now = ticks()                      # 1/100 s
for each AO in active-AO list (sorted by AO.z ascending):
    if now < AO.next_time: continue
    if AO.next_time == 0: AO.next_time = now
    late = now - AO.next_time
    AO.next_time = now + AO.period            # period = 100 / fps (can be changed by the game)
    catchup = 1 if AO.allow_catchup and late >= AO.period else 0
    if AO.state is not RUNNING: continue
    erase AO's previous SEs (restore background under them, mark dirty, flag overlapping SEs)
    count = 1                                  # cels in this frame (set by -3)
    loop:
        e = seq[AO.index]
        if e.c < 0: execute opcode (table above); continue
        if catchup: AO.index += count; catchup -= 1; continue   # drop one frame when late
        break
    for k in 0 .. count-1:                     # build screen elements
        e = seq[AO.index + k]
        rect = (AO.x + e.dx, AO.y + e.dy, cel[e.c].w, cel[e.c].h)
        clip rect against the AO's clip rectangle (full 512x384, or the game viewport rectangle)
        if fully outside: skip
        create SE {cel, rect, z = AO.z, flags: DRAW | (mirror?0x80) | (x-clipped?0x20) | ...}
        insert SE into the SE list after all SEs with z <= AO.z
    AO.index += count
then render all SEs (§5.3), then present the dirty tiles (§5.4)
```

### 2.5 ESEQ — external sequence sets

ESEQ is in SSO1 only (1000, 1001, 1002). An ESEQ has the same layout as the sequence part of an
ASEQ, but it has no cels:

| off | type | meaning |
|---|---|---|
| 0 | u16 | `nseq` (≥ 0x100 means byte-swapped) |
| 2 | u32 × nseq | reserved; becomes a pointer table at run time |
| … | | `nseq` × (`u16 nent` + `nent` × 8-byte entries) |

The game attaches an ESEQ to an already-loaded ASEQ, and any previous ESEQ is released. The ESEQ's
sequences are then selected with **negative sequence numbers**: sequence `−k` means ESEQ sequence
`k−1`. Their cel numbers refer to the host ASEQ's cels.
* ESEQ 1002 (7 sequences) belongs to the hall sprite set ASEQ 200, for example the player walking
  in/out at x=77.
* ESEQ 1000 (11 sequences) and ESEQ 1001 (3 sequences) are attached to the Master of Mischief sprite
  set (ASEQ 215 / 220) during the Decide ending. Sequence 0 of each is the door-knock light loop (a
  `−500` sound cue plus two cels at (469,59)/(479,63)); the other sequences are the tornado exit, his
  reactions and the zap / turn-into-a-TV award animations (chapter 07 Part B §2).

---------------------------------------------------------------------------------------------------

## 3. RRGB — palette

1536 bytes = 256 × {u16 R, u16 G, u16 B}, **big-endian** (Mac `ColorSpec`-style). Use the high byte
of each channel.

* The engine loads only **RRGB 128** (SSO1), once, at start-up.
  Entries 10..245 (236 colours) are copied into the logical palette with `PC_RESERVED`. In WinG mode
  they are also copied into the colour table of each WinG bitmap.
* RRGB 128 is in Windows order: 0 = black, 1..9 and 246..255 are the 20 Windows static colours, and
  255 = white. Every ASEQ in SSO1, SSO2 and SSO3 renders correctly with it, and visual checks confirm
  that SSO2/SSO3 art looks right with 128 and wrong with "its own" RRGB.
* The 29 SSO2 RRGBs (6001, 8500–8510, 8520–8531, 8533, 8540, 8600–8602, 10000) and RRGB 15000 (SSO3)
  are in **Mac order**: 0 = white, 255 = black, and 1–9 / 246–254 hold a magenta placeholder
  (FFFF,5B5B,FFFF). The Windows code never loads them; they are Mac leftovers. Several SSO2 RRGBs
  differ from 128 only at entry 245.
* The engine can also load a partial range: entries `[start, start+count)`, optionally read from the
  same offset in the resource. It can save/restore the "current palette id". No caller of either exists in this build, so there are **no palette fades or colour
  cycling** in this build.
* At start-up, if the system palette is in `SYSPAL_STATIC` mode, the game captures the 20 static
  colours (GetSystemPaletteEntries) to build the identity part of the palette.
* In 16-colour mode (Graphics=4, see §5.1) all palette calls are skipped.

---------------------------------------------------------------------------------------------------

## 4. Other resources

### 4.1 NFNT — bitmap fonts (EXE, 10 resources)

These are Macintosh `NFNT`/`FONT` resources converted for Windows. The header words and the location
table were byte-swapped to little-endian. The bitmap ("strike") and the offset/width table were
**not** swapped.

| off | type | field | notes |
|---|---|---|---|
| 0 | u16 | fontType | 0x9000 (all) |
| 2 | u16 | firstChar | 0 |
| 4 | u16 | lastChar | 252 or 255 |
| 6 | s16 | widMax | maximum advance |
| 8 | s16 | kernMax | most negative left offset (−1…−6) |
| 10 | s16 | nDescent | |
| 12 | s16 | fRectWidth | |
| 14 | s16 | fRectHeight | = glyph bitmap height (all rows) |
| 16 | u16 | owTLoc | the offset/width table is at byte `16 + 2*owTLoc` |
| 18 | s16 | ascent | baseline = row `ascent` of the strike |
| 20 | s16 | descent | |
| 22 | s16 | leading | |
| 24 | u16 | rowWords | strike row length in 16-bit words |
| 26 | bytes | strike | `fRectHeight` rows × `rowWords*2` bytes, 1 bpp, MSB = leftmost pixel |
| … | u16 × (last−first+3) | locTable | x bit-offset of each glyph in the strike. Glyph i spans `[loc[i], loc[i+1])`. Index last−first+1 is the "missing glyph". The final sentinel is garbage in some fonts, so clamp to `rowWords*16`. |
| 16+2·owTLoc | 2 bytes × (last−first+3) | owTable | per glyph: byte0 = left offset (add kernMax), byte1 = advance width. `FF FF` = glyph not present. |

Text rendering:
* For each character, take its offset/width entry. If it is missing, use the missing glyph.
* Copy the glyph's 1-bpp strike columns to `pen_x + kernMax + offset`, `pen_y − ascent`. Every set bit
  writes one pixel of a single solid colour. Clear bits are transparent, and pixels are clipped to the
  destination rectangle.
* Advance the pen by the width byte. There is no anti-aliasing, outline or shadow; shadowed text, if
  any, is done by the caller drawing twice.
* Up to 5 fonts can be loaded at once. The game's font handles: **handle 0 / "normal" = NFNT 28625**
  (loaded first; any font handle never assigned is 0, so it means this font), **"large" = NFNT 28568**,
  **"medium" = NFNT 28558** (two handles hold it). Screens that need others load them explicitly
  (15001 sign-in, 19637 challenge titles, 25240 map labels). Chapter 07's "normal ending font" =
  28625, "large UI font" = 28568, FL = 28568, FS = 28558.
* Colours are 16-bit "dual" colour values: low byte = 256-colour palette index, high byte = index
  used in 16-colour mode. Colour −1 means the current default colour.
* Character set: Mac Roman (é = 0x8E etc.). Font 15001 contains extra **key-cap glyphs** at
  0x06 (R), 0x17 (S), 0x80–0x9A (arrow keys, Space/bar, Esc, return, back/space, Enter, Ctrl, Tab,
  Delete, ⌘). Some key caps are split across two codes, e.g. "Spac"+"ebar"; the halves are printed
  side by side.

| NFNT id | height (fRectH) | ascent/descent/leading | widMax | look |
|---|---|---|---|---|
| 15001 | 21 | 17/4/0 | 46 | sans "Chicago-like" 12 pt + key caps |
| 15052 | 31 | 25/6/2 | 32 | large heavy sans (titles) |
| 19637 | 22 | 18/4/0 | 18 | serif (Times-like) |
| 23377 | 24 | 19/5/1 | 24 | bold rounded sans (big) |
| 25240 | 18 | 15/3/0 | 14 | bold serif (New York bold) |
| 26944 | 19 | 15/4/1 | 19 | bold sans |
| 28556 | 13 | 10/3/0 | 16 | small serif |
| 28558 | 16 | 12/4/0 | 34 | serif |
| 28568 | 24 | 19/5/0 | 29 | large serif |
| 28625 | 16 | 13/3/1 | 16 | small bold sans |

### 4.2 SSO.FON — "TLC Keycaps"

This is a standard Windows 3.x FNT (version 0x200) inside an NE .FON file, with one font:
* Face "TLC Keycaps", copyright "(c) Copyright TLC 1995".
* 10 pt, weight 700 (bold), proportional, 17 px high, ascent 13, ANSI charset.
* Characters 0x20–0xFF, default char 0x60.
* Bitmaps are column-major 8-pixel strips.

At start-up the game registers it with `AddFontResource("sso.fon")` and broadcasts
WM_FONTCHANGE. It is used only by Windows dialogs/menus (key icons inside dialog text).

| code | glyph |
|---|---|
| 0x20–0x7E | ordinary bold sans ASCII |
| 0x7F | ← key cap (16 px) |
| 0x80 | ↑ key cap |
| 0x81 | → key cap |
| 0x82 | key cap with two horizontal bars (probably ↓ / "menu"; unclear) |
| 0x83 | "C" key cap |
| 0x84 | "Spacebar" key cap (52 px) |
| 0x85 | "Backspace" key cap (59 px) |
| 0x86 | "Enter" key cap (31 px) |
| 0x87–0x9F | thick vertical bar (unused filler; 0x91/0x92 = quotes) |
| 0xA0–0xFF | Latin-1 |

### 4.3 Cursors (EXE)

There are seven 32 × 32 monochrome cursors, CURSOR 1–7 in GROUP_CURSOR 128–134 (one image each).
Standard AND/XOR monochrome cursors: AND=0 → opaque (XOR gives black/white), AND=1/XOR=0 → transparent, AND=1/XOR=1 → inverted screen.

| group | image | hotspot | shape (likely use) |
|---|---|---|---|
| 128 | 1 | (24,16) | arrow → (walk right / exit right) |
| 129 | 2 | (26,7) | arrow ↑ (enter door / go up) |
| 130 | 3 | (7,15) | arrow ← |
| 131 | 4 | (8,8) | arrow ↖ |
| 132 | 5 | (23,8) | arrow ↗ |
| 133 | 6 | (16,16) | filled disc |
| 134 | 7 | (16,16) | ring |

The standard IDC_ARROW (0x7F00) is also used. Which in-game situation uses which cursor belongs to
chapter 03 §6.3.

### 4.4 BINA 5000 and INTS (not graphics)

* **BINA 5000** (SSO1, 1914 bytes used) is a block of pre-initialised game tables. At game start it is copied straight into the data segment in three pieces:
  * bytes 0x000–0x37F: world hot-spot table (16 areas × 4 slots × 14 bytes)
  * bytes 0x380–0x70D: animation-state table (35 × 26 bytes)
  * bytes 0x70E–0x779: initial object table (6 × 18 bytes)

  The pieces hold 14-, 26- and 18-byte records respectively. They are
  Their meaning is specified in the action-game chapter (04).
* **INTS 1700–1703** (SSO1): lists of u16 values terminated by 0xFFFF. The values are SND.DAT WAVE
  ids (1700–1769), i.e. sound playlists:
  * 1700: 19 ids
  * 1701: 14 ids
  * 1702: 8 ids
  * 1703: 3 ids

  See the audio section of chapter 03.

---------------------------------------------------------------------------------------------------

## 5. Rendering engine

### 5.1 Video modes and the `Graphics=` INI key

The INI value `Graphics` (default 3) selected the original's presentation path:

| value | path | presentation |
|---|---|---|
| 1 | DIB | `StretchDIBits` of the 512 × 384 bottom-up DIB, 1:1, DIB_RGB/PAL colours |
| 2 | DDB | `BitBlt` from a compatible bitmap/memory DC; only on exactly-8-bpp displays |
| 3 | WinG (default) | loads WING.DLL. If that fails, mode = fallback (1). Uses WinGCreateDC/Bitmap and WinGBitBlt; a top-down DIB is possible, so there are separate top-down blitters |
| 4 | 16-colour | chosen automatically when the display is 4-plane/1-bpp or 1-plane/4-bpp. 4-bpp work buffers (256 bytes per row) and 16-colour blitters; palette calls are skipped |

* Requests on displays with fewer than 8 bpp fall into mode 4, or 0 (unsupported).
* On displays with more than 8 bpp, mode 2 falls back to 1.
* A reimplementation only needs the logic itself: an indexed 512 × 384 framebuffer, the
  RRGB 128 palette, and nearest-neighbour presentation.

### 5.2 Buffers and drawing targets

* `work` (front) and `back` (background) are both 512 × 384 × 8 bpp. The stride is 512, and row 0 is
  the top of the picture; in DIB mode the memory is bottom-up.
* There are two drawing-target settings:
  * normal: draw into `work`, restore from `back`;
  * "into background": draw into `back`.

  A background picture, such as a room image or the panel frame 1000, is drawn by stamping it into
  `back` and then copying the rectangle to `work`, or by drawing it into both.
* **Erase** = copy a rectangle `back → work`, then mark it dirty.
* **Commit** = copy a rectangle `work → back`.
* **Direct drawing persists.** Besides SEs, game code draws straight into `work`: text, filled
  rectangles, recolouring, and stamped cels without the "also background" flag (chapter 07 Part A §0).
  These pixels stay on screen until something overwrites them, or until an erase restores that area
  from `back`, for example when an overlapping SE moves or is removed. So do not rebuild `work` as
  "back + all SEs" every frame. Keep `work` persistent, and erase/redraw only what the SE rules in §5.3
  say. (Rebuilding from scratch would wipe direct-drawn text and boxes, such as prompts, HUD digits and
  math-challenge charts drawn into `work` only.)

### 5.3 Screen elements (SE)

There is a fixed pool of **100** SEs. A debug warning ("Maximum SE Used") appears at 50, and the
game asserts if the pool runs out. SE fields: index; z; flags; clipped rectangle (x, y, w, h); cel
pointer; unclipped draw origin (x, y); owner AO.

SE flags:

| bit | meaning |
|---|---|
| 0x0001 | needs drawing |
| 0x0002 | temporary: erase and free it before the owner's next frame (set unless the owner is paused) |
| 0x0004 | needs redraw (externally requested) |
| 0x0008 | also stamp into the background buffer (from opcode −22) |
| 0x0010 | do not propagate overlap / do not erase (owner flag) |
| 0x0020 | cel is horizontally clipped, so use the clipped blitter |
| 0x0040 | overlapped by a changed SE, so redraw it this pass |
| 0x0080 | mirrored horizontally |
| 0x0200 | clip to the alternate rectangle (game viewport) instead of 0,0–511,383 |
| 0x0400 | call the owner's pre-draw hook |
| 0x0800 | call the owner's post-draw hook (custom drawing on top, e.g. text) |

The SE list is sorted by **z ascending**, and an insert goes after existing SEs with equal z. The
render pass goes through the list in order, so a higher z appears on top and equal z means
"later-created on top". For each SE with flags & (0x01 | 0x04 | 0x40):
1. Set the clip rectangle: full screen, or the viewport if 0x200.
2. If 0x01 or 0x04: mark its rectangle dirty.
3. Pre-draw hook, if any.
4. Blit the cel with the RLE decoder: normal, mirrored, or x-clipped. A cel that is both mirrored and
   x-clipped is **not drawn** (an engine quirk).
5. If 0x08: draw it again into `back`.
6. Post-draw hook, if any.
7. Unless 0x10: mark every later SE whose rectangle intersects this one with 0x40, so that it is
   redrawn on top.
8. Clear 0x01, 0x04 and 0x40.

Before an AO advances, its old SEs are erased: restore `back → work` over each one, flag every SE
whose rectangle intersects with 0x40, and free the temporary SEs. So moving one sprite redraws only
the sprites that overlap it.

### 5.4 Presentation (dirty tiles)

* The dirty map is 24 rows × 32 bytes, one byte per 16 × 16 tile.
* A "mark dirty" call clamps its rectangle to 0..511 × 0..383 and sets the tiles it covers. A null
  rectangle means "everything", and a full-screen blit follows.
* Presentation (once per game-loop iteration) processes each tile row. It finds
  horizontal runs of dirty tiles (each run up to 32 tiles), blits each run (run × 16 px wide, 16 px
  tall) to the window at `(origin_x + x, origin_y + y)`, and clears the map.
* `origin = ((clientW − 512)/2, (clientH − 384)/2)`.
* On WM_PAINT, the black border is refilled and the whole canvas is blitted.

### 5.5 Clip rectangles

* The default AO clip rectangle is (0, 0, 512, 384).
* Each AO can instead be clipped to an alternate "viewport" rectangle that the game sets.
  Room and hall scenes use it so that sprites stay inside the black window of panel frame 1000. That
  window is about x 8–503, y 35–283. The 492 × 247 room backgrounds fill it.
* Clipping returns a bit for "clipped horizontally". Vertical clipping is handled inside every
  blitter by skipping rows outside [clipTop, clipBottom].

### 5.6 Animated objects (AO) — engine API summary (behavioural)

* **create(aseq_id)**: loads the ASEQ resource. It is reference-counted, and the parsed header is
  shared between AOs using the same id. Sets `period = 100/fps` and `sequence = 0`. Each new AO gets
  a serial id. Its "level/owner" tag lets all AOs created by a screen be destroyed
  together.
* **start(seq)**: sets the sequence, index 0, state RUNNING, next_time = 0 (draw on the next tick).
  Inserts the AO into the active list, sorted by z.
* **set sequence** (negative = ESEQ), **set position**, **set z**, **set period/fps**, **stop**, **hide** (erase
  SEs), **link** another AO (for opcodes −13…−17), **set script callback** (opcode −12), **set
  pre/post draw hooks**, **attach text** (opcodes −18, −22).
* **Global hooks**:
  * a per-tick "before/after animation" hook;
  * the USER-opcode hook for codes ≤ −500.
* **Per-frame loop**: run all due AOs (§2.4) → render SEs (§5.3) → run the
  registered after-render callbacks → present (§5.4).

---------------------------------------------------------------------------------------------------

## 6. Resource catalogue (ID → content)

Sizes are cel width × height. "seqs" gives `nseq`/`ncel`.

### SSO1.DAT (fps 10)

| id | cels | content |
|---|---|---|
| 200 | 213 | **Hall master sprite set**. Cels:<br>• 0–49: player (blue hoodie, red cap, zapper): walk, turn, jump/flip, fall, knocked over, dizzy<br>• 50–55: zapper beam cone<br>• 56–63: 1-px strips<br>• 64–73: clock digits 0–9 (8×13)<br>• 74–77: "MIDNIGHT", red MIDNIGHT, "PAUSE", time box ": P.M."<br>• 78–97: the 20 decoder pictures (41×29: camcorder, mug, tower, clipboard, phone, …; picture id = cel − 78)<br>• 99: player<br>• 100–114 and 180–192: **Telly** robot (walk, lit/unlit, zapped, crashing to pieces)<br>• 115–116: lightning<br>• 117–121: room name plates News/Game/Cartoon/Equipment/Sound<br>• 122: elevator<br>• 123: wall/railing<br>• 125–133: hall props (mic stand, camera, director's chair, cactus, sofa+pictures, TV, plant, boom camera, SGTV news desk)<br>• 134–136: room door closed/opening/open<br>• 137–174: 13-px hall wall/column slices (scrolling hall)<br>• 175–179: elevator doors opening<br>• 183–185: sparks<br>• 189–190: thrown "tricks"<br>• 193–194: discs<br>• 195: **energy supply machine**<br>• 196–202: energy beam frames<br>• 203: player figure (also used as an invisible placeholder cel for room-object hot spots)<br>• 204–208: red blobs<br>• 209–212: "ZAP!", "BLAST!", "BOOM", "CRASH!" |
| 205 | 14 | Orange-toned (palette 10–25 only) drill screen set: Telly hands, burst, 3 TV-static backgrounds, ticket animation, "Time/Problems/Correct" box. Likely the Drill-for-Skill / low-colour variant of SSO2 8600–8605. |
| 210 | 19 | **LiveWire** (electric cord: crawl, spark, coil, vanish), security camera rotating (3–7), player electrocuted skeleton (8–9), VCR/tape bar, calculator (205×115), "QUESTION:" panel with "Your Answer" (205×275), small tape icon |
| 215 | 57 | **Master of Mischief** (Morty Maxwell): stand, run, whirlwind teleport, sparks, door, speech balloon (192×132), electrified, transformation into a TV-robot (Telly-maker), fireballs, smoke |
| 220 | 14 | Ending/caught sequence: lightning bolt strikes, player falling into a hole, cap/zapper flying, speech balloons (256×102, 192×132) |
| 225 | 17 | **Score-summary art**: gold trophy statue of player on a TV (Game/Total shown on its screen), rank ladder, red rank markers; extra beam animation for Champions (chapter 07 Part B §3.3) |
| 1000 | 1 | **Main game panel frame** 512×384: blue bezel, black viewport, bottom message area, Time/Energy/Score boxes |
| 1001 | 1 | NEWS ROOM background 492×247 |
| 1003 | 1 | GAME SHOW ROOM background (Prize is Right, Spinner, jukebox) |
| 1004 | 1 | CARTOON ROOM background (line graph, piano, "Previews!" poster, bookcase) |
| 1005 | 1 | EQUIPMENT ROOM background (shelf, calendar, dress form, TV) |
| 1006 | 1 | SOUND ROOM background (tape deck, jukebox, guitar, monitor) |
| 1007–1010 | 2 each | Decoder-screen buttons, normal/pressed: Decoder, Station, Match Rule, Decide |
| 1011 | 1 (3 seqs) | 307×115 message panel (system colours) that slides in/out vertically (seq 0 down, seq 1 up, seq 2 still) |
| 1012 | 1 | "Continue" button |
| 1013–1020 | 28–50 | Morty talking-head lip-sync animations (59×~51), 8 speeches |
| 1021 | 1 | small Morty figure (38×66) |
| 1022 | 1 | hall wall section (74×115) |
| 1500–1502 | 1 each | orange-toned 77×81 pictures: Morty-robot, energy machine, player zapping (palette 10–25) |
| 1503 | 1 | orange 192×36 strip of 4 code symbols |
| 1700 | 28 (3 seqs) | **Intro**: title card "The Learning Company presents OUTNUMBERED!" 512×384 (cel 0); tower beacon and blast; player running and zapping; TV-station doors; "Shady Glen Television Station" exterior 512×384 (cel 27). Sound cue opcodes reference SND 1727. |
| 5500 | 1 | Telly close-up frame 512×283 (no use found in the Windows build; the drill screen uses SSO2 8600) |
| 7000 | 1 | security camera (51×50) |
| 7010 | 1 | clipboard with lined paper 512×384 (no use found in the Windows build) |

### SSO2.DAT (fps 8) — math-challenge close-ups and decoder

| id | cels | content |
|---|---|---|
| 6000 | 2 | **Decoder** screen 489×250: Secret Code padlock with 4 slots and 5 room rows × 4 clue slots (2 variants) |
| 6002 | 8 | decoder buttons (Decoder/Match Rule/Decide/Station × normal/lit) |
| 8500 | 1 | challenge: US **map** |
| 8501 | 2 | challenge: **bar graph** grid (orange) |
| 8502 | 1 | challenge: "Prize is Right" **prizes** A–D |
| 8503 | 2 | challenge: **Great Eight** chart (Song) |
| 8504 | 1 | challenge: **volume** meters 100s/10s/1s |
| 8505 | 1 | challenge: **Spinner** |
| 8506 | 1 | challenge: **monitor** (blank TV) |
| 8507, 8508 | 1 each | challenge: **line graph** grids |
| 8509 | 1 | challenge: **Equipment List** table |
| 8510 | 1 | challenge: **VCR tapes** shelf |
| 8511 | 4 | Great Eight "Song" header animation |
| 8520 | 10 | spinner pointer positions (sequence moves it around the dial) |
| 8530 | 1 | VHS tape label bar |
| 8531, 8534 | 1 each | on-screen calculator (full / without ÷×−+ column) |
| 8533 | 1 | small red tape |
| 8540 | 1 | numeric keypad (ENTER 7 8 9 C 4 5 6 0 1 2 3) |
| 8551 | 1 | "Cancel" button |
| 8600 | 1 | **Drill questions**: Telly close-up holding its screen 485×246 |
| 8604 | 13 | drill: Telly hands, flying tickets, burst, 3 TV-static backgrounds |
| 8605 | 1 | drill "Time / Problems / Correct" box |
| 10000 | 1 | QUESTION panel + calculator (202×302) |
| 20000 | 2 | "Continue" button lit/unlit (72×22) |

### SSO3.DAT (fps 10) — sign-in and intro narration

| id | cels | content |
|---|---|---|
| 10001 | 6 (2 seqs) | Telly throwing a disc |
| 11001–11009 | 7–8 | large Morty heads, talking/expressions (94×~95) |
| 11010, 11011 | 1 | Morty head still (bobbing sequences) |
| 12001–12006 | 4–16 | Morty full-body (lab coat) gesturing/pointing, startled |
| 12007 | 2 | flying disc (cel type 1) |
| 12008 | 8 | Telly head with electric antenna |
| 15000 | 1 | **Master of Mischief's lair** 514×386, background of the intro narration (chapter 07 Part B §1.2) |
| 16000 | 1 | **Sign-In screen**: computer monitor 512×384 with list box, Start Game / New Player / Cancel / Drill for Skill |
| 16001, 16002, 16003, 16009 | 3 (2 seqs) | Sign-In buttons START GAME / NEW PLAYER / CANCEL / DRILL FOR SKILL: normal, highlighted, disabled |
| 16004 | 1 | list-box close box |
| 16005–16008 | 2 | scroll arrow up/down, normal/pressed |

ASEQ ids used by the game screens include: 1007, 1011/1021, 1700, 16000 and
16004+, 15000/10001/12006–12008, 8551 and 6002, 8511/8520/8533, and 200.

---------------------------------------------------------------------------------------------------

## 7. Open questions

1. **Cel `type` field (4 vs 1)**: the 8-bit path ignores it. Only ASEQ 12007 uses 1. It may be a
   Mac-era "has transparency"/compression flag.
2. **16-colour mode (Graphics=4)**: the 4-bpp blitter reads a 4th header word as a nibble
   transparency key. Shipped cels do not have that word, so either 16-colour mode needs converted
   data, or it was never finished. The orange, palette-10–25-only assets (ASEQ 205, 1500–1503) hint
   at a low-colour art set, but their use was not traced. Recommendation: ignore mode 4.
3. ASEQ header words `w3` (1/15/10) and `w4` (always 1) have no confirmed purpose.
4. The exact game-side meaning of USER opcodes −500/−501 (they look like sound cues) and of the
   CALLBACK hooks is defined by the game modules, not the engine.
5. The exact viewport rectangle (alternate clip rectangle) and the placement of the 492×247 room
   images inside panel 1000 were estimated from pixels (≈ x 8–503, y 35–283), not read from code.
6. SE flag 0x0004 (needs redraw) is set by game code outside the animation engine; its exact uses were not traced. Treat it as "force redraw".
7. The usage mapping of cursors 128–134 and of the many hall cels (exact cel indices per action) is
   left to chapters 04/07.

---------------------------------------------------------------------------------------------------
