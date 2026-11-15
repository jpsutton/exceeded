# 11 — Open questions and fidelity decisions

## A. Decisions for the implementer (original defects; recommended choice in bold)

| # | original behaviour | chapter | recommendation |
|---|---|---|---|
| D1 | CD/resource failure shows non-existent dialog 128 | 03 §1.1 | **show 129** |
| D2 | Full sign-in list shows the hints dialog 22012 | 03 §2.3 | **show 23012** |
| D3 | All-Star rank dialog 149 does not exist | 03 §2.5 | **use DIALOG 1** (same text, speech 1803) |
| D4 | Expert first-time dialog 9133 is unreachable (stage 3 never set) | 03 §2.5 | **show 9133/speech 1832 the first time a player starts a game as Expert** (stage 2 → 3); optional |
| D5 | Zapping with the last energy unit never hits | 04 §7 | **keep** (switchable) |
| D6 | Walking into screen column 1/33 trips the player | 04 §5.2 | **keep** |
| D7 | Expert puzzle: hideout order equals code order 1 game in 24 | 05 §3.4 | **re-shuffle until different** |
| D8 | All-Star puzzle: ABCD pattern leaves stale pictures → rare ambiguity; hideout often shares pictures with the code | 05 §3.4 | **re-roll until exactly one room matches the rule and the hideout shares no picture** |
| D9 | Picture pool/pattern table persist across games in a run | 05 §3.1 | keep (harmless) or reset per game |
| D10 | DfS elapsed-time display runs ~35 % fast | 05 §2.6 | **fix (real seconds)** |
| D11 | Re-roll after customisation change tests "solved" by operation index | 06 §10 | **skip objects with state 2/3** |
| D12 | After a wrong Decide, an earlier save may stay resumable | 05 open q. 4 | **clear the saved-game flag on any loss** |
| D13 | Game seed reproduces only the math problems (re-seeded from time) | 05 §1.3 | keep; optionally seed everything from the game seed for replays |
| D14 | `hiscore.dat` written but never displayed | 03 §4.1 | **drop** |
| D15 | Single instance / refuse Windows shutdown / NumLock forcing / WinG/16-colour/WaveMix modes | 03, 02 | **drop / replace** |
| D16 | A mirrored *and* horizontally clipped cel is not drawn | 02 §5.3 | **draw it correctly** (only affects sprites at screen edges) |

## B. Unresolved questions (verify against the original if possible)

1. Exact SFX meaning for several SND WAVE ids (03 §5.3) — confirm by ear.
2. Tier-table field 11 (20/30/40/60) has no known use.
3. Hall hot-spot `field3` widths (e.g. 294) make door zones very wide; believed deliberate.
4. Mouse over the player but below the head line: probably no command.
5. Math-challenge display fine pixel offsets and the spinner font (06 §8, §10).
6. Hard line-graph templates (MTHT 85–88) verified statically only.
7. ASEQ header words `w3`/`w4` and cel `type` field (4 vs 1) have no known effect in 8-bit mode.
8. ASEQ 205 and 1500–1503 (orange 16-colour-style art) — probably only for 16-colour mode; unused by a 256-colour port.
9. Hint 1016 trigger (first Problem Customization during a game) is inferred.
10. Resume position after loading a saved game (assumed: new-game hall start position).
11. Drill-entry key codes beyond digits/Enter/Backspace/Esc (05 §2.4): treat Delete as Backspace and `c`/`C` as clear.
12. Items listed in `07-screens.md` "Open questions".
13. (Resolved in v1.5, see 02 §4.1) normal = NFNT 28625, large = NFNT 28568, medium = NFNT 28558.
    The "Enter" key-cap glyph used in drill/score prompts is NFNT 15001's Enter key cap (0x80–0x9A range).
14. Math-challenge Esc: chapter 06 lists Esc both as "leave the challenge" (§6) and "clear entry" (§7).
    Recommended: Esc leaves (challenge stays available, tries kept); `c`/`C` clears.
15. Calculator keypad click rectangles and the placement of SSO2 ASEQ 8540 (stand-alone keypad) were
    not extracted; derive key rectangles from the calculator art (ASEQ 8531, 200×104 at (299,235)).
16. Mapping of calculator error messages to dialogs 20100–20107 (by text match; 20108 is the
    "settings used next game" notice).
17. Decoder room rows: click rectangles start at y 43 (+47 per row, chapter 05) while the highlight
    recolouring starts at y 45 (chapter 07); both may be right (click area vs drawn row).
18. Telly's lit-belly overlay: which cel and offset (likely cel 112 or the 180-series, drawn over
    Telly) was not pinned down.
19. Footstep and Telly-walk sound ids (hall floor 1/floor 2/rooms) are in the INTS 1700 list but the
    per-event mapping was not tabulated; INTS 1701's purpose is unknown.
20. Drill for Skill end-of-set animations: art (SSO2 ASEQ 8604 cels) and sounds not tabulated.
21. The trigger for the " to Explore TV Station" prompt (drill screen) was not traced.
22. Hall scrolling: the maximum scroll is given as `endArea*38 − 1` while an elevator arrival sets
    `area*38` for the right-end areas; one of the two is off by one. Clamp to the larger value.
23. Text y convention: chapters 06/07 coordinates are the top of the text line (chapter 07 Part A §0);
    if text appears shifted when compared with the original, this is the first thing to check.

## C. What was verified and how

* All 93 ASEQ resources, 10 NFNT fonts, cursors and the palette were decoded and visually checked
  against expectations (rooms, sprites, title screens render correctly with RRGB 128).
* All 80 MTHT templates were parsed and instantiated for seeds 1–10 × questions 0–3; questions,
  hints and answers are mutually consistent (e.g. chapter 06 §11).
* The RNG formulas were checked numerically against the original arithmetic.
* The save-file layout matches the shipped blank file exactly (1508 + 99 × 168 = 18,140 bytes).
* Behaviour was derived statically; the original was **not** run (no Windows 3.1 environment
  was available). Timings and layouts should be compared with a running original (e.g. in an emulator
  running Windows 3.1) if one becomes available.
