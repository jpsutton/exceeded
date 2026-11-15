# 03 — Application shell, UI flow, persistence, audio, input

This spec describes how the program behaves, not how the original code is written. 
Confidence: **H** = read directly from code, **M** = strong inference, **L** = guess.

---------------------------------------------------------------------------------------------------
## 1. Startup sequence

### 1.1 Order of operations (H)
1. **Single instance.** If a window of class `TLCSSO` already exists, post it a "restore/activate"
   message (user message 0x400) and exit.
2. Record the desktop rectangle. Create the main window: class `TLCSSO`, title **"OutNumbered! CD"**,
   style caption + system menu + minimize box, sized to the whole desktop. The game draws a fixed
   **512×384 logical area** centred in the client area (offsets computed as (client−512)/2, (client−384)/2).
   All mouse coordinates are converted to this area's coordinates.
3. Install menu set **A** (File 500 + Options 600) (see §3).
4. `AddFontResource("sso.fon")` (the TLC Keycaps font that dialogs use) and broadcast WM_FONTCHANGE.
   At exit: RemoveFontResource and broadcast again.
5. **System / INI checks** (INI file = `<current dir>\ssowincd.ini`, section `[OutNumbered!]`).
   Each failed check shows a "The Learning Company" dialog and ends the program:
   | check | failure dialog |
   |---|---|
   | free global memory < 2,200,000 bytes | 3080 "not enough memory" |
   | (free memory > 5,000,000 sets a "large memory" flag, probably used for caching) | – |
   | Windows major version ≠ 3 and minor ≠ 10 | 3010 "You need Windows 3.1" |
   | not in 386 Enhanced mode (WF_ENHANCED) | 3020 |
   | display is 16-colour (no 256-colour palette device) | 3035 "designed for 640×480 256-color" |
   | no wave-out device | 3040 "WAV driver could not be found" |
   | no usable MIDI device (no midiOut devices, or device caps query fails, or `\system\midimap.cfg` cannot be opened) | 3041 "MIDI driver…" |
   The program also records whether the CPU is a 386 (WF_CPU386), probably for a slow-machine mode
   (M; the flag's consumer was not traced).
   Dialogs 3030, 3036 and 3050 (the 3050 one has a "don't show again" checkbox, id 1601) are never shown.
6. **INI keys** (all live under `[OutNumbered!]`):
   | key | type/default | effect |
   |---|---|---|
   | `Graphics` | int, 3 | rendering back-end. 3 = WinG (drops to 1 if WING.DLL fails to load); 2 = falls back to 1 if bpp > 8; a 16-colour display forces mode 4. Details belong to chapter 02. |
   | `wavemix` (written `WaveMix=` in the shipped INI; Windows INI keys are case-insensitive) | int, 1 | non-zero → mix sound effects through WAVEMIX.DLL. If WaveMix later reports an error, the program converts every loaded SFX to plain waveOut and **writes `wavemix=0`** to the INI. |
   | `CDDrive` | string, "" | path prefix put directly in front of the resource-DLL names and the MIDI folder (e.g. `D:\SSOWINCD\`). An empty value means the current directory. |
   | `MIDIPath` | string, "" | MIDI folder. A trailing `\` is added if missing; max 65 chars. Empty → `CDDrive + "midi\"`. |
   | `MIDIType` | int, −1 | −1 = autodetect. 0 = use **B**ase-level files (`b*.mid`), non-zero = **E**xtended (`e*.mid`). |
   | `CheatKeys` | int, 0 | read and stored, but **nothing reads the stored value** in this build. No effect. |
   | `NoBlanket` | int, 0 | 0 → paint the window area outside the 512×384 game rectangle with the light-grey stock brush ("blanket"). Non-zero → leave it unpainted. |
7. **Resource DLLs.** First, any module named `SSORSRC` still loaded from an earlier crash is freed.
   Then each file in this table is loaded with LoadLibrary(CDDrive + name):
   | file | load rule |
   |---|---|
   | sso1.dat | only if the display is 256-colour (type 2) |
   | sso2.dat, sso3.dat, snd.dat, speech.dat | always |
   | (winsso.exe) | the EXE itself; its own resources (dialogs, menus, NFNT, cursors) |
   If any LoadLibrary fails, the code shows dialog **128**, then exits. No dialog 128 exists. The
   intended dialog is clearly **129** ("The OutNumbered! CD could not be found…"). A reimplementation
   should show 129.
8. Initialise the rest: timer (100 ticks/s, see §6.4); graphics 512×384; the animated-object system;
   the sound manager (§5); the 16-entry input queue; hot regions; palette (RRGB 128); fonts.
9. Run the main program (§2).

### 1.2 Window-message behaviour (H)
- **Deactivate** (WM_ACTIVATEAPP with "deactivating"): pause all wave channels and MIDI, and pause
  the game clock. **Activate** (WM_ACTIVATE, not minimised): resume them.
- Opening any menu (WM_INITMENU) also pauses. When the menu closes (WM_MENUSELECT with "closed"), the program resumes.
- The system menu gets a separator and an **"About OutNumbered!"** item (id 0x80), which shows dialog 2000.
  Moving the window (SC_MOVE) is disabled unless the window is minimised.
- **Close** (SC_CLOSE / WM_CLOSE) runs the Exit confirmation (§3.3).
- WM_QUERYENDSESSION returns 0, so the program refuses to let Windows shut down while it is running.
- **Dialog buttons and results (H).** Enter activates the default push button (style low nibble 1).
  Esc always sends "cancel" (id 2), even when the dialog has no control with id 2. Results: id 1
  (OK/Yes/Continue) closes with result 1; id 2 (No/Cancel, and the Continue of 23012) closes with 0;
  ids 200+n close with n. Customization dialogs validate on OK first (05 §2.1). Help dialogs page with
  100/101/102 as described in §3.2. Appendix B lists every dialog's buttons.
- **Dialog units (corrected in v1.28).** The TLC Keycaps dialogs use **1 DLU = 2.0 px horizontally** and
  2.125 px vertically. The horizontal base unit is the Windows 95 rule: the average width of the 52
  letters A–Z a–z in the dialog font, rounded ((405 + 26) div 52 = 8 px, so 8/4 = 2.0 px per DLU), not the
  font's tmAveCharWidth (7). With 1.75 px several statics are too narrow and their text wraps onto a
  clipped extra line (e.g. New Player Hint 1004 "…math challenge."); with 2.0 px they fit as designed.
  The vertical unit is the font height 17 / 8. The MS Sans Serif 8 pt system dialogs
  (129, 30xx) use about 1.5 × 1.625 px.
- **Dialog placement.** Every modal dialog is centred on the 512×384 game area. Some dialogs are
  placed at an explicit game-area position instead (the calculator error dialogs at (16,236)).
  A dialog can carry a SPEECH wave that starts when the dialog opens and stops when it closes.
  Opening a dialog first stops all sound-effect and speech channels and pauses MIDI; closing the dialog resumes MIDI.

---------------------------------------------------------------------------------------------------
## 2. Program flow

```
start ──► INTRO / TITLE (attract loop) ──► [restart point 0] SIGN-IN ─┬─► GAME (main) ──► end of game ─┐
              ▲                                   ▲                   └─► DRILL FOR SKILL ── Esc ──┐   │
              │                                   └─────── Return to Sign In / drill exit ◄────────┘   │
              │                                   [restart point 1] "new game, same player" ◄──────────┘
```
Before the intro, the program preloads the hall/room music (level1, level2, room1, room2) and the
SFX list INTS 1700.

### 2.1 Intro / title (H for control flow, M for content)
- Main-game flags are reset first (no player is signed in).
- Load `intro.mid` (TEXT 1724) and draw the title screen (SSO1 ASEQ 1700 background, M). The whole
  screen is one hot region: a click counts as a key press. Play intro.mid **looped**.
- Wait up to **30 s** for a key:
  - **Esc** → quit at once, with no confirmation.
  - **Space** → skip: go straight to sign-in.
  - any other key → play the intro animation once, then go to sign-in.
  - timeout (no key) → play the intro animation, redraw the title, and repeat (attract loop).
- The intro animation uses SSO3 WAVE 11000/11001 and speech, driven by sound cues inside the
  animation script (user opcodes; chapter 07 Part B §1.3). Enter or Space during the
  animation sets "skip". Esc quits.
- `open%03d.bob` is **not** part of the intro. It belongs to an unreachable developer cel-viewer that
  dumps frames to files. Ignore it.

### 2.2 Main loop (H)
```
loop forever:
    restart point 0:
    result = SignIn()              # §2.3; Esc quits immediately
    restart point 1:               # "play again" lands here
    mode = PrepareSession()        # §2.4; 1 = game, 2 = drill
    if mode == game:  StartGame(); RunGameLoop()     # exits only by jumping to a restart point
    if mode == drill: RunDrill()                     # returns when Esc is pressed
    remove the menu bar
```
"Return to Sign In" jumps to restart point 0. Loss, win or wrong guess jump to restart point 1, which
immediately starts a **new game for the same player**. The original release frees any resources
allocated after the restart point it jumps to.

### 2.3 Sign-in screen
Art: SSO3 ASEQ 16000 at (0,0) (a monitor showing a "Sign In" window with a list box and up/down
scroll arrows). Buttons (73×48, DRILL 85×48) are drawn at START GAME (164,331), NEW PLAYER (241,331),
CANCEL (318,331), DRILL FOR SKILL (395,330) (H). The list box rectangle is (87,107) 312×132 with 6 rows of
22 px (H). Scroll arrows 16005–16008 (31×24) sit in the scroll bar at about (400,108) and (400,218), the
close box 16004 (30×30) at about (88,76) (M, from the art). The list box is at x = 87..398, first row at y ≈ 108; rows are 22 px high, **6 visible
rows**. Four buttons, each an ASEQ with 3 cels (0 normal, 1 pressed, 2 disabled):

| button | ASEQ | event | enabled when |
|---|---|---|---|
| START GAME | 16001 | start | a real player is selected, or a new name has been typed into the provisional entry |
| NEW PLAYER | 16002 | new | the current selection is not a provisional (new) entry |
| CANCEL | 16003 | cancel | the list is not empty and a provisional entry exists (NEW PLAYER is disabled) |
| DRILL FOR SKILL | 16009 | drill | always |

Clicking a button plays SFX 1756 (click), shows the pressed cel and posts the button's event.
Keyboard events (from the input queue) go into the same event queue. Some modifier-key variants of the button events are accepted but nothing generates them;
ignore them.

Each list row shows the **name** (left, typed text in another colour), the **rank title** at
x + 130 and the **lifetime score** right-aligned at x + 290, formatted with thousands commas.
The list is kept sorted by name (plain byte comparison, case-sensitive ASCII).

Behaviour (H unless noted):
- **List empty on entry** → behaves as if NEW PLAYER had been pressed.
- **Typing.** Accepted characters are 0x20–0x7F, at most 8. The first character must be a letter
  (a–z is upper-cased). A character is rejected if the rendered name would reach 100 px.
  As the user types, the list jumps to the first name that matches the typed prefix. If no name
  matches exactly, a **provisional "new player" entry** is inserted at the sorted position
  (header score field = −1, or −2 for a blank entry created by NEW PLAYER).
- **Backspace** deletes the last character.
- **Up/Down arrow** move the selection one row. Holding the key auto-repeats after a short delay.
- **Mouse click on a row** selects it. Clicking the already-selected row = START GAME.
  The scroll arrows scroll the list.
- **NEW PLAYER.** If the list already holds 99 players, nothing happens. Otherwise a blank
  provisional entry is created with a text cursor (internally char 0x1F) and the user types the name.
  When the list is full the code shows dialog **22012**, which is the "New Player Hints on/off"
  question. That is a bug: the intended dialog is **23012** "Sign-In List is Full", which is never
  referenced. A reimplementation should show 23012.
- **CANCEL** clears the typed name and removes the provisional entry.
- **Remove player = Ctrl+R** (the input queue receives 0x12). The selected entry must be a real
  player. Dialog **15000** "Remove Player" (Yes/No). On Yes, the entry is removed from the header
  list and the header is rewritten. The 168-byte record stays on disk; its slot index is recycled
  (moved past the end of the active list). (An older DOS-era wording of this confirmation exists but is unused.)
- **START GAME / Enter**:
  - Trailing spaces are trimmed from a new name.
  - If the new name equals an existing name → dialog **11000** "Duplicate Name", stay on the screen.
  - New player → create the record with defaults (§4.3): Calculator on, New Player Hints on,
    Clock on, SFX/Music/Voice on. Write the record and header.
  - Existing player → load the record from its slot.
  - The player is now "signed in".
- **DRILL FOR SKILL** → Drill mode **without a player**: nobody is signed in and nothing is saved.
  The drill customisation is reset to the defaults for level 0.
- **Esc** → quit the program at once (no confirmation).
- On leaving: New Player Hints = (rank == Trainee), drill level = rank ÷ 2 (0..3).
- If the data file cannot be opened/created, a reimplementation should show dialog 9149 ("The player file could not be opened…"); the original's handling of this case was not located.

### 2.4 Session preparation (runs after sign-in, and again on every "play again") (H)
- Drill level = rank ÷ 2. Load the default customisation for that level.
- Clear the New Player Hint flags (18 bytes, §4.4).
- **Game mode:**
  - If the record's "game customised" flag (bit 1) is set → use the record's game customisation,
    and its level bits.
  - Else, if the stored customisation differs from the defaults, it still replaces the defaults (M).
  - If the record's **saved-game flag** = 1 → this game will **continue the saved game
    automatically**. The Windows version never asks; the DOS-era "Do you want to continue that saved game?" string is unused.
  - Write the record.
- **Drill mode:** use the record's drill customisation.

### 2.5 Starting a game (H)
1. Set the "game" menu set (§3).
2. Show the **rank dialog with speech** for the player's rank (table below).
3. Initialise the game state:
   - **Continue:** restore the 99-byte saved state, restore Sound / Music / Voice / Calculator /
     Hints / Clock from the record, clear the record's saved flag, and re-seed the RNG with the saved seed.
   - **New game:** new random seed; clock 9:00 (stored 900, §4.4); score 0; energy byte = 12;
     clock option forced on; generate the scenario.
4. Set the game menus' check marks.
5. Draw the hall. The clock starts running.

Rank dialog shown at the start of every game:

| rank | dialog / speech (normal) | first-time variant |
|---|---|---|
| Trainee | 132 / SPEECH 1809 | the first time ever (record intro-stage 0): **9130** / 1830, then stage = 1 |
| Junior | 144 / 1805 | when stage = 1: **9131** / 1831, then stage = 2 |
| Senior | 145 / 1808 | |
| Pro | 146 / 1807 | |
| Expert | 147 / 1804 | **9133** / 1832 requires stage = 3, which is never set. **Bug: 9133 is never shown** |
| Master | 148 / 1806 | |
| All-Star | **149** / 1803. No dialog 149 exists. DIALOG **1** has the All-Star text and is probably the intended one (M). | |
| Champion | 134 / 1833 | |

The dialog texts give fixed totals ("You need a total of N points"). A routine computing
"points still needed" exists but is unused.

### 2.6 During the game
- **Esc** → Exit confirmation (§3.3). **Tab** → New Player Hint 1014 (if not yet shown), then the decoder.
- The game clock pauses while dialogs, menus, the decoder and math challenges are open (other specs cover this).
- **Out of time** (clock reaches 12:00 while not in the decoder):
  1. Clear the game score and energy.
  2. Midnight sequence: play `midnite.mid` (TEXT 1735) once, flash the clock 10 times, wait for the music to end.
  3. Clear the saved-game flag and write the record.
  4. Dialog **9136** "Out of Time".
  5. **New game, same player** (restart point 1).
- **Decide** (decoder): confirmation dialog 1017 + room index (1017..1021), with SPEECH placement
  (§5.6). If the decision rules are not met → dialog 25003. Match Rule → dialog 25000 + matchRule (0, 1 or 2), with SPEECH 1800 + matchRule.
  - At the chosen door the player shouts one of 8 lines (§5.6).
  - **Correct:** the Master of Mischief (MoM) caught. One of 8 MoM lines; victory music
    (`victory.mid`, TEXT 1757); bonus screen ("B O N U S", Energy Left / Time Left / Winning Game);
    the game score is added to the lifetime score (capped at 9,999,999) and the session high-score
    table is updated; rank-up handling; points/rank table screen ("Points / Rank / Game / Total").
  - **Wrong:** SFX 1760; one of 8 MoM gloating lines; the clock runs to midnight; score and energy are
    drained; midnight sequence. The game is lost.
  - In both cases: a "press Enter to continue" prompt (ASEQ 1012) waits for **Enter** (→ new game,
    same player) or **Esc** (→ Exit confirmation).
  - At the score screen: the saved-game flag is cleared, the record and hiscore are written, and the
    program is marked "no player signed in" until the next game starts. So Exit / Return to Sign In
    from the score screen do not mention saving.
  - If the player was a Trainee at game start and is now a Junior, New Player Hints are switched off.

### 2.7 Drill for Skill (summary; chapter 05 owns the details)
Set the drill menu set. Run problem sets back to back. The end of each set plays `victory.mid`.
**Esc** while a drill question is waiting for input pauses and opens the Exit confirmation
(§3.3, "no player" variant 23013 if nobody is signed in). The session otherwise ends only through
the menu (Return to Sign In / Exit). Dialog 9142 ("stop playing Drill for Skill?") is never used.

### 2.8 Unused / dead UI
- Some strings embedded in the executable (the "Play New Game / Start New Player / Exit Program" confirm texts,
  "Club Members" messages, "continue saved game?") belong to DOS-era message boxes. Their functions
  are unreachable, and the routine that would display them is an empty stub.
- Unreferenced dialogs: 24000, 23012 (see above), 9142, 9149, 9151, 22014, 22015, 3030, 3036, 3050,
  1013/1015 (do not exist).
- Menus 700 (Help for set A), 800 (Debug) and 5000–5700 (Beta) are never installed.

---------------------------------------------------------------------------------------------------
## 3. Windows menus

### 3.0 Menu bar presentation (v1.30)
The original is an ordinary Windows program: its main window has a **visible menu bar** directly above
the 512×384 game area, at all times except while set "none" is active. Players are told to use it (e.g.
New Player Hint 9130 "go to the Options menu"), so it must be visible and usable with the mouse, not only
through a hidden key. Reproduce it in the style of a Windows 95 menu bar:
* **Layout.** The engine's logical frame becomes 512 × (20 + 384): a 20-px menu bar strip on top, the
  game canvas below it at y offset 20 (all game coordinates in the other chapters stay canvas-relative).
  Windowed and full-screen scaling apply to the whole 512×404 frame. With set "none" the strip is drawn
  empty (same height, so the canvas never moves).
* **Look.** Strip background RGB (192,192,192), menu titles in an MS Sans Serif 8 pt substitute (the
  same face the engine uses for the system dialogs, 03 §1.2), black, with the `&` letter underlined;
  titles start at x 6 and are separated by 12 px of padding on each side of the text. An open title is
  drawn sunken (or highlighted navy (0,0,128) with white text). Drop-down: background (192,192,192),
  1-px 3-D border (white top/left, (128,128,128)+black bottom/right), items 18 px high, text inset 18 px
  (room for the check mark), shortcut letters underlined, separators as an etched line, check marks
  for checked items (the options toggles of §3.2), disabled items grey (128,128,128) with a white
  emboss; the item under the pointer / selected item is navy (0,0,128) with white text.
* **Mouse.** Press on a title opens its drop-down; moving across titles while one is open switches;
  releasing or clicking on an enabled item runs its command (§3.2); clicking elsewhere or on the
  open title closes it.
* **Keyboard.** Alt (pressed and released alone) or F10 selects the first title; Left/Right move,
  Down/Enter open, Up/Down move within the drop-down (skipping separators and disabled items), Enter
  runs, Esc closes the drop-down then leaves the bar; Alt+letter opens the title with that `&` letter;
  a letter while a drop-down is open runs the matching item.
* **Pausing.** While a drop-down is open the game behaves as the original's modal menu loop: the game
  clock and animation stop and MIDI/DOS music pauses (as already specified for menu commands and in
  12 §5.1), and resume when the menu closes.
* **Conformance.** Frames captured in play mode (`frame`) are the 512×384 canvas only, as before; add
  `frame NAME full` to capture the whole 512×404 frame including the bar. The `menu ID` script command
  is unchanged; add `menuclick X Y` (frame coordinates) to drive the bar with the mouse.


### 3.0a Menu behaviour on every screen (v1.31)

# Menu behaviour on every screen (answer for spec 03 §3.0 / §3.2)

Behaviour-only description of the original. Confidence: H = observed in the original's logic, M = inferred.
Where the original has a defect, the recommended behaviour is given as well.

## A. What happens when a menu is opened (H)
* The original does not respond to "menu loop entered/exited" notifications. It responds to only two
  menu events:
  - **A menu opens.** This covers any drop-down of the menu bar and also the window's system menu.
    The program then:
    1. sets the arrow cursor;
    2. **pauses** every playing wave channel (sound effects **and** speech) and the MIDI song;
    3. if the game clock is live (hall, rooms, decoder, ending before the bonus/score part), stamps
       the **PAUSE** cel (cel 76) into the clock box at (402,303) and shows it at once. In Drill for
       Skill the same cel covers the Time field.
  - **The menu closes** (with or without a choice). The program:
    1. resumes the paused waves and MIDI from where they stopped (nothing restarts);
    2. redraws the clock box with the current time, which appears with the next screen update. In
       Drill for Skill it erases PAUSE, blanks the Time digits until the next timer tick, and
       switches the drill timer back on.
  - Moving the highlight between items has no effect.
* Pauses nest. A menu opened while sound is already paused (for example by losing focus) does not
  pause twice, and sound resumes only when everything that paused it has ended.
* **The game freezes while a drop-down is open.** Animation, scripted sequences, delays, the frame
  loop and input handling all stop, because the program is inside the Windows modal menu loop.
* **The game clock is not frozen by opening a menu.** The minute in progress keeps counting in real
  time. After the menu closes, if that minute has expired, the clock advances by **exactly one
  minute** at the next game frame. There is never more than one, because the next minute is timed
  from that moment. A port that freezes the clock completely while the menu is open differs from the
  original by at most one game minute. Either is acceptable; the recommendation is to freeze it.
* **A chosen command runs after the menu has closed**, so sound has already resumed. It runs at
  once, inside whatever the game was doing. Almost every command opens a dialog, and opening a
  dialog **stops** all sound effects and speech (they do not continue afterwards) and pauses MIDI
  until the dialog closes. Any "wait until this speech/sound ends" step that was interrupted then
  continues straight away. The one command without a dialog is Clock while the clock option is
  already off; it does nothing.
* While any dialog is open (hints, rank, confirmations, Out of Time, help…), the menu bar cannot be
  used.
* While a command runs in the game or in Drill (but not on the sign-in screen), the game clock (or
  the drill timer) is paused for the whole command and the clock box shows PAUSE. This is the
  existing §3.1 rule.

## B. When the menu bar can be opened (H)
* The original handles window messages (including menu clicks and Alt/F10) every time it shows a
  frame, in every scripted delay, while waiting for a sound, speech or music to end, and while
  waiting for a key. So the bar can be opened at **any moment of any screen**, including the
  middle of animations. Examples: the intro, challenge build-ups, the decoder spin, door/elevator
  walks, endings, the midnight flashes, bonus tallies, and drill static and praise.
* While the drop-down is open, the animation **freezes on its current frame**. After it closes, the
  animation continues from that frame. Delays run on real time, so a delay that was in progress
  ends immediately if the menu stayed open longer than the delay. Running sprite animations
  continue from their current frame and drop at most one frame. Nothing replays and nothing jumps
  ahead by more than that.
* A command chosen mid-animation runs **at once** (it is not deferred to the end of the sequence).
  Return to Sign In and Exit (after "Yes") leave the animation immediately. Option toggles take
  effect at once. For example, Music off while the game waits for a song to finish ends that wait.
  Voice off during spoken text ends the speech and the step that waits for it.
* Keys typed while no menu is open are stored in the game's 16-key buffer (new keys are dropped when
  it is full). They are used by the next screen that reads keys. The buffer is cleared only when
  the sign-in screen opens and when the intro starts. So, for example, an Enter typed during an
  ending can dismiss the Continue prompt at once (M for how often this happens in practice). Keys
  pressed while a drop-down is open go to the menu, not the game. Mouse clicks on game hot spots
  are handled immediately.

## C. Items greyed per phase (H)
The menu titles can always be opened. In the phases below, **every item of every menu is greyed**,
so nothing can be chosen. Opening a title still pauses sound as in A.

| phase | menu set | items |
|---|---|---|
| Intro / title (attract loop) | A | **all greyed** |
| Sign-in screen | A | enabled (Clock as per option) |
| Game: hall, rooms, math challenge, decoder | Game | enabled |
| Decide → door → taunt → outcome (win **and** lose), MoM caught, bonus panel, award, victory music, **score summary** | Game | **all greyed** |
| Lose: trap door, gloat, score/energy drain, midnight sequence | Game | **all greyed** |
| Out of Time: midnight sequence | Game | **all greyed**; enabled again when it ends, then the Out of Time dialog 9136 (modal) |
| "Continue" prompt after a win or a loss | Game | **enabled** |
| Drill for Skill, including end-of-set animations and its Continue prompt | Drill | enabled |

Clock relevance in these phases:
* From the Decide sequence up to the bonus panel, the clock box is still live, so opening a menu
  shows PAUSE there and closing it restores the time.
* From the score summary, the midnight sequence and the Continue prompt onwards, the clock is
  finished, and opening a menu does not touch the clock box.

The Continue prompt (enabled items):
* **After a win** the player counts as signed out, because the game was already recorded at the
  score summary. Return to Sign In shows 23007 and Exit shows 23013. Nothing is saved.
* **After a loss** the player is still signed in. Return to Sign In shows 23006 and Exit shows
  23008, and the original saves the lost game: clock at 12:00, score 0, energy 0. Resuming that
  save gives "Out of Time" at once (M). **Recommendation:** treat the game as finished here. Use
  23007/23013 and do not save.

Side note: re-enabling all items also un-greys Clock when the clock option is off. Choosing it then
does nothing. The next game greys it again. A port may keep Clock greyed instead.

## D. Return to Sign In (1000 / 1300) mid-challenge or in the decoder (H)
* Player signed in → dialog **23006** ("your game will be saved"). Yes saves the game **exactly as
  it is at that instant** and goes straight to the sign-in screen. No closes the dialog and play
  continues. Dialog 23007 is used only when nobody is signed in (Drill for Skill; after a win's
  score summary).
* **Math challenge** → treated like leaving it with Esc. The challenge stays available.
  - The wrong tries already used are kept: a wrong answer counts the moment it is judged wrong,
    before its feedback text.
  - On resume, the same problem (same object, question and operation) can be opened again, with
    those tries still counted against its points.
  - If this happens during the "correct" praise, before the challenge screen closes, the points are
    **not** awarded and the challenge is still open on resume.
  - Original defect: if it happens during the feedback after the **third** wrong answer, the object
    is saved as "no challenge" (it disappears without being marked failed, and no clue is
    processed). **Recommendation:** save it as failed (as if the feedback had finished), or keep it
    available.
  - Original defect: after leaving a challenge this way, the rest of that program run no longer
    pauses the clock around menu commands. Do not reproduce.
* **Decoder** → simply abandoned. Nothing about the decoder is part of the saved game. The resumed
  game starts normally.

## E. Problem Customization (1106) mid-challenge (H)
* **The problem on screen is not changed.** It was generated when the challenge opened.
* On OK:
  - The new settings become the live settings. If they differ from the level defaults, they are
    stored in the player record (marked "customised") and the record is written.
  - The question variant of every object and the operation of every unsolved object are drawn
    again. So any challenge opened later in this game, including reopening the current one after
    leaving it, uses the new operations.
* The **Level** chosen in the dialog applies only from the **next new game**. The current game and a
  resumed saved game keep the level they started with.
* The number-emphasis and format choices affect only Drill for Skill.
* Cancel changes nothing.

## F. Calculator toggle (606 / 1108) mid-challenge (H for the art, M for the exact input effects)
* After the Yes/No dialog the option toggles at once, but the calculator art (8531 with the
  operator column, 8534 without) is **not redrawn**. The art changes only when the next challenge
  opens.
* Input handling follows the new setting immediately:
  - whether operator keys are accepted;
  - whether keypad clicks act as calculator keys;
  - whether the operator symbol shows in the answer box.

  So until the challenge is left, the art and the behaviour can disagree.
* **Recommendation:** redraw the calculator art at once when the option changes during a challenge,
  keeping the entry. This is the cleaner choice, but it is a deviation from the original.
* The "calculator off/on" New Player Hint (1010/1011) is chosen when the challenge opens and is not
  affected by the toggle.

### 3.1 Menu sets (H)
| set | menus | active |
|---|---|---|
| A | 500 File (See Rank…, Exit), 600 Options (Hints, SFX, Music, Voice, Calculator, Clock) | intro, title and sign-in |
| Game | 1000 File, 1100 Options, 1200 Help | from game start until the game ends |
| Drill | 1300 File, 1400 Options, 1500 Help | Drill for Skill |
| none | menu bar removed | briefly, after a game or drill ends |

A command is routed by id: menu = id rounded down to a multiple of 100, item = id mod 100. While
set Game or Drill is active, the game clock is paused around the command.

### 3.2 Commands (H)
| ids | command | behaviour |
|---|---|---|
| 500, 1002 | See Rank… | Read the header from the data file. Sort the players by lifetime score (descending; empty names last). Show the top 10 in DIALOG 200 "Rank": three columns **Name / Rank / Score** (rank title derived from the score, score with commas). Layout (H): DIALOG 200 has three multi-line static controls, **id 100** (Name, x 5 y 5 w 46 h 90 DLU, left), **id 101** (Rank, x 70, w 44, left) and **id 102** (Score, x 110, w 45, **right-aligned**). Their resource text is the placeholder "Text", which is replaced. A black 1-DLU line (x 5, y 12, w 150) separates the header. Each control's text is built as the header line ("Name" / "Rank" / "Score") followed by one line per player, every line terminated by a single CR (0x0D) character (a line break). The loop looks at sorted positions 0..9 and **skips players whose name is empty** (no blank row is added for them). |
| 502, 1004, 1302 | Exit | Exit confirmation (§3.3) |
| 1000, 1300 | Return to Sign In | Player signed in → dialog 23006 "game will be saved" (Yes: **save game**, go to sign-in). No player → 23007 (Yes: go to sign-in). |
| 600, 1100 | New Player Hints | On → 22012 "turn off?"; Off → 22013 "turn on?". Yes toggles the check mark. |
| 602, 1102, 1400 | Sound Effects | 22000 / 22001. Turning off stops all wave channels (speech too). |
| 603, 1103, 1401 | Music | 22002 / 22003. Off stops the current song; on restarts the remembered current song with its loop flag. |
| 604, 1104 | Voice | 22004 / 22005. Off stops the current speech. |
| 606, 1108 | Calculator | 22010 / 22011. Toggles and records "calculator changed". (Dialogs 22014/22015 do not exist.) |
| 607, 1109 | Clock | Only while on: 22016 "turn clock off for the rest of the game?". Yes → off, item greyed, clock display blanked. It cannot be turned back on. |
| 1106, 1403 | Problem Customization… | Game → DIALOG 221 (Level 1–4); Drill → DIALOG 220 (set size 5/10/15/20). See §4.5. |
| 1200 | Moving Around… | help dialog 12500 (1 page) |
| 1201 | The Game Plan… | 12400–12401 (2 pages) |
| 1202 | Solving Math Problems… | 12300 |
| 1203 | Inside Telly's Belly… | 12200 |
| 1204 | Winning the Game… | 12100 |
| 1205 | Educational Focus… | 12000–12004 (5 pages) |
| 1500 | Playing Drill for Skill… | 12600 |

Help dialogs page with buttons 100 "Go Back a Page", 101 "See Next Page" and 102 "Done". A button
with id 200+n ends the dialog with result n. Check marks are re-synchronised whenever a menu set is
installed.

Keyboard accelerator: **Ctrl+S** toggles all sound. If SFX, Music and Voice are all on, all three go
off; otherwise all three go on. Ctrl+R posts 0x12 (Remove player at sign-in).

### 3.3 Exit confirmation (H)
Player signed in → DIALOG **23008** ("Your game will be saved…"); Yes → save game (§4.3), quit.
No player → **23013**; Yes → quit. The same applies to the window close box.

---------------------------------------------------------------------------------------------------
## 4. Persistence

### 4.1 Files
- `ssowincd.dat`: player database, in the **current directory**. It is not on the CD path; the CD
  ships a blank copy, and the installer is assumed to copy it. If the file is missing it is created
  ("w+b") with a fresh header and 99 default records.
- `hiscore.dat`: **write-only**. A 150-byte table of the session's top-10 single-game scores is
  written after every game, but the file is never read back. The table starts empty each run.
  A reimplementation may drop it.
- Strings `users.dat`, `mocket.dat`, `*.gra`, `help.dat` are DOS leftovers and are unused.

### 4.2 `SSOWINCD.DAT` layout (H). Little-endian. Total size 18,140 = 1508 + 99 × 168.
**Header (0x5E4 = 1508 bytes)**
| off | size | field |
|---|---|---|
| 0 | 4 | magic `55 53 52 1A` ("USR\x1a") |
| 4 | 2 | reserved, 0 (never read) |
| 6 | 2 | **player count N** (0..99) |
| 8 | 100 × 15 | list entries, **sorted by name**. Only the first N are meaningful; the code initialises 99, and the 100th is never used (the blank file holds garbage there). |

List entry (15 bytes) = a copy of the first 15 bytes of the player record:
| off | size | field |
|---|---|---|
| 0 | 9 | name, NUL-terminated (max 8 chars) |
| 9 | 4 | lifetime score (int32). In memory, −1 / −2 mark provisional entries; only ≥ 0 are saved. |
| 13 | 2 | record slot index (0..98) → record at file offset 1508 + slot × 168 |

A fresh file sets entry i's slot to i. Remove/insert shuffle whole 15-byte entries, so the slot
indices travel with them and free slots stay at the end.

**Player record (168 = 0xA8 bytes)**
| off | size | field | default (new player) |
|---|---|---|---|
| 0x00 | 9 | name | |
| 0x09 | 4 | lifetime score (uint32, capped at 9,999,999) | 0 |
| 0x0D | 2 | own slot index | slot |
| 0x0F | 13 | **game** customisation (§4.5); byte 12 = 0xFF means "unset" | defaults for level 0 |
| 0x1C | 13 | **drill** customisation; byte 12 = 0xFF means "unset" | defaults for level 0 |
| 0x29 | 1 | flags: bit0 = drill customised, bit1 = game customised. The Reset button clears the respective bit. | 1 (blank file) |
| 0x2A | 1 | unused | 0 |
| 0x2B | 1 | **saved game present** (1 = an unfinished game is stored) | 0 |
| 0x2C | 99 | **saved game state** (copy of the live state block, §4.4) | 0 |
| 0x8F | 20 | **math-challenge problem seeds** `objectSeed[0..9]` (10 × int16, values 1..10; chapter 06 §4). Loaded at sign-in, regenerated at each new game, written with the record | 0 |
| 0xA3 | 1 | Sound effects on | 1 |
| 0xA4 | 1 | Music on | 1 |
| 0xA5 | 1 | Voice on | 1 |
| 0xA6 | 1 | rank-intro stage (0 = 9130 not yet shown, 1 = 9131 pending, 2 = done) | 0 |
| 0xA7 | 1 | options: bit0 Calculator, bit1 New Player Hints, bit2 Clock | 7 |

Write points:
- The record (168 bytes at its slot) is written, and then its 15-byte header entry rewritten, at:
  game start (§2.4), Out-of-Time, the score screen, and Save Game.
- The whole header is rewritten when a player is created or removed.
- Nothing is written when no player is signed in.

### 4.3 Save Game (H)
Runs on Exit and Return to Sign In while a player is signed in:
(**v1.31 robustness rule, non-original:** Save Game does nothing unless this game's live state has
been initialised, i.e. §2.5 step 3 has completed for the current game (new game generated or saved game
restored). In particular, Exit / window close / Return to Sign In while the rank dialog of §2.5 step 2 is
open, or anywhere before step 3, must not set the saved flag. Also, at sign-in, a saved game whose state
is invalid — clock outside 900..1200, or the four secret-code pictures not all in 0..19 and distinct,
or all 99 bytes zero — is discarded: clear the saved flag and start a new game.)
1. Store Sound / Music / Voice and the options byte into the record.
2. Set the saved flag = 1 and copy the 99-byte live state into record +0x2C.
3. Write the record and hiscore.dat.

The game is resumed automatically at the next sign-in (§2.4–2.5). The saved flag is cleared when
the game ends (out of time, or score screen). Sound settings and options are restored **only when
a saved game is resumed**. New games keep the in-memory settings, except that Clock is forced on
and Hints are set by rank at sign-in.

Note: at the start of the Decide door sequence the code also writes the state with the saved flag
set (M). If the program is quit during that sequence, the game resumes there.

### 4.4 Live game-state block (99 bytes = 0x63) (H for offsets; meanings per chapters 04–06)
| off | size | field |
|---|---|---|
| 0x00 | 2 | game seed (RNG-A value used at new game; re-applied with srand on resume) |
| 0x02 | 2 | game score (16-bit; added to lifetime only on a win) |
| 0x04 | 2 | clock, hours×100+minutes on a 12-h dial (900 = 9:00 PM start, 1200 = midnight) |
| 0x06 | 1 | word-problem level 0..3 |
| 0x07 | 1 | zapper energy (new game: 12) |
| 0x08 | 1 | pending energy credit (collected at the energy supply machine) |
| 0x09 | 10 | math-challenge object state[10]: 0 none, 1 has challenge, 2 solved, 3 failed |
| 0x13 | 10 | object operation (MTHT op index 0 Add, 1 Divide, 2 Multiply, 3 Subtract) |
| 0x1D | 10 | object question number 0..3 |
| 0x27 | 10 | object wrong-try counters (0..3) |
| 0x31 | 20 | room clues: 5 rooms × 4 slots, picture id 0..19, bit 7 set = not yet revealed |
| 0x45 | 4 | secret code: 4 slots, picture id, bit 7 set = not yet captured |
| 0x49 | 2 | number of code pieces captured |
| 0x4B | 2 | match rule (0, 1, 2) |
| 0x4D | 18 | New Player Hint flags, index i ↔ dialog/speech 1000+i: 0 = not triggered, 1 = pending, 0xFF = shown |
| 0x5F | 2 | unused (no reader found) |
| 0x61 | 2 | hideout room index 0..4 |

Not saved (re-created on resume): player position (a resumed game starts in the hall at the
new-game start position), Telly/LiveWire, and the energy-machine location (re-rolled). The
math-challenge problem seeds live in the player record (§4.2, +0x8F). (Resume start position: M — the hall start used for new games.)

### 4.5 Customisation block (13 bytes) (H)
- byte n (n = 0..12): bit 2 = number n emphasised.
- bytes 0..3: bit 4 = operation enabled (0 Add, 1 Multiply, 2 Subtract, 3 Divide; the dialog's checkbox order is 101 Add, 102 Mult, 103 Sub, 104 Div).
- bytes 0..2: bit 3 = format enabled (0 `a+b=?`, 1 `?+b=c`, 2 `a+?=c`).
- byte 0 bits 0–1 = level (dialog 221) or set-size index (dialog 220: 5/10/15/20).

Validation on OK:
- some group has no box checked → dialog 140;
- too few numbers checked for division/multiplication → dialog 141.

**Reset** restores the defaults for the player's level (rank ÷ 2) and clears the matching
"customised" bit. Cancel restores the previous values. When the game customisation changes, the new
settings take effect from the next game: when the game customisation is changed during a game, DIALOG 20108 ("…will not be used until your next game") is shown (M).

---------------------------------------------------------------------------------------------------
## 5. Audio

### 5.1 Sound manager model (H)
- **Sound items** are loaded by resource id and type:
  - SFX = WAVE resource, normally from SND.DAT (or SSO3);
  - speech = WAVE resource from SPEECH.DAT;
  - music = a TEXT resource (in SND.DAT) holding a MIDI file name.
- Global switches: SFX, Music, Voice. Turning SFX or Voice off stops the playing channels; turning
  Music off stops the song.
- **Channels:**
  - With WaveMix: a session of mono 22 kHz, all 8 WaveMix channels opened, and the game uses
    channels **1..4** (4 simultaneous waves).
  - Without WaveMix: one waveOut stream (channel 1). A new sound pre-empts the old one.
- **Allocation:**
  1. Take the first free channel.
  2. Otherwise, steal the channel with the lowest priority, if that priority is below the new sound's.
  3. Otherwise, drop the new sound.
  SFX priorities passed by callers range from 2 to 35. Speech always has priority 1000.
  **Starting speech stops every playing wave first.**
- `PlaySFX(id, priority, loop)`:
  - Ignored when SFX is off.
  - If the sound is already playing, it is restarted only when flag bit 1 is set.
  - loop ≠ 0 → loop forever (waveOut loop count −1, or WaveMix loops −1).
- Speech helpers: "play", "play and auto-unload when finished", and "speak and wait". The wait
  versions pump input until the speech ends.
- **Music:**
  - File = MIDI folder + (`b` or `e`) + the TEXT string. The TEXT strings start with a space
    (" enter.mid"), and that first character is overwritten with the b/e letter.
  - Played through MCI "sequencer" with a completion notification. If the loop flag is set, the
    song seeks to the start and plays again when it ends.
  - Only one song plays at a time. The current song id is remembered so that switching Music back on
    resumes it.
  - Pause/resume calls nest.
- **B/E autodetect** when MIDIType = −1:
  1. If `%WINDIR%\system\midimap.cfg` exists → default **E**.
  2. Read the current-setup index (word at offset 6) and the 16-byte setup name at 0x12 + (index−1) × 0x36.
  3. Upper-case the name. If it contains ADLIB, AD LIB, MVI PRO, BAS, BASE or THUNDER BOARD → **B**.
  Without midimap.cfg the MIDI check fails (dialog 3041); the code default would be B.
- **Levels:** the original sets no volume anywhere (no waveOutSetVolume, no WaveMix or MIDI volume,
  no CC7/CC11 in the MIDI files). The engine's non-original level balance, normalisation and volume
  sliders are in chapter 15.
- **WaveMix failure handling:** any WaveMix error converts every loaded SFX/speech to plain waveOut,
  closes WaveMix and writes `wavemix=0` to the INI.

### 5.2 Music catalogue (H for ids and call sites; M for the "when")
| TEXT id | file | when | loop |
|---|---|---|---|
| 1724 | intro.mid | title / intro screen | yes |
| 1728 / 1729 | level1.mid / level2.mid | hall background: level1 on floor 1 (areas 0–4), level2 on floor 2 (areas 5–9) (H) | yes |
| 1745 / 1746 | room1.mid / room2.mid | inside rooms: room1 for rooms entered from floor 1 (NEWS, GAME SHOW), room2 for floor 2 (CARTOON, EQUIPMENT, SOUND) (H) | yes |
| 1735 | midnite.mid | midnight takeover (out of time, or after a wrong decision) | once; the game waits for it to end |
| 1717 | enter.mid | **every room entry** through a hall door (the room is built after it ends; 04 §14), and the Decide door sequence | once, waited |
| 1757 | victory.mid | MoM caught; end of each Drill set | once, waited |

Background music is re-evaluated every action-game frame (only while the action game owns the music, i.e. not during scripted sequences) and set through one helper. It stops the previous song and plays the new one
looped, and does nothing if the requested song is already the current one.

### 5.3 Sound-effect catalogue (SND.DAT WAVE ids; meaning inferred from the calling code, M)
| id | where it plays |
|---|---|
| 1756 | UI click: sign-in buttons, decoder buttons, dialog/answer feedback; also math-challenge on-screen keys and the challenge Exit button (prio 2) |
| 1743 | math challenge: correct answer (prio 2, waited) |
| 1760 | math challenge: wrong answer (prio 2; waited on the 3rd wrong); also opens the wrong-decision sequence |
| 1734 | Map display (prio 10) |
| 1701 | Bar Graph display: each bar growing (prio 10, cut when the bar stops) |
| 1742 | Prize board display: price flashing (prio 10) |
| 1751 | Spinner display: tick per pointer step (prio 2) |
| 1722 | Great 8 display: chart animation (prio 10) |
| 1736 | Volume display: slider knob moving (prio 10, cut when the knob stops) |
| 1740 | Monitor display: TV static (prio 10) |
| 1718 | Equipment list display (prio 10) |
| 1730 | Line Graph display: each line segment (prio 10) |
| 1758 | VCR display: each tape appearing (prio 10) |
| 1753, 1754, 1763 | Telly pop-in / hum / flash (hall; chapter 04) — not challenge sounds |
| 1725 | clock chime at each full hour and at 11:50 (entry 25 of the hall SFX table) |
| 1747 | score change |
| 1712 | decoder slot-machine spin when code/clue pictures are revealed (H) |
| 1748, 1749 | bonus panel slides in / slides out (tally ticks use 1703, plus 1747 on every score change) |
| 1731, 1733 | end-of-game sequence, stop/start pair |
| 1704, 1719, 1720, 1721, 1726, 1731, 1732 | player movement / jump / flip / collisions (hall engine) |
| 1723 | priority-3 effect in the hall engine (chapter 04) |
| 1713, 1714 | Telly / zapper effects (prio 5 and 6) |
| 1744, 1761 | drill answer right / wrong (H) |
| 1709, 1710 | code-piece reveal animation frames / end (H) |
| 1711 | drill set failed ("No Code") (H) |
| 1752 | drill question start (prio 10) |
| SSO3 11000, 11001 | intro animation |

Preloaded lists: INTS 1700 holds the hall SFX (1764 1765 1731 1747 1723 1704 1719 1732 1720 1721
1726 1754 1763 1753 1733 1706 1750 1700 1727). INTS 1701/1702/1703 are the decide-sequence lists;
1702 = correct, 1703 = wrong. Their entries are played by animation-script sound cues (user opcodes −500…−504; table in chapter 07
Part B §2.5).
The meanings above are inferred from where the sounds are triggered; confirm by ear while implementing. The action-game chapter (04) names the sound for each hall event where known.

### 5.4 SPEECH.DAT catalogue (H for the mapping)
| ids | content |
|---|---|
| 1000–1012, 1014, 1016 | spoken text of New Player Hint dialogs 1000+i (same id) |
| 1803–1809 | rank dialogs: 1809 Trainee, 1805 Junior, 1808 Senior, 1807 Pro, 1804 Expert, 1806 Master, 1803 All-Star |
| 1830, 1831, 1832, 1833 | dialogs 9130, 9131, 9133, 134 (Champion) |
| 1838–1845 | player taunt at the hideout door = TEXT 500–507 (random 0..7) |
| 1846–1853 | MoM caught = TEXT 508–515 (same random index picks the ASEQ 1013+i portrait) |
| 1854–1861 | MoM wins (wrong decision) = TEXT 516–523 |
| 1862–1870 | intro animation narration: 9 lines played in order, one per animation stage |
| 1800, 1801, 1802 | Match Rule dialogs 25000, 25001, 25002 (index = match rule) |
| 1836, 1835, 1829, 1834, 1837 | Decide confirmation dialogs 1017..1021 for rooms NEWS, GAME SHOW, CARTOON, EQUIPMENT, SOUND respectively ("Are you sure … the <room>?") |
| 1828 | not referenced by the game (spare recording) |

**Voice on** → the line is spoken and no bubble is shown. **Voice off** → the matching TEXT line is
drawn in a speech-bubble ASEQ. Bubble placement and timing: chapter 07 Part B §2 (player taunt ≈ 4.0 s, Master of Mischief ≈ 3 s).

### 5.5 New Player Hints (H)
- Shown only while the **New Player Hints** option is on. Each hint shows **once per game**: the
  flags live in the game state, are cleared for every new game and are kept in saved games.
- Showing a hint pauses the clock, stops waves, and opens DIALOG 1000+i with SPEECH 1000+i.
- Two modes:
  - **immediate**: shown now if not yet shown; it is marked shown even when hints are off.
  - **armed**: becomes pending; the game loop shows at most one pending hint per frame, and only
    while the player is in a normal walking state.
| hint | trigger |
|---|---|
| 1000 | clock reaches 9:02 (2 game-minutes into a new game), immediate |
| 1001 | Telly's trick hits the player (armed) |
| 1002 | Telly crashes into the player (armed) |
| 1003 | a secret-code piece is captured (immediate) |
| 1004 | energy machine used before solving a challenge (armed) |
| 1005 | entering a room (immediate; once per game) |
| 1006 | LiveWire hits the player (armed) |
| 1007 / 1008 | after a math challenge: solved (state 2) / tried without points |
| 1009 | drill questions: shown **3 s** after the questions appear unless the player starts typing; typing cancels it and marks it shown |
| 1010 / 1011 | opening a math challenge with the calculator off / on |
| 1012 | zapper energy low (armed) |
| 1014 | Tab (decoder) pressed |
| 1016 | first opening of Problem Customization during a game (M; the dialog text explains that dialog) |

---------------------------------------------------------------------------------------------------
## 6. Input

### 6.1 Input queue (H)
A 16-entry ring buffer of key codes:
- WM_CHAR → the ASCII character. Ctrl+letter goes to the accelerator handler: Ctrl+S toggles all
  sound; Ctrl+R queues 0x12.
- WM_KEYDOWN, first press only (not auto-repeat): PgUp/PgDn/End/Home/←/↑/→/↓ queue their VK codes
  0x21–0x28; F1–F24 queue 0x170–0x187.

Codes used by the shell:
| code | meaning |
|---|---|
| 0x0D | Enter |
| 0x1B | Esc |
| 0x09 | Tab = decoder |
| 0x08 | Backspace |
| 0x20 | Space (skip intro) |
| 0x26 / 0x28 | Up / Down (sign-in list) |
| 0x12 | remove player |

The VK codes overlap ASCII `!`–`(`. The original ignores this; a port should keep the two sources separate.

### 6.2 Action-game keys (polled every frame) (H)
| key | action |
|---|---|
| ← | walk left |
| → | walk right |
| ↑ | up action: jump, go through a door, use the elevator, approach a challenge, use the energy machine |
| Home, or ↑ + ← | flip left |
| ↑ + → | flip right |
| Space | zap |
NumLock is forced off during play (so keypad 4/6/8/7 act as arrows/Home) and restored on exit/restart.

### 6.3 Mouse (H)
- Clicks on hot regions (buttons, list rows, title screen) post events.
- In the hall and rooms the cursor shows the direction. Holding the **left button** performs that
  direction's action. With the player sprite's horizontal span [L, R] and a reference height Y
  (≈ the player's head):
  | mouse position | cursor (GROUP_CURSOR) | action |
  |---|---|---|
  | x > R, between the head line Y and Y+50 | 128 → | walk right |
  | x > R, above Y | 132 ↗ | flip right |
  | x < L, level | 130 ← | walk left |
  | x < L, above Y | 131 ↖ | flip left |
  | L ≤ x ≤ R, above Y | 129 ↑ | up action |
  | lower than Y+50 on the side the player faces | 134 (target ring) | zap |
  | lower than Y+50 on the other side | 128 / 130 (turn) | walk |
  | elsewhere | default arrow | – |
  Cursor 133 (filled disc) is unused.

### 6.4 Timing
The internal tick is 1/100 s (timeGetTime ÷ 10). The program asks for 1 ms multimedia-timer resolution.

### 6.5 TLC Keycaps glyphs (SSO.FON, 17 px high) (H, from the glyph bitmaps)
| char | key | char | key |
|---|---|---|---|
| 0x7F | ← Left arrow | 0x83 | "C" (calculator Clear; only in unused DOS strings) |
| 0x80 | ↑ Up arrow | 0x84 | Spacebar (wide key) |
| 0x81 | → Right arrow | 0x85 | Backspace (wide key) |
| 0x82 | "=" (calculator equals) | 0x86 | Enter |
Other key-glyph codes (0x88–0x8D, 0xEA/0xEB) appear only in unused DOS-era strings.

---------------------------------------------------------------------------------------------------
## 7. Open questions
1. SPEECH 1828 is never referenced (spare recording).
2. The original's handling of an unreadable player file (dialog 9149) was not located.
3. Hint 1016's trigger is inferred (first Problem Customization during a game).
4. Bugs (dialog 128→129, 22012→23012, 149→1, 9133 unreachable): see chapter 11 §A.
5. The SFX meanings in §5.3 are partly inferred from context; listening would confirm them.
6. Whether the clock display is blanked or shows the PAUSE cel when the Clock option is turned off
   (chapter 07 says PAUSE; this chapter said blanked). Recommended: show PAUSE.

---------------------------------------------------------------------------------------------------
