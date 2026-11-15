# 01 — Data files and containers

## 1. Files on the CD (directory `SSOWINCD\`)

| file | size | kind | used for |
|---|---|---|---|
| `SSOWINCD.EXE` | 407,056 | Win16 NE executable | the original program. A port reads only its **resources**: DIALOG (96), MENU (18), NFNT fonts (10), CURSOR/GROUP_CURSOR (7), ICON, and an RT_NAMETABLE |
| `SSO1.DAT` | 2,637,972 | NE resource DLL | main art: hall/room sprites, rooms, panel, intro, palette RRGB 128, BINA 5000 world tables, INTS sound lists, TEXT speech-bubble lines, ESEQ |
| `SSO2.DAT` | 1,048,994 | NE resource DLL | decoder, math-challenge art, drill art, 80 MTHT word-problem templates, (unused) RRGBs |
| `SSO3.DAT` | 997,524 | NE resource DLL | sign-in screen, intro narration art (Master of Mischief's lair, his heads/poses, Telly close-ups), 2 intro WAVEs |
| `SND.DAT` | 1,361,044 | NE resource DLL | 60 sound-effect WAVEs, 8 TEXT resources naming the MIDI songs |
| `SPEECH.DAT` | 9,348,756 | NE resource DLL | 68 voice WAVEs |
| `SSO.FON` | 32,544 | Windows .FON (NE) | "TLC Keycaps" bitmap font with key-cap glyphs (dialog text) |
| `MIDI\B*.MID`, `MIDI\E*.MID` | | SMF format 0, 1 track, 480 PPQ | music; B = base-level synth arrangement, E = extended (GM-like) |
| `SSOWINCD.DAT` | 18,140 | binary | blank player database (template); format in chapter 03 §4 |
| `SSOWINCD.INI` | 37 | INI | `[OutNumbered!] Graphics=3 WaveMix=1` (chapter 03 §1) |
| `WAVEMIX.DLL`, `WAVEMIX.INI` | | Microsoft WaveMix | not needed by a port |
| (root) `SETUP.EXE`, `*.Z`, `TLC.1`, `~INS0762.LIB`, `SETUP.INS/PKG` | | InstallShield | not needed (WING.Z = WinG runtime) |

The .DAT files contain a stub code segment and no meaningful code; treat them as resource archives.
Resource IDs of each type are unique across SSO1/SSO2/SSO3; the original searches all of them.

## 2. NE resource container (the public Windows 3.x "New Executable" format)

Reading resources only requires this subset:

1. At file offset 0x3C read u32 `ne_off`. At `ne_off` the signature is `"NE"`.
2. At `ne_off + 0x24` read u16 `rsrc_rel`; the resource table starts at `ne_off + rsrc_rel`.
   (`ne_off + 0x26` is the resident-name table offset; the resource table ends before it.)
3. Resource table:
   ```
   u16 align_shift
   repeat:
     u16 type_id            0 terminates the table
     u16 count
     u32 reserved
     count × {
       u16 offset           file offset = offset << align_shift
       u16 length           byte length = length << align_shift (rounded up; trailing padding)
       u16 flags
       u16 id               bit 15 set: integer id = id & 0x7FFF; else offset of a name string
       u32 reserved
     }
   ```
   `type_id` with bit 15 set is an integer type (`& 0x7FFF`); otherwise it is the offset (from the
   start of the resource table) of a Pascal string (length byte + chars) naming the type.
4. Standard integer types used: 1 CURSOR, 3 ICON, 4 MENU, 5 DIALOG, 7 FONTDIR, 8 FONT,
   12 GROUP_CURSOR, 14 GROUP_ICON, 15 NAMETABLE, 16 VERSION.

### 2.1 Custom type names (RT_NAMETABLE, type 15)

All custom types in these files are integer types `0x7F01, 0x7F02, …` whose names are defined by the
file's type-15 resource. Entries:
```
u16 entry_length (0 = end)   u16 type_ordinal (0xFF01 = custom type 0x7F01, …)
u16 id_ordinal               char type_name[] NUL   char id_name[] NUL
```
Build `type_ordinal & 0x7FFF → type_name` from it. The resulting mapping (identical for every copy of
the game, so it may also be hard-coded):

| file | 0x7F01 | 0x7F02 | 0x7F03 | 0x7F04 | 0x7F05 | 0x7F06 |
|---|---|---|---|---|---|---|
| SSO1.DAT | ASEQ (36) | BINA (1) | ESEQ (3) | INTS (4) | RRGB (1) | TEXT (24) |
| SSO2.DAT | ASEQ (26) | MTHT (80) | RRGB (30) | | | |
| SSO3.DAT | ASEQ (31) | RRGB (1) | WAVE (2) | | | |
| SND.DAT | TEXT (8) | WAVE (60) | | | | |
| SPEECH.DAT | WAVE (68) | | | | | |
| SSOWINCD.EXE | NFNT (10) | | | | | |

The game always addresses resources as (type name, integer id) and searches the open files.

## 3. Resource types — where each is specified

| type | format | spec |
|---|---|---|
| ASEQ | animation: cels (RLE images) + sequence scripts | 02 §2 |
| ESEQ | extra sequence scripts attached to an ASEQ | 02 §2.5 |
| RRGB | palette, 256 × 3 big-endian u16 | 02 §3 |
| NFNT | Mac-style bitmap font | 02 §4.1 |
| BINA 5000 | world hot spots, animation states, initial objects | 04 §3–4, appendix A |
| INTS | u16 list of SND WAVE ids, terminated by 0xFFFF | 03 §5.3 |
| TEXT | NUL-terminated text; see §4 | here |
| WAVE | complete RIFF/WAVE file, PCM 8-bit unsigned mono | here |
| MTHT | word-problem template program | 06 |
| DIALOG, MENU | standard Win16 resources | §5 |
| CURSOR/GROUP_CURSOR | standard Win16 monochrome cursors 32×32 | 02 §4.3 |
| FONT (in SSO.FON) | Windows 2.x/3.x .FNT bitmap font | 02 §4.2 |

## 4. TEXT, WAVE and MIDI

* **SSO1 TEXT 500–523**: speech-bubble text (512-byte resources; NUL-terminated, lines separated by
  CR/LF or CR). 500–507 = the player's taunts at the hideout door, 508–515 = the Master of Mischief
  caught, 516–523 = the Master of Mischief winning. Each has a spoken counterpart (chapter 03 §5.4).
* **SND TEXT 1717, 1724, 1728, 1729, 1735, 1745, 1746, 1757**: MIDI file names with a leading space,
  e.g. `" enter.mid"`. The engine replaces the first character with `b` or `e` and opens it from the
  MIDI folder: `MIDI\BENTER.MID` / `MIDI\EENTER.MID` (file names are case-insensitive on the CD).
* **WAVE** resources are complete RIFF files: `fmt ` PCM, 1 channel, 8 bits, 11025 Hz (19 files) or
  22050 Hz (111 files). Trailing bytes after the `data` chunk are resource padding.
* **MIDI** files: SMF format 0, one track, 480 ticks per quarter note. BLEVEL1/BLEVEL2 and
  BROOM1/BROOM2 (and the E pairs) have identical sizes: the "2" version is the same arrangement
  transposed up one semitone (note numbers +1). Play them as the separate files they are.
  Looping is done by the engine (restart at end), not by markers in the file.

## 5. DIALOG and MENU resources (Win16 format)

The port needs the text and the control layout of the dialogs. Units are Windows *dialog units*
(x × font-avg-width/4, y × font-height/8); with the dialogs' font (TLC Keycaps 11 pt, or MS Sans
Serif 8 pt for the system-error dialogs) 1 DLU ≈ 1.5–2 px. A port may lay dialogs out freely, but
must keep the text, buttons and their ids.

**DIALOG template (Win16):**
```
u32 style              (bit 0x40 DS_SETFONT)
u8  item_count
s16 x, y, cx, cy
menu   : 0x00 = none | 0xFF u16 ordinal | NUL-terminated name
class  : NUL-terminated (empty = default)
caption: NUL-terminated
if DS_SETFONT: u16 point_size, NUL-terminated face name
item_count × {
  s16 x, y, cx, cy;  u16 id;  u32 style
  class: one byte ≥ 0x80 (0x80 BUTTON, 0x81 EDIT, 0x82 STATIC, 0x83 LISTBOX, 0x84 SCROLLBAR,
         0x85 COMBOBOX) or a NUL-terminated name
  text : 0xFF u16 ordinal | NUL-terminated string  (\n = line break, & = mnemonic)
  u8 extra_bytes; extra data
}
```
Button style low nibble: 0 push, 1 default push, 3 auto-checkbox, 4 radio (groups start with
WS_GROUP 0x00020000). STATIC text is left-aligned (style & 3 = 0), centred (1), or right-aligned (2).
Characters 0x7F–0x86 inside dialog text are key-cap glyphs from TLC Keycaps
(← ↑ → = C Spacebar Backspace Enter; chapter 03 §6.5): render them as key icons.

**MENU template (Win16):** `u16 version(0), u16 header_size(0)`, then items: `u16 flags` (0x10
POPUP, 0x80 last item at this level, 0x08 checked, 0x01 grayed), non-popup items then `u16 id`,
then a NUL-terminated caption. Popups are followed by their children.

Dialog ids used by the game and their purposes are catalogued in chapter 03 (flows), 05 (rules) and
06 (calculator messages). The name table maps DIALOG ordinal 1 to the name `ALLSTAR_DLG`.

## 6. Data the port must keep in its own code

Everything else (all numeric tables quoted in chapters 04–06 that are not inside BINA 5000, e.g.
the per-tier table, city coordinates, rank thresholds, patterns, strings such as praise lines and
room names) lived in the original program's data segment and is reproduced in these chapters. The
English strings the port needs that are not in resources:

| purpose | text |
|---|---|
| praise (random of 5) | "That's correct!", "That's the answer!", "That's right!", "You're right!", "Good job!" |
| drill wrong | "That's incorrect." |
| drill failed set | "No Code.   Catch me again!" |
| math challenge wrong 1 | "That's not correct." / "Read the question;" / "study the <word>." |
| math challenge wrong 2 | "HINT: " + hint |
| math challenge wrong 3 | "That's not correct.  The correct answer is <n>." |
| study-the words (object 0–9) | map, graph, prizes, spinner, chart, settings, list, chart, graph, tapes |
| room names | NEWS ROOM, GAME SHOW ROOM, CARTOON ROOM, EQUIPMENT ROOM, SOUND ROOM |
| decide prompt | "Click on the name of the room that you think is the Master of Mischief's hideout." |
| rank names | Trainee, Junior, Senior, Pro, Expert, Master, All-Star, Champion |
| bonus screen | "B O N U S", "Energy Left: ", "Time Left: ", "Winning Game: " |
| score screen | "Points", "Rank", "Game", "Total" (the " to Continue"/" to Go Back" strings are unused; the Continue button ASEQ 1012 is used instead) |
| drill prompt | " to Explore TV Station", "to Continue" |
| great-8 headers | "Last/Wk's/Pos.", "Weeks/on/Chart", "Calls/This/Week", "Calls/Last/Month"; "Great Eight" |
| spinner | "SPIN", "LAST" |
| bar graph $ titles | "Cassette Cost", "Cost of Video Tape" |
| map cities | see chapter 06 §8.0 |
| window title / about | "OutNumbered! CD", "About OutNumbered!" |
