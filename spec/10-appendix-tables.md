# 10 — Appendix A: fixed tables

## A.1 BINA 5000 (SSO1.DAT) — world, animation-state and object tables

The port should **read these from the resource** (layout below); the dump is given for checking.
All fields are s16 little-endian.

* bytes 0x000–0x37F: hot spots, `[16 areas][4 slots]` × 7 s16 = `type, col, y, field3, cel, field5, field6`
* bytes 0x380–0x70D: animation states, `[35]` × 13 s16 = `nframes, curframe, cel[9], tick_counter, ticks_per_frame`
* bytes 0x70E–0x779: initial objects, `[6]` × 9 s16 = `x(col), y, cel, f6, f8, f10, active, hit_offset, hit_width`

Cel ids < 10000 are cels of SSO1 ASEQ 200 (hall sprite set); ids ≥ 10000 are cels `id − 10000` of
SSO1 ASEQ 210 (LiveWire/room set). Hot-spot type codes: −1 empty, 0 plain door, 1 room door (hall) /
exit door (room), 2 elevator, 6 floor-end wall, 9 energy machine (inserted at run time),
11..20 math-challenge object (object index = type − 11). The energy machine replaces one of the
candidate plain doors at run time (A.2), so the in-memory copy is modified per game.

```
   fields: type, col, y, field3, cel, field5, field6   (type 11..20 = math-challenge object id)
area  0 slot 0: (6, 0, 54, 51, 123, 0, 0)    end wall
area  0 slot 1: (2, 6, 68, 282, 122, 77, 0)  elevator
area  0 slot 2: (0, 23, 63, 448, 129, 294, 0) plain door
area  0 slot 3: (-1, 0, 0, 0, 0, 0, 0)       empty
area  1 slot 0: (0, 3, 63, 128, 131, 38, 0)  plain door
area  1 slot 1: (1, 14, 76, 294, 117, 179, 0) room door/exit
area  1 slot 2: (0, 25, 67, 422, 132, 320, 0) plain door
area  1 slot 3: (-1, 0, 0, 0, 0, 0, 0)       empty
area  2 slot 0: (0, 3, 63, 128, 131, 38, 0)  plain door
area  2 slot 1: (0, 14, 71, 384, 133, 179, 0) plain door
area  2 slot 2: (0, 31, 63, 486, 131, 397, 0) plain door
area  2 slot 3: (-1, 0, 0, 0, 0, 0, 0)       empty
area  3 slot 0: (0, 4, 36, 115, 130, 51, 0)  plain door
area  3 slot 1: (1, 15, 76, 307, 118, 192, 0) room door/exit
area  3 slot 2: (0, 25, 63, 410, 131, 320, 0) plain door
area  3 slot 3: (-1, 0, 0, 0, 0, 0, 0)       empty
area  4 slot 0: (0, 4, 63, 205, 129, 51, 0)  plain door
area  4 slot 1: (2, 18, 68, 435, 122, 230, 0) elevator
area  4 slot 2: (6, 36, 54, 486, 123, 461, 0) end wall
area  4 slot 3: (-1, 0, 0, 0, 0, 0, 0)       empty
area  5 slot 0: (6, 0, 54, 51, 123, 0, 0)    end wall
area  5 slot 1: (2, 6, 68, 282, 122, 77, 0)  elevator
area  5 slot 2: (0, 24, 67, 397, 128, 307, 0) plain door
area  5 slot 3: (-1, 0, 0, 0, 0, 0, 0)       empty
area  6 slot 0: (0, 4, 71, 205, 126, 51, 0)  plain door
area  6 slot 1: (1, 20, 76, 371, 119, 256, 0) room door/exit
area  6 slot 2: (-1, 384, 0, 474, 0, 0, 0)   empty
area  6 slot 3: (-1, 0, 0, 0, 0, 0, 0)       empty
area  7 slot 0: (0, 4, 63, 141, 125, 51, 0)  plain door
area  7 slot 1: (1, 17, 76, 333, 120, 218, 0) room door/exit
area  7 slot 2: (0, 30, 36, 448, 130, 384, 0) plain door
area  7 slot 3: (-1, 0, 0, 0, 0, 0, 0)       empty
area  8 slot 0: (0, 5, 63, 128, 127, 64, 0)  plain door
area  8 slot 1: (1, 11, 76, 256, 121, 141, 0) room door/exit
area  8 slot 2: (0, 24, 36, 371, 130, 307, 0) plain door
area  8 slot 3: (-1, 0, 0, 0, 0, 0, 0)       empty
area  9 slot 0: (0, 5, 67, 154, 128, 64, 0)  plain door
area  9 slot 1: (2, 18, 68, 435, 122, 230, 0) elevator
area  9 slot 2: (6, 36, 54, 486, 123, 461, 0) end wall
area  9 slot 3: (-1, 0, 0, 0, 0, 0, 0)       empty
area 10 slot 0: (1, 5, 0, 13, 0, 0, 0)       room door/exit
area 10 slot 1: (12, 15, 60, 18, 203, 0, 0)  object 12
area 10 slot 2: (11, 26, 60, 28, 203, 0, 0)  object 11
area 10 slot 3: (-1, 0, 0, 0, 0, 0, 0)       empty
area 11 slot 0: (1, 5, 0, 13, 0, 0, 0)       room door/exit
area 11 slot 1: (-1, 0, 0, 0, 0, 0, 0)       empty
area 11 slot 2: (-1, 0, 0, 0, 0, 0, 0)       empty
area 11 slot 3: (-1, 0, 0, 0, 0, 0, 0)       empty
area 12 slot 0: (1, 5, 0, 13, 0, 0, 0)       room door/exit
area 12 slot 1: (13, 16, 60, 18, 203, 0, 0)  object 13
area 12 slot 2: (14, 24, 60, 26, 203, 0, 0)  object 14
area 12 slot 3: (-1, 0, 0, 0, 0, 0, 0)       empty
area 13 slot 0: (1, 5, 0, 13, 0, 0, 0)       room door/exit
area 13 slot 1: (19, 15, 60, 18, 203, 0, 0)  object 19
area 13 slot 2: (-1, 0, 0, 0, 0, 0, 0)       empty
area 13 slot 3: (20, 33, 60, 36, 203, 0, 0)  object 20
area 14 slot 0: (1, 5, 0, 13, 0, 0, 0)       room door/exit
area 14 slot 1: (18, 16, 60, 17, 203, 0, 0)  object 18
area 14 slot 2: (-1, 0, 0, 0, 0, 0, 0)       empty
area 14 slot 3: (17, 34, 60, 35, 203, 0, 0)  object 17
area 15 slot 0: (1, 5, 0, 13, 0, 0, 0)       room door/exit
area 15 slot 1: (16, 16, 60, 18, 203, 0, 0)  object 16
area 15 slot 2: (15, 24, 60, 25, 203, 0, 0)  object 15
area 15 slot 3: (-1, 0, 0, 0, 0, 0, 0)       empty

== BINA 5000 part 2: animation states [35] x 26 bytes (13 x int16)
   fields: nframes, curframe, cel[9], tick_counter, ticks_per_frame
state  0: n=0 cels=[] ticks=0/0
state  1: n=1 cels=[0] ticks=1/1
state  2: n=8 cels=[1, 2, 3, 4, 5, 6, 7, 8] ticks=1/1
state  3: n=5 cels=[9, 10, 11, 12, 13] ticks=1/1
state  4: n=5 cels=[14, 15, 16, 17, 18] ticks=1/1
state  5: n=4 cels=[19, 20, 21, 22] ticks=1/1
state  6: n=4 cels=[23, 24, 25, 26] ticks=1/1
state  7: n=3 cels=[47, 48, 49] ticks=1/1
state  8: n=8 cels=[34, 35, 36, 37, 38, 39, 35, 34] ticks=1/1
state  9: n=9 cels=[27, 28, 29, 28, 27, 30, 31, 32, 33] ticks=1/1
state 10: n=4 cels=[100, 101, 102, 103] ticks=2/2
state 11: n=4 cels=[104, 105, 106, 107] ticks=2/2
state 12: n=6 cels=[50, 51, 52, 53, 54, 55] ticks=1/1
state 13: n=3 cels=[10000, 10001, 10002] ticks=1/1
state 14: n=8 cels=[10003, 10004, 10005, 10006, 10007, 10006, 10005, 10004] ticks=4/4
state 15: n=5 cels=[9, 10, 11, 12, 203] ticks=1/1
state 16: n=0 cels=[] ticks=0/0
state 17: n=5 cels=[105, 113, 114, 113, 105] ticks=1/1
state 18: n=5 cels=[105, 180, 181, 182, 105] ticks=1/1
state 19: n=6 cels=[105, 186, 187, 188, 186, 105] ticks=1/1
state 20: n=6 cels=[105, 186, 191, 192, 186, 105] ticks=1/1
state 21: n=0 cels=[] ticks=0/0
state 22: n=2 cels=[115, 116] ticks=1/1
state 23: n=3 cels=[183, 184, 185] ticks=1/1
state 24: n=2 cels=[189, 190] ticks=1/1
state 25: n=2 cels=[193, 194] ticks=1/1
state 26: n=7 cels=[40, 41, 42, 43, 43, 43, 43] ticks=1/1
state 27: n=8 cels=[99, 99, 99, 99, 30, 31, 32, 33] ticks=1/1
state 28: n=9 cels=[44, 45, 46, 44, 45, 46, 44, 35, 35] ticks=1/1
state 29: n=5 cels=[14, 15, 16, 17, 18] ticks=1/1
state 30: n=4 cels=[10008, 10009, 10008, 10009] ticks=1/1
state 31: n=5 cels=[10010, 10011, 10012, 10013, 10014] ticks=1/1
state 32: n=5 cels=[108, 109, 110, 110, 111] ticks=1/1
state 33: n=5 cels=[111, 110, 110, 109, 108] ticks=1/1
state 34: n=8 cels=[100, 112, 100, 112, 100, 112, 100, 112] ticks=1/1

== BINA 5000 part 3: initial objects [6] x 18 bytes (9 x int16)
   fields: x(col), y, cel, f6, f8, f10, active, hit_offset, hit_width
obj 0 zap bolt : (0, 0, 13, 59, 0, 150, 0, 0, 13)
obj 1 LiveWire : (20, 113, 11, 32, 200, 0, 0, 1, 9)
obj 2 room prop: (19, 24, 4, 26, 400, 0, 0, 0, 4)
obj 3 player   : (20, 81, 6, 64, 0, 0, 1, 1, 4)
obj 4 Telly    : (0, 77, 8, 64, 200, 0, 0, 1, 6)
obj 5 trick    : (0, 0, 6, 27, 200, 150, 0, 0, 6)
```

## A.2 Other fixed tables (not in the data files)

| table | values |
|---|---|
| per-tier table (T0..T3), 13 fields | see chapter 04 §10 / chapter 05 §0.1 |
| Telly stop % / resume % per tier | (60,15) (50,12) (40,11) (30,9) |
| energy-machine candidate slots (area, slot) | (0,2) (1,2) (3,2) (4,0) (5,2) (6,0) (7,0) (8,2) (9,0) |
| column → pixel `COLPX[0..40]` | 0 13 25 38 51 64 77 89 102 115 128 141 153 166 179 192 205 217 230 243 256 269 281 294 307 320 333 345 358 371 384 397 409 422 435 448 461 473 486 499 512 |
| room index → area | NEWS 10, GAME SHOW 12, CARTOON 13, EQUIPMENT 14, SOUND 15 |
| trick release frame (types 22..25) | 2, 0, 3, 3 |
| trick hit splat cel (types 22..25) | 210, 212, 209, 211 |
| trick (y, hit width) (types 22..25) | (81,5) (91,2) (115,4) (118,3) |
| trick loop sound, SND WAVE id (types 22..25) | 1700, 1727, 1706, 1750 |
| zap bolt left shift per frame (facing left) | 5, 3, 0, 0, 0, 0 |
| rank thresholds | 0, 5000, 30000, 60000, 120000, 200000, 300000, 500000 |
| winning-bonus count-up chunk per tier | 50, 100, 150, 250 |
| clues revealed per challenge per tier | 4, 4, 2, 2 |
| drill questions per Telly per tier | 4, 6, 8, 10 (need N−1 correct) |
| default customisation per tier (13 bytes) | see chapter 05 §2.1 |
| decoder patterns (10) and permutations (24) | see chapter 05 §3.1 |
| map city table (18 cities) | see chapter 06 §8.0 |
| delay(n) → centiseconds | `floor(floor(n*4/13)*100/73)`; used values: 10→4, 23→9, 120→49, 200→83, 240→100, 480→201, 720→302 |

## B. Dialog resources — structure

Every DIALOG resource in `SSOWINCD.EXE`: id, size in DLU, font, and its buttons (`id:label[kind]`;
DEFAULT = default push button, disabled = initially disabled). The number of static text controls is
given in brackets. The texts themselves are read from the resource at run time.

```
    1 140x50 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
  129 180x60 font=MS Sans Serif buttons: 1:OK[DEFAULT]  (+1 static)
  132 140x50 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
  134 140x75 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
  140 140x50 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
  141 140x50 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
  144 140x50 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
  145 140x50 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
  146 140x50 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
  147 140x50 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
  148 140x50 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
  200 160x120 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+4 static)
  220 200x160 font=TLC Keycaps buttons: 121:5[radio], 122:10[radio], 123:15[radio], 124:20[radio], 101:Addition[checkbox], 103:Subtraction[checkbox], 102:Multiplication[checkbox], 104:Division[checkbox], 105:0[checkbox], 106:1[checkbox], 107:2[checkbox], 108:3[checkbox], 109:4[checkbox], 110:5[checkbox], 111:6[checkbox], 112:7[checkbox], 113:8[checkbox], 114:9[checkbox], 115:10[checkbox], 116:11[checkbox], 117:12[checkbox], 118:3 + 5 = ?[checkbox], 119:? + 5 = 8[checkbox], 120:3 + ? = 8[checkbox], 1:&OK[DEFAULT], 2:&Cancel[push], 3:&Reset[push]  (+8 static)
  221 200x160 font=TLC Keycaps buttons: 121:1[radio], 122:2[radio], 123:3[radio], 124:4 (Selects all)[radio], 101:Addition[checkbox], 103:Subtraction[checkbox], 102:Multiplication[checkbox], 104:Division[checkbox], 105:0[checkbox], 106:1[checkbox], 107:2[checkbox], 108:3[checkbox], 109:4[checkbox], 110:5[checkbox], 111:6[checkbox], 112:7[checkbox], 113:8[checkbox], 114:9[checkbox], 115:10[checkbox], 116:11[checkbox], 117:12[checkbox], 118:3 + 5 = ?[checkbox], 119:? + 5 = 8[checkbox], 120:3 + ? = 8[checkbox], 1:&OK[DEFAULT], 2:&Cancel[push], 3:&Reset[push]  (+8 static)
 1000 170x75 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
 1001 170x75 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
 1002 170x80 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
 1003 170x80 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
 1004 170x50 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
 1005 170x50 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
 1006 170x105 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
 1007 170x80 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
 1008 170x85 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
 1009 170x60 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
 1010 170x70 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
 1011 170x90 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
 1012 170x60 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
 1014 170x100 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
 1016 170x80 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
 1017 140x50 font=TLC Keycaps buttons: 1:&Yes[DEFAULT], 2:&No[push]  (+1 static)
 1018 140x50 font=TLC Keycaps buttons: 1:&Yes[DEFAULT], 2:&No[push]  (+1 static)
 1019 140x50 font=TLC Keycaps buttons: 1:&Yes[DEFAULT], 2:&No[push]  (+1 static)
 1020 140x50 font=TLC Keycaps buttons: 1:&Yes[DEFAULT], 2:&No[push]  (+1 static)
 1021 140x50 font=TLC Keycaps buttons: 1:&Yes[DEFAULT], 2:&No[push]  (+1 static)
 2000 205x155 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+2 static)
 3010 180x45 font=MS Sans Serif buttons: 1:&OK[DEFAULT]  (+1 static)
 3020 180x60 font=MS Sans Serif buttons: 1:&OK[DEFAULT]  (+1 static)
 3030 180x75 font=MS Sans Serif buttons: 1:&OK[DEFAULT]  (+1 static)
 3035 180x65 font=MS Sans Serif buttons: 1002:E&xit[DEFAULT]  (+1 static)
 3036 180x80 font=MS Sans Serif buttons: 1002:E&xit[DEFAULT]  (+1 static)
 3040 200x70 font=MS Sans Serif buttons: 1002:&Exit[DEFAULT]  (+1 static)
 3041 200x70 font=MS Sans Serif buttons: 1002:&Exit[DEFAULT]  (+1 static)
 3050 200x90 font=MS Sans Serif buttons: 1601:(long label)[checkbox], 1001:&Continue[push], 1002:E&xit[DEFAULT]  (+1 static)
 3080 175x50 font=MS Sans Serif buttons: 1:OK[DEFAULT]  (+1 static)
 9130 170x75 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
 9131 170x65 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
 9133 170x65 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
 9136 140x65 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
 9142 140x40 font=TLC Keycaps buttons: 1:&Yes[DEFAULT], 2:&No[push]  (+1 static)
 9149 140x60 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
 9151 140x40 font=TLC Keycaps buttons: 1:&Yes[DEFAULT], 2:&No[push]  (+1 static)
11000 140x40 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
12000 220x135 font=TLC Keycaps buttons: 101:See &Next Page[DEFAULT], 102:&Done[push], 100:Go &Back a Page[disabled push]  (+1 static)
12001 220x135 font=TLC Keycaps buttons: 101:See &Next Page[DEFAULT], 102:&Done[push], 100:Go &Back a Page[push]  (+1 static)
12002 220x135 font=TLC Keycaps buttons: 101:See &Next Page[DEFAULT], 102:&Done[push], 100:Go &Back a Page[push]  (+1 static)
12003 220x135 font=TLC Keycaps buttons: 101:See &Next Page[DEFAULT], 102:&Done[push], 100:Go &Back a Page[push]  (+1 static)
12004 220x135 font=TLC Keycaps buttons: 101:See &Next Page[disabled push], 102:&Done[DEFAULT], 100:Go &Back a Page[push]  (+1 static)
12100 220x135 font=TLC Keycaps buttons: 100:Go &Back a Page[disabled push], 102:&Done[DEFAULT], 101:See &Next Page[disabled push]  (+2 static)
12200 220x135 font=TLC Keycaps buttons: 100:Go &Back a Page[disabled push], 102:&Done[DEFAULT], 101:See &Next Page[disabled push]  (+1 static)
12300 220x135 font=TLC Keycaps buttons: 100:Go &Back a Page[disabled push], 102:&Done[DEFAULT], 101:See &Next Page[disabled push]  (+2 static)
12400 220x135 font=TLC Keycaps buttons: 101:See &Next Page[DEFAULT], 102:&Done[push], 100:Go &Back a Page[disabled push]  (+2 static)
12401 220x135 font=TLC Keycaps buttons: 101:See &Next Page[disabled push], 102:&Done[DEFAULT], 100:Go &Back a Page[push]  (+1 static)
12500 220x135 font=TLC Keycaps buttons: 100:Go &Back a Page[disabled push], 102:&Done[DEFAULT], 101:See &Next Page[disabled push]  (+4 static)
12600 220x85 font=TLC Keycaps buttons: 100:Go &Back a Page[disabled DEFAULT], 102:&Done[DEFAULT], 101:See &Next Page[disabled DEFAULT]  (+1 static)
15000 140x80 font=TLC Keycaps buttons: 2:&No[DEFAULT], 1:&Yes[push]  (+1 static)
20100 140x50 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
20101 140x65 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
20102 140x60 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
20104 140x55 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
20105 140x50 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
20106 140x50 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
20107 140x45 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
20108 140x60 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
22000 140x50 font=TLC Keycaps buttons: 1:&Yes[DEFAULT], 2:&No[push]  (+1 static)
22001 140x50 font=TLC Keycaps buttons: 1:&Yes[DEFAULT], 2:&No[push]  (+1 static)
22002 140x50 font=TLC Keycaps buttons: 1:&Yes[DEFAULT], 2:&No[push]  (+1 static)
22003 140x50 font=TLC Keycaps buttons: 1:&Yes[DEFAULT], 2:&No[push]  (+1 static)
22004 140x50 font=TLC Keycaps buttons: 1:&Yes[DEFAULT], 2:&No[push]  (+1 static)
22005 140x50 font=TLC Keycaps buttons: 1:&Yes[DEFAULT], 2:&No[push]  (+1 static)
22010 140x75 font=TLC Keycaps buttons: 1:&Yes[DEFAULT], 2:&No[push]  (+1 static)
22011 140x75 font=TLC Keycaps buttons: 1:&Yes[DEFAULT], 2:&No[push]  (+1 static)
22012 140x75 font=TLC Keycaps buttons: 1:&Yes[DEFAULT], 2:&No[push]  (+1 static)
22013 140x75 font=TLC Keycaps buttons: 1:&Yes[DEFAULT], 2:&No[push]  (+1 static)
22014 140x80 font=TLC Keycaps buttons: 1:&Yes[DEFAULT], 2:&No[push]  (+1 static)
22015 140x80 font=TLC Keycaps buttons: 1:&Yes[DEFAULT], 2:&No[push]  (+1 static)
22016 140x90 font=TLC Keycaps buttons: 1:&Yes[DEFAULT], 2:&No[push]  (+1 static)
23006 140x80 font=TLC Keycaps buttons: 1:&Yes[DEFAULT], 2:&No[push]  (+1 static)
23007 140x60 font=TLC Keycaps buttons: 1:&Yes[DEFAULT], 2:&No[push]  (+1 static)
23008 140x80 font=TLC Keycaps buttons: 1:&Yes[DEFAULT], 2:&No[push]  (+1 static)
23012 170x75 font=TLC Keycaps buttons: 2:&Continue[DEFAULT]  (+1 static)
23013 140x60 font=TLC Keycaps buttons: 1:&Yes[DEFAULT], 2:&No[push]  (+1 static)
24000 140x80 font=TLC Keycaps buttons: 1:&Yes[DEFAULT], 2:&No[push]  (+1 static)
25000 140x50 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
25001 140x50 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
25002 140x50 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
25003 140x95 font=TLC Keycaps buttons: 1:&Continue[DEFAULT]  (+1 static)
```

## C. Calculator key rectangles

Key faces in the calculator art (SSO2 ASEQ 8531, drawn at (299,235)), relative to its top-left.
The original hit-tests slightly larger rectangles (about 2 px of margin around each face).

| key | x | y | | key | x | y |
|---|---|---|---|---|---|---|
| 7 | 109–120 | 14–26 | | 1 | 109–120 | 58–70 |
| 8 | 130–141 | 14–26 | | 2 | 130–141 | 58–70 |
| 9 | 151–162 | 14–26 | | 3 | 151–162 | 58–70 |
| ÷ | 172–183 | 14–26 | | − | 172–183 | 58–70 |
| 4 | 109–120 | 36–48 | | 0 | 109–120 | 80–92 |
| 5 | 130–141 | 36–48 | | = | 130–162 | 80–92 |
| 6 | 151–162 | 36–48 | | + | 172–183 | 80–92 |
| × | 172–183 | 36–48 | | C (clear) | 21–32 | 58–70 |
| | | | | ENTER | 21–69 | 79–91 |

The display window is the light-blue area at about x 17–95, y 15–40.
