# 14 — First-run setup and the per-user installation (NON-ORIGINAL ENHANCEMENT) [enh]

The original game was installed by an InstallShield program on the CD. This chapter replaces that
with the engine's own setup. When the engine is started **without `--data`** and finds no working
installation, it shows a **setup screen**. The user drops the CD image or folder (and optionally the
MS-DOS game) onto the window. The engine copies the files it needs into a **per-user folder** and
starts the game. Later starts find that folder and start the game at once.

Everything in this chapter is a design decision of this project. Nothing here changes game
behaviour: an installed game folder is an ordinary folder data source (13 §1.1). Containers are read
as in chapter 13. The DOS files are checked as in chapter 12. Explicit `--data` / `--dos-game` /
`--save-dir` keep working as before.

---------------------------------------------------------------------------------------------------
## 1. The base directory

### 1.1 Location
The **base directory** (`<base>`) is chosen in this order:
1. `--home DIR` (command line);
2. the environment variable `OUTNUMBERED_HOME`, if it is set and not empty;
3. the platform default:

| platform | default `<base>` |
|---|---|
| Linux, *BSD, other Unix | `$XDG_DATA_HOME/outnumbered`. Use `XDG_DATA_HOME` only if it is set, not empty and an **absolute** path (the XDG Base Directory rule). Otherwise use `$HOME/.local/share/outnumbered`. If `HOME` is unset, use the home folder from the user database (`getpwuid(getuid())->pw_dir`) |
| Windows | `<Local AppData>\OutNumbered`. Get Local AppData with `SHGetKnownFolderPath(FOLDERID_LocalAppData)`. If that fails, use `%LOCALAPPDATA%` |
| macOS | `$HOME/Library/Application Support/OutNumbered` |

* A relative `--home` or `OUTNUMBERED_HOME` is made absolute against the current directory at
  start-up.
* If no base can be determined, stop with `engine: cannot find a per-user data folder; use --home DIR.`
  and exit status 1.
* All defaults are inside the user's profile. No elevation or administrator rights are ever needed.
* **Do not use `SDL_GetPrefPath` on Windows.** It returns the *roaming* AppData folder, and roaming
  15 MB of game data is wrong. On Linux and macOS, `SDL_GetPrefPath` may be used only if it gives
  exactly the paths above.
* `<base>` may be a symbolic link or junction. The engine follows it.

### 1.2 Layout
```
<base>/
  install.json        manifest (§3), written last
  game/               the installed Windows game folder (§5.1)
    SSOWINCD.EXE  SSO1.DAT ... SSO.FON  SSOWINCD.DAT  SSOWINCD.INI
    MIDI/BENTER.MID ...
  dos/                optional: SSO.EXE, MOCKET.DAT (§5.2)
  saves/              default save directory: SSOWINCD.DAT (player database), outnumbered.ini, SSOWINCD.INI if written
  .install-tmp/       only during an install (§6)
  .install-old/       only during an install (§6)
  install.lock        only during an install (§6.1)
```
* File names in `game/` and `dos/` are the **upper-case logical names** of 13 §3.2 / §5, whatever case
  the source used (`MiDi/e00.mid` → `game/MIDI/E00.MID`). The engine still opens them
  case-insensitively.
* `game/` and `dos/` are data sources. The engine writes them **only** while installing (§6). It never
  writes them while the game runs (13 §1.3).
* `saves/` is the **default save directory** in every interactive run. This applies whether or not
  `--data` is given (§2.3). `--save-dir` overrides it.

### 1.3 Paths, names and permissions
* The engine keeps paths as UTF-8 strings internally. SDL drop events, SDL text input and the SDL2
  `main` arguments on Windows are already UTF-8.
  * **Windows:** convert to UTF-16 and call the wide API for every file operation (`CreateDirectoryW`,
    `CreateFileW` or `_wfopen`, `FindFirstFileW`, `MoveFileExW`, `GetFileAttributesExW`,
    `GetEnvironmentVariableW`). `SDL_RWFromFile` is acceptable, because it converts internally.
    Plain `fopen` with a UTF-8 name is **not** acceptable. For absolute paths of 248 characters or
    more, use the `\\?\` prefix.
  * **POSIX:** paths are byte strings and are passed through unchanged. Non-UTF-8 bytes are allowed.
* Spaces, non-ASCII letters and the characters `'` `!` `&` `(` `)` are allowed in every path,
  including `<base>`. No path is ever given to a shell.
* **Permissions.** Each folder the engine creates (`<base>`, missing parents such as
  `~/.local/share`, and every folder inside `<base>`) is created with mode **0700** on POSIX/macOS.
  Each file it creates gets mode **0600** (before the umask). On Windows, the folder inherits the
  ACL of Local AppData, which is already private to the user. Do not set an explicit ACL.
* Installed files are created **new**: they are writable by the user and keep no attributes from the
  source. This matters because files copied off a CD are often read-only, which would stop the user
  deleting `<base>` (Windows) or need `chmod` (POSIX). File times are not preserved.
* The engine never changes the mode of a folder that already exists.

---------------------------------------------------------------------------------------------------
## 2. Start-up resolution

### 2.1 New options
| option / variable | effect |
|---|---|
| `--home DIR` | base directory (§1.1) |
| `OUTNUMBERED_HOME` | base directory, if `--home` is not given |
| `--setup` | always show the setup screen (re-install, repair, add the DOS files). Not allowed together with `--data` or `--dos-game`: print `engine: --setup cannot be combined with --data or --dos-game.` and exit 1 |
| `--verify-install` | also check the CRC-32 of every installed file at start-up (§2.2, full check) |

### 2.2 Checking an installation
**Manifest validity.** `install.json` is valid only if all of these hold:
* It is at most 1 MiB and parses as a JSON object.
* `format` is the integer 1.
* `game` is an object or `null`, and `dos` is an object or `null`.
* Each `files` entry has these fields:
  * `path`: a string that starts with `game/` (or `dos/` in the `dos` part). It has no `\`, no empty
    component and no `.` or `..` component;
  * `size`: an integer ≥ 0;
  * `crc32`: 8 lower-case hex digits.
* A non-null `game` part lists the seven required files of §5.1.
* A non-null `dos` part lists exactly `dos/MOCKET.DAT` and `dos/SSO.EXE`.
* Unknown keys are ignored.

Any failure → reason `bad_manifest`. A missing file → `no_manifest`.

**Part check** (the `game` and `dos` parts are checked independently; files on disk that are not
listed are ignored):
* **fast** (default): every listed file exists, is a regular file and has the recorded size. First
  failure → `missing_file` or `size_mismatch`, with detail = its manifest path.
* **full** (`--verify-install`, the setup screen §7.2, `--conform install`): the fast check, then
  CRC-32 (13 §4.3) of every listed file. A mismatch → `crc_mismatch`. Files are checked in manifest
  order, and the first failure is reported.

A part is **installed** if it is non-null and passes the check. A `game: null` part counts as
`no_manifest` for the game.

### 2.3 Resolution
This is decided before any window opens. It does not write anything; it does not even create
`<base>`.

1. Determine `<base>` (§1.1).
2. **Save directory:** `--save-dir`, otherwise `<base>/saves`.
3. **Game data:**
   * `--data` given → use it (chapter 13). The game part is not checked. Reason `explicit_data`.
   * else `--setup` → setup screen, reason `forced`.
   * else the manifest's game part is installed → data = `<base>/game`, reason `installed`.
   * else → setup screen, with the reason of §2.2 (`no_manifest`, `bad_manifest`, `missing_file`,
     `size_mismatch`, `crc_mismatch`).
4. **DOS folder:** the first that applies:
   * `--dos-game`;
   * `DosGameDir` in `<save dir>/outnumbered.ini`, if it is not empty;
   * `<base>/dos`, if the manifest's dos part is installed (fast check; full with `--verify-install`);
   * none.

   Installed DOS files are therefore used even with an explicit `--data`. A `DosGameDir` set by the
   user in Audio Options **takes precedence** over the installed copy. If that folder later fails to
   load, the engine does **not** fall back to the installed copy (12 §1.3 applies). Installing DOS
   files from Audio Options clears `DosGameDir` (§9).
   If the installed dos part fails its check, stderr gets
   `engine: the installed DOS files in '<base>/dos' are damaged (<detail>); not using them.` The game
   still starts.
5. The run continues with 03 §1.1 using these paths. In `--conform play`, the save directory stays
   as in README §3.1.

If the game was started from `<base>/game` (reason `installed`) and the resource files then fail to
load (03 §1.1 step 7), the engine does not exit. It shows the setup screen with the Windows row in
state `error`, code `load_failed` (§7.4). The manifest is left unchanged. With an explicit `--data`,
the existing behaviour stays: a message and exit.

**Optional migration** (not tested): an engine that used a different default save directory before
v1.26 may copy `SSOWINCD.DAT` and `outnumbered.ini` from there into `<base>/saves`. It does this once,
and only when `<base>/saves` has neither file.

---------------------------------------------------------------------------------------------------
## 3. The manifest `install.json`

UTF-8 JSON, written by the engine in this key order (readers must not depend on the order):
```
{
 "format": 1,
 "engine": "outnumbered-engine 0.9.3",
 "installed_utc": "2026-11-14T15:23:07Z",
 "game": {
  "source": "/home/ann/Downloads/SSOWINCD.iso",
  "source_kind": "iso2048",
  "folder": "SSOWINCD",
  "files": [
   {"path": "game/MIDI/BENTER.MID", "size": 356, "crc32": "0a1b2c3d"},
   ...
  ]
 },
 "dos": {
  "source": "/home/ann/Downloads/outnumbered_dos.zip",
  "source_kind": "zip",
  "folder": "SSOUTN",
  "files": [{"path": "dos/MOCKET.DAT", ...}, {"path": "dos/SSO.EXE", ...}]
 }
}
```
* `engine`: free text naming the engine and its version.
* `installed_utc`: the time of the last install that changed the file, as ISO 8601 UTC to the second
  with `Z`.
* `source`: the absolute source path as the user gave it. It is shown to the user only.
  * POSIX: bytes that are not valid UTF-8 are written as U+FFFD.
  * JSON escaping: `"`, `\` and control characters are escaped. Other characters are written as raw
    UTF-8.
* `source_kind` and `folder`: as in `list-files` (13 §8.1). `folder` is upper case and `""` means the
  top of the source.
* `files`: sorted by byte order of `path`. `size` is in bytes. `crc32` is 8 lower-case hex digits.
* `dos` is `null` when no DOS files are installed.
* When only one part is replaced (§6), the other part's object is copied unchanged from the old
  manifest.

---------------------------------------------------------------------------------------------------
## 4. Sources and classification

### 4.1 Accepted sources
A source path is one of these:
* a Windows CD image or a `.cue` sheet (13 §1.1);
* a folder that holds a game folder (13 §3.1). This includes the CD root, a copied CD, an existing
  Windows installation, or the extracted `SSOWINCD` folder itself;
* a DOS `.zip` or a folder with the DOS files (13 §1.2, §5).

A folder that holds both the game folder and a DOS folder (for example `Games/` with `SSOWINCD/` and
`SSOUTN/`) fills **both** rows. Image files inside a dropped folder are **not** opened: drop the
image itself (§11 D4).

### 4.2 Input normalisation (typed or pasted text, `SDL_DROPTEXT` lines)
1. Trim spaces, tabs, CR and LF.
2. If the text is enclosed in a pair of `"` or `'` characters, remove that pair. Windows "Copy as path"
   adds `"…"`.
3. A `file://` URI: remove `file://` and an optional host part up to the next `/`, decode `%XX`
   escapes, and on Windows drop the `/` in front of a drive letter (`/C:/x` → `C:/x`).
4. POSIX only: a leading `~/` becomes `$HOME/`.
5. A relative path is resolved against the current directory. In `--conform play`, it is resolved
   against the script file's folder.

Paths from `SDL_DROPFILE` are used as they are.

### 4.3 Classification of one path P
Classification has **stage 1** (find the row) and **stage 2** (read and check). Interactive engines
run both stages on a worker thread, and the UI stays responsive. In `--conform` both run
synchronously.

1. P does not exist → message `not_found`. No row changes.
2. Stage 1, both roles:
   * **Windows role:** 13 §1.1 and §3.1 find the game folder. Then §5.1 picks the install set, and
     every required file must be present (otherwise `missing_files`).
   * **DOS role:** 13 §1.2 and §5 find the DOS folder.

   Each role that succeeds puts its row into state `checking`.
3. Stage 2, for each role still successful:
   * **Windows role:** read every file of the install set completely, through the chapter 13 reader,
     and record its size and CRC-32. Errors: the 13 §7.1 codes, or `read_error` for an OS read
     error in a folder source.
   * **DOS role:** read both files (13 §5, errors 13 §7.2) and run the 12 §2.6 validation. A failed
     validation gives `dos_invalid`, with the first failed check as the detail.

   Success → the row becomes `ok`. Its record holds the name, source kind, folder, the file list with
   sizes and CRCs, the file count and the total bytes.
4. Neither role succeeded (stage 1 or 2):
   * **P is a folder:** if the Windows error is not `no_game_folder` → Windows row `error`. Otherwise,
     if the DOS error is not `dos_files_not_found` → DOS row `error`. Otherwise → message
     `nothing_found`.
   * **P is a file:** if the Windows error is not `unrecognised` (an image or `.cue` was detected) →
     Windows row `error`. Otherwise, if the DOS error is not `unrecognised` (a zip was detected) → DOS
     row `error`. Otherwise → message `unknown_file`.
5. If one role succeeded, the other role's error is ignored.

**Replacement:** a result for a row (`ok` or `error`) **replaces** what the row held before, including
`installed`. Rows only change through results. Messages never change rows.

**Drops:** in SDL2, files arrive as `SDL_DROPBEGIN`, then one `SDL_DROPFILE` per path (files or
folders), then `SDL_DROPCOMPLETE`. All the paths between BEGIN and COMPLETE form **one drop**. They
are classified **in the order received**, so for one row the last path wins. Without BEGIN/COMPLETE
events, each `SDL_DROPFILE` is its own drop. Each line of `SDL_DROPTEXT` (§4.2) is a path of the drop.
Free the event strings (`SDL_free`). After the drop, the message line shows the **last** message the
drop produced. If the drop produced no message, the message line is cleared. Drops while the phase is
`installing` give the message `busy_drop` and are ignored. Drops in phase `complete` are ignored.

---------------------------------------------------------------------------------------------------
## 5. What gets installed

### 5.1 Windows game folder → `<base>/game/`
Take the logical files of the game folder (13 §3.2):
* **required:** `SSOWINCD.EXE`, `SSO1.DAT`, `SSO2.DAT`, `SSO3.DAT`, `SND.DAT`, `SPEECH.DAT`,
  `SSO.FON`. If any is missing → `missing_files`, with the missing names in byte order;
* **optional, copied if present:** `SSOWINCD.DAT` (the template of 13 §6.1), `SSOWINCD.INI` (13 §6.2),
  and every file `MIDI/<name>.MID` directly in the `MIDI` folder (one level, any name, extension
  `.MID`).
* **Never copied:** everything else. This includes `WAVEMIX.DLL`, `WAVEMIX.INI` (01 §1: not needed by
  a port), other folders such as `BACKUP/`, the root installer files, and readme files.

`SSOWINCD.DAT` is copied to `game/` **unchanged**, as a template. Nothing is copied to `saves/`. The
copy-on-first-write rule of 13 §6.1 stays in force with `<base>/game` as the data source. Players in
an installed folder's `SSOWINCD.DAT` therefore carry over at the first write.

The 01 §1 sizes are **not** checked, and resource contents are not parsed. Damaged resources show up
when the game starts (`load_failed`, §2.3). The synthetic fixtures depend on this: their files are
placeholders.

### 5.2 DOS files → `<base>/dos/`
Exactly `SSO.EXE` and `MOCKET.DAT` from the DOS folder of 13 §5. Nothing else from the zip or folder
is copied.

---------------------------------------------------------------------------------------------------
## 6. Install procedure
The installer copies the **pending** parts:
* the Windows part if the Windows row is `ok`;
* the DOS part if the DOS row is `ok`.

Parts in state `installed` are **kept**: they are not copied, and their manifest object carries over.
A row in state `none` or `error` installs nothing. An existing installed part of that kind stays only
while its row is `installed`. When a newly copied Windows part replaces the game folder, the old DOS
part survives only if the DOS row is `installed` or `ok`.

1. **Lock.** Create `<base>` and its missing parents (mode 0700). Open or create `<base>/install.lock`
   and take an exclusive non-blocking lock on it (`flock(LOCK_EX|LOCK_NB)` / `LockFileEx`). If the
   lock is not free → `busy`.
2. **Clean up.** Remove `<base>/.install-tmp` and `<base>/.install-old` recursively if they exist.
   These are leftovers of an interrupted install. Remove only these two names.
3. **Space check.** Let *S* be the total bytes of the pending parts. If the platform reports less
   than *S* + 1 MiB free (`statvfs` / `GetDiskFreeSpaceExW`) → `disk_full`. If the query fails, skip
   the check.
4. **Copy and verify.** Go through the files in order: the Windows part's files in byte order of
   their logical names, then `MOCKET.DAT` and `SSO.EXE`. For each file:
   1. **Copy:** read it again from the source and write it to `<base>/.install-tmp/game/<NAME>` or
      `…/dos/<NAME>`, creating folders as needed. Compute the CRC-32 while copying. If it differs from
      the stage-2 CRC → `source_changed`. Flush the file (`fsync` / `FlushFileBuffers`) and close it.
   2. **Verify:** read the written file back. Its size and CRC-32 must equal the recorded values,
      otherwise → `verify_failed`.
5. **Commit.** Cancelling is no longer possible from here on.
   1. If the Windows part is pending: delete `<base>/install.json`. From now on, an interruption
      leaves no manifest, so the next start shows the setup screen.
   2. For each pending part P: if `<base>/P` exists, move it to `<base>/.install-old/P`. Then rename
      `<base>/.install-tmp/P` to `<base>/P`.
   3. Write the new manifest (§3) to `<base>/install.json.tmp`, flush it, and rename it over
      `install.json`. POSIX: `rename`. Windows: `MoveFileExW(…, MOVEFILE_REPLACE_EXISTING |
      MOVEFILE_WRITE_THROUGH)`.
   4. On Windows, a rename in steps 2–3 that fails with a sharing or access error is retried up to
      10 times, 100 ms apart. Virus scanners briefly lock new files.
   5. Remove `.install-old` and `.install-tmp`. Create `<base>/saves` (mode 0700) if it is missing.
      POSIX: `fsync` `<base>` (best effort). Failures in this step are ignored.

   A failure in steps 1–3 gives `io_error`. No rollback is needed: a missing or stale manifest
   makes the next start show the setup screen, or treat the DOS part as not installed (§2.2).
6. Release and delete `install.lock` (best effort).

**Cancel** (Esc, the Cancel button, or closing the window) during step 4: stop at the next block
boundary (blocks of at most 64 KiB), close the files, remove `.install-tmp`, release the lock.
Nothing outside `.install-tmp` was changed. The result is `cancelled`.

**Errors in steps 1–4:**
1. Close the files and remove `.install-tmp`.
2. Release the lock.
3. Show the message of §7.4. Rows keep their states.
4. Button 1 becomes **Retry**, which runs the install again from step 1.

Map OS errors as follows:
* `ENOSPC` / `EDQUOT` / `ERROR_DISK_FULL` / `ERROR_HANDLE_DISK_FULL` → `disk_full`;
* `EACCES` / `EPERM` / `EROFS` / `ERROR_ACCESS_DENIED` / `ERROR_WRITE_PROTECT` → `permission`;
* `ENAMETOOLONG` / `ERROR_FILENAME_EXCED_RANGE` → `path_too_long`;
* errors reading the source → `read_error`, or the chapter 13 code;
* anything else → `io_error`.

---------------------------------------------------------------------------------------------------
## 7. The setup screen

It must work **without any game file**: it uses no game art, font, palette, sound or dialog. The
window is the normal game window, with the same size, scaling and mouse mapping as the 512×384 game
area (03 §1.1 step 2). The window title is `OutNumbered! Setup`. No audio device is needed. The
screen runs at 100 Hz like the game: each tick processes input, does install work and redraws.

### 7.1 Font, palette, text
* **Font:** **font8x8** by Daniel Hepper (public domain, https://github.com/dhepper/font8x8), tables
  `font8x8_basic` (U+0000–U+007F) and `font8x8_ext_latin` (U+00A0–U+00FF). Vendor the two header
  files unchanged and keep their public-domain notice. The build has no network access (README §3),
  so the dirty side delivers the two files with this chapter, as it does for the SoundFont.
  * Each glyph is 8 bytes, one per row, top row first. Bit 0 is the **leftmost** pixel.
  * Cell 8×8, advance 8 px. Only set pixels are drawn: the background stays.
  * Characters are decoded from UTF-8. Code points with no glyph in the two tables are drawn as `?`.
    This covers U+0000–U+001F, U+007F–U+009F, everything above U+00FF and invalid UTF-8 bytes.
  * Scale 2 (title only): every font pixel becomes 2×2.
* "Text at (x, y)" means the top-left corner of the first cell. Text is not wrapped. Lengths are
  counted in code points.
* **Elision to N characters:** a longer string becomes its first ⌈(N−3)/2⌉ characters, then `...`,
  then its last ⌊(N−3)/2⌋ characters.
* **Palette** (RGB):

| # | use | RGB |
|---|---|---|
| 0 | background | 14 18 2C |
| 1 | row panel fill | 23 2A 45 |
| 2 | borders | 3A 45 70 |
| 3 | normal text | F0 F0 F0 |
| 4 | dim text, disabled label | 9A A3 C0 |
| 5 | title, focus border, caret | FF D3 4E |
| 6 | OK / success, progress fill | 6B D4 6B |
| 7 | error | FF 6B 6B |
| 8 | checking / busy | 5F C8 FF |
| 9 | button face | 3C 4A 80 |
| 10 | button face pressed | 2A 35 60 |
| 11 | button face disabled | 26 2C 44 |
| 12 | text-field background | 0A 0C 18 |

Rectangles are given as inclusive pixel ranges x0..x1, y0..y1. A "border" is a 1-pixel outline on
the rectangle's edge.

### 7.2 Layout (canvas 512×384, background colour 0)
| element | position | content / colour |
|---|---|---|
| title | (112,12), scale 2 | `OutNumbered! Setup`, 5 |
| instructions | (16,40), (16,50), (16,60) | `Drag and drop your OutNumbered! CD image (.iso, .bin/.cue)` / `or its folder, and optionally the MS-DOS version's .zip or` / `folder, onto this window. You can also type a path below.`, 3 |
| legal line | (16,74) | `Use files from your own copy of the game.`, 4 |
| Windows row panel | x 16..495, y 92..131 | fill 1, border 2 |
| — marker | (24,100) | §7.3 |
| — label | (56,100) | `Windows CD (required)`, 3 |
| — status | (56,114) | §7.3, elided to 54 |
| DOS row panel | x 16..495, y 140..179 | fill 1, border 2 |
| — marker | (24,148) | §7.3 |
| — label | (56,148) | `MS-DOS game (optional: Tandy / PC-speaker sound)`, 3 |
| — status | (56,162) | §7.3, elided to 54 |
| field label | (16,199) | `Path:`, 3 |
| text field | x 64..495, y 194..211 | fill 12; border 2, or 5 when focused (§7.5) |
| message line | (16,222) | §7.4, elided to 60 |
| progress text | (16,244) | only in phase `installing` (§7.6) |
| progress bar | x 16..495, y 258..273 | only in phase `installing`: border 2; fill 6 over x 18..(17 + w), y 260..271, w = ⌊476 × done / total⌋ |
| base caption | (16,292) | `Files will be copied to:`, 4 |
| base path | (16,304) | `<base>` (native separators), elided to 60, 3 |
| button 1 | x 136..235, y 330..353 | §7.5 |
| button 2 | x 276..375, y 330..353 | §7.5 |
| help line | (36,368) | `Tab: next  Enter: select  Esc: quit (or cancel copying)`, 4 |

### 7.3 Rows
Each row has a state: `none`, `checking`, `ok`, `installed` or `error`. `<name>` is the last component
of the source path. A trailing separator is ignored, and for a drive root such as `D:\` it is the
whole path. `[<folder>]` is added only when the folder is not `""`. `<MB>` is bytes ÷ 1,000,000 to one
decimal place, rounded half up.

| state | marker | Windows status text | DOS status text | colour |
|---|---|---|---|---|
| none | `[ ]` | `Not provided yet.` | `Not provided (optional).` | 4 |
| checking | `[~]` | `Checking <name>...` | same | 8 |
| ok | `[+]` | `OK: <name> [<folder>], <n> files, <MB> MB` | `OK: <name> [<folder>]` | 6 |
| installed | `[+]` | `Installed (from <name>).` (name from the manifest `source`) | same | 6 |
| error | `[!]` | text of §7.4 | text of §7.4 | 7 |

The marker has the row's colour.

**Initial row states** when the screen opens:
* The manifest is read. Each part that passes the **fast** check starts as `checking`. A **full**
  check (§2.2) then runs, and the part becomes `installed`, or `error` with code `damaged_install`.
* Parts that are missing, null or fail the fast check start as `none`.
* If the screen opened because resolution failed with `bad_manifest`, `missing_file`, `size_mismatch`
  or `crc_mismatch`, the message line shows that code (§7.4).
* When the screen opens because of `load_failed` (§2.3), the Windows row is `error` / `load_failed`.

### 7.4 Messages and error texts
The message line has a kind (`none`, `info` colour 3, `success` colour 6, `error` colour 7), a code
and a text. A message stays until it is replaced or cleared (§4.3, §7.5). It does not time out.

| code | where | text |
|---|---|---|
| `not_found` | message, error | `Not found: <P>` |
| `unknown_file` | message, error | `<name>: not an OutNumbered! CD image, game folder or DOS .zip.` |
| `nothing_found` | message, error | `No OutNumbered! files found in <name>.` |
| `busy_drop` | message, error | `Please wait until the files are copied.` |
| `bad_manifest`, `missing_file`, `size_mismatch`, `crc_mismatch` | message, error (on entry) | `The installed game files are damaged or incomplete (<detail>). Drop the CD again.` (bad_manifest: detail `install.json`) |
| `cancelled` | message, info | `Installation cancelled. Nothing was changed.` |
| `installed` | message, success | `Setup complete. Starting the game...` |
| `disk_full` | message, error | `Not enough disk space in <base> (<MB> MB needed).` |
| `permission` | message, error | `Cannot write to <base>: permission denied. Try --home DIR.` |
| `path_too_long` | message, error | `The path is too long: <path>` |
| `read_error` | message or row, error | `Cannot read <file> from <name>.` |
| `source_changed` | message, error | `<name> changed after it was checked. Drop it again.` |
| `verify_failed` | message, error | `A copied file is damaged (<file>). Check the disk and retry.` |
| `busy` | message, error | `Another OutNumbered! window is installing. Try again later.` |
| `io_error` | message, error | `Installation failed: <OS error text>.` |
| `missing_files` | Windows row | `Incomplete: <name> lacks <F1>, <F2>, ...` |
| `bad_cue`, `truncated`, `bad_image`, `no_game_folder` | Windows row | `<name>: ` + the 13 §7.1 message after its `engine: '<P>' ` part, or `Cannot use the cue sheet <name>.` for bad_cue |
| `not_found` (a cue's FILE) | Windows row | `<name>: the image named in the cue sheet is missing.` |
| 13 §7.2 codes | DOS row | the 13 §7.2 reason text with `<P>` = `<name>` |
| `dos_invalid` | DOS row | `<name>: not the expected DOS files (<12 §2.6 reason>).` |
| `damaged_install` | either row | `Installed files are damaged (<detail>). Drop the source again.` |
| `load_failed` | Windows row | `The installed game files could not be loaded. Drop the CD again.` |

Only the codes are tested. The texts are recommended.

### 7.5 Controls, focus, keyboard and mouse
**Focus order:** text field → button 1 → button 2. Tab moves forward and Shift+Tab backward, skipping
disabled controls and wrapping around. The field has focus when the screen opens and after every
drop. The focused button has a 2-pixel border in colour 5 (its outer rectangle and the one inside
it). Otherwise its border is 1 pixel in colour 2.

**Button 1:**
* label `Play`: the Windows row is `installed`, the DOS row is not `ok`, and the phase is `ready` or
  `complete`;
* label `Retry`: the last install failed;
* label `Install`: otherwise.

`Install` / `Retry` are enabled when the phase is `ready`, no classification is running, and either
the Windows row is `ok`, or the Windows row is `installed` and the DOS row is `ok`. `Play` is always
enabled.

**Button 2:** `Cancel` in phase `installing`, otherwise `Quit`. Always enabled.

**Button drawing:** face colour 9 (10 while the mouse holds it pressed, 11 when disabled). The label
is centred: x = x0 + ⌊(100 − 8 × length) / 2⌋, y = 338, colour 3, or 4 when disabled.

**Text field:**
* It holds up to 4096 bytes of UTF-8 and a caret position, counted in code points.
* Text is drawn from (68,199) in colour 3. It shows 53 characters, scrolled so that the caret is
  visible.
* When the field is empty, it shows the placeholder `type or paste a path, then press Enter` in
  colour 4.
* The caret is a 1×10 bar at x = 68 + 8 × (caret column on screen), y 198..207, colour 5. It is
  drawn only while the field has focus, blinking 50 ticks on and 50 ticks off. Every edit or focus
  change restarts the "on" half.

**Keys:**

| key | field focused | button focused |
|---|---|---|
| text input (`SDL_TEXTINPUT`) | insert at the caret | — |
| Left / Right / Home / End | move the caret | — |
| Backspace / Delete | delete before / after the caret | — |
| Ctrl+V (Cmd+V on macOS) | insert the clipboard's first line (`SDL_GetClipboardText`), without CR/LF | — |
| Enter (either Enter key) | non-empty: normalise and classify (§4.2, §4.3); if this gave a row result, clear the field; on a message, keep the text. Empty: activate button 1 if it is enabled | activate the button |
| Space | insert a space | activate the button |
| Esc | phase `installing` → cancel. Otherwise: a non-empty field is cleared, an empty field → Quit | `installing` → cancel; otherwise Quit |

Text input is enabled (`SDL_StartTextInput`) while the field has focus.

**Mouse** (left button, canvas coordinates):
* Pressing on the field focuses it and puts the caret at the end.
* Pressing on an enabled button focuses it and shows the pressed face. Releasing over the same button
  activates it.
* Other clicks do nothing.

**Activation:**
* `Install` / `Retry` → start the install (§7.6).
* `Play` → start the game (§7.7).
* `Quit` → end the program with exit status 0.
* `Cancel` → cancel the install.

Closing the window (`SDL_QUIT`) = Quit. During an install it cancels first, then quits.

### 7.6 Phases and the install display
Phases: `ready` → `installing` → `complete`. A cancel or a failure returns to `ready`.
* **Starting an install:** the phase becomes `installing`, and button 1 is disabled. The work runs on
  a worker thread.
* **Progress text:**
  * `Copying <NAME> (<i>/<N>)  <pct>%` or `Verifying <NAME> (<i>/<N>)  <pct>%`, colour 3;
  * `Finishing...` during the commit.

  `<NAME>` is the path relative to the part, for example `MIDI/BENTER.MID`. `<i>` counts from 1, and
  N = all files of the pending parts.
* **Progress units:** total = 2 × *S*. Copying a byte and verifying a byte each count 1 unit.
  pct = ⌊100 × done / total⌋.
* **Success:**
  * phase `complete`, message `installed`, rows of the copied parts become `installed`;
  * the progress text and bar disappear;
  * button 1 is `Play`, and focus moves to it;
  * the game starts **150 ticks** after the commit, or earlier on Enter / Play.

### 7.7 Starting the game
The setup screen closes, and resolution (§2.3) runs again as if the engine had been started with
only the original `--home`, `--save-dir` and audio options, without `--setup`. The game then starts
normally (03 §1.1 step 7 on, intro and title). A load failure returns to the setup screen (§2.3,
`load_failed`).

### 7.8 Platform notes
* SDL2 file drops work on Windows, macOS, X11 (Xdnd) and Wayland. Some environments block or lack
  drag and drop:
  * an elevated (administrator) process on Windows does not get drops from Explorer (UIPI);
  * some window managers lack it;
  * Flatpak/Snap sandboxes may hand over `/run/user/<uid>/doc/…` portal paths, which are readable
    and work, and may refuse folders.

  The text field (§7.5) covers all of these.
* macOS: a file dropped on the application's Dock icon also arrives as `SDL_DROPFILE`, and is
  handled the same way.
* The setup screen uses no SoundFont. Audio starts only with the game.

---------------------------------------------------------------------------------------------------
## 8. Re-install, repair, uninstall
* **Repair / re-install:** start with `--setup`. The full check of §7.3 shows damaged parts. Drop the
  sources again, then click Install. A broken install found at a normal start (§2.3) also leads to
  the setup screen.
* **Adding the DOS files later:**
  * `--setup` with the DOS row only. The Windows row stays `installed`, and button 1 shows `Install`
    because the DOS row is `ok`.
  * Or use Audio Options (§9).
* **Uninstall:** the engine has no uninstaller.
  * Delete `<base>` to remove the installed files, saved players and settings. To keep the players,
    keep `<base>/saves/SSOWINCD.DAT` first.
  * The engine itself (its folder with the bundled SoundFont) is separate. Delete that folder too.
* Document all three points in the engine's user README, with the `<base>` paths of §1.1.

---------------------------------------------------------------------------------------------------
## 9. Audio Options: installing the DOS files (amends 12 §6.3 and 13 §7.2)
* **Template change:** edit control 320 becomes `5,62,140,12`. A new button
  `331 | BUTTON | 150,61,45,14 | 0x50010000 | &Install` is added. Its mnemonic is I.
* **Enabling 331:** it is enabled only if all of these hold:
  * the run's game data came from the base (`data_from` = `installed`);
  * field 320 holds a path whose last check succeeded (13 §5 and 12 §2.6);
  * that path is not `<base>/dos` itself.

  Otherwise 331 is disabled.
* **Drops while the dialog is open:** the first path of the drop replaces the text of field 320 and
  is checked at once (as on losing focus). Any other paths are ignored. Outside the setup screen and
  this dialog, drops are ignored.
* **Clicking Install:** runs §6 for the DOS part only. It is synchronous: two small files, status
  `Installing...`, no cancel. The manifest's `game` object is kept unchanged.
  * **Success:**
    * field 320 is cleared;
    * the status reads `DOS files installed in:` + `<base>/dos`;
    * the DOS files are reloaded from there (12 §1.3), and the DOS radios are enabled.
  * **Failure:** the status reads `DOS files not installed:` + the §7.4 text.
* **Empty field:** if the dos part is installed, the status reads `Using installed DOS files:` +
  `<base>/dos` instead of `No DOS game folder set.`, and the DOS radios stay enabled.
* **OK** stores the field as typed (13 §7.2), so after an install it stores an empty `DosGameDir`, and
  the installed copy is used (§2.3 step 4).
* **Cancel** after an install: the copied files and the manifest stay. `DosGameDir` stays unchanged,
  and if it is not empty it still takes precedence.
* **`--conform play`:** after the `audio` command of 12 §7.6, the dialog is reached as described
  there. The `state` field `audio.dos_game_dir` reports the INI value, and a new field
  `audio.dos_installed` (bool) reports whether `<base>/dos` is installed.

---------------------------------------------------------------------------------------------------
## 10. Conformance

### 10.1 `install`
```
engine --conform install --home DIR [SRC ...] --out result.json
```
Headless. `--home` is required (it never touches the real profile).
1. Each SRC is one drop (§4.3), in order.
2. The initial rows come from DIR's existing manifest, with a **full** check (§7.3).
3. If button 1 would then be `Install` and enabled (§7.5), the install runs (§6) to the end.
4. Otherwise nothing is written.

Exit status 0 if the result is ok, else 2.
```
{"ok": true,
 "error": null,                                  # code when ok is false
 "rows": {"windows": {"state": "ok", "error": null, "name": "data_plain.iso",
                      "source_kind": "iso2048", "folder": "SSOWINCD", "files": 73, "bytes": 30157},
          "dos":     {"state": "none", "error": null, "name": null, "source_kind": null,
                      "folder": null, "files": 0, "bytes": 0}},
 "messages": ["unknown_file"],                   # codes of all messages, in order
 "installed": ["windows"],                       # parts copied by this run, order windows, dos
 "manifest": { ...install.json after the run, or null... },
 "tree": [{"path": "game/MIDI/B00.MID", "size": 35}, ...]}
```
* `rows`: the states when the install started (or at the end if none started). `installed` rows report
  the manifest values.
* **`error` when ok is false:**
  * the Windows row's error code, if that row is `error`;
  * else `no_game_source` (no Windows row `ok` and no installed game part);
  * else the §6 error code.

  With no SRC and a valid installation, the result is ok with `installed: []`.
* `tree`: every regular file under DIR after the run, except `install.json` and `install.lock`. Paths
  are relative with `/`, sorted by byte order. Empty folders are not listed. This catches leftover
  `.install-tmp` files and any pre-copied save file.
* **Compared:** `ok`, `error`, rows (`state`, `error`, `source_kind`, `folder`, `files`, `bytes`),
  `messages`, `installed`, `tree`, and from `manifest`: `format`, plus each part's `source_kind`,
  `folder` and `files`. **Not compared:** `name`, `engine`, `installed_utc`, `source`, message texts.

### 10.2 `resolve`
```
engine --conform resolve [--home DIR] [--data P] [--dos-game P] [--save-dir P] [--setup] [--verify-install] --out r.json
```
Runs §2.3 and reports the result without opening anything else. It writes nothing except `--out`.
Exit status 0.
```
{"home": "/abs/base", "home_from": "option"|"env"|"default",
 "action": "run"|"setup",
 "reason": "installed"|"explicit_data"|"forced"|"no_manifest"|"bad_manifest"|"missing_file"|"size_mismatch"|"crc_mismatch",
 "detail": "game/SSO2.DAT" or null,
 "data": path or null, "data_from": "installed"|"option"|null,
 "dos": path or null, "dos_from": "option"|"ini"|"installed"|null,
 "dos_installed": "ok"|"none"|"missing_file"|"size_mismatch"|"crc_mismatch",
 "save_dir": path, "save_from": "option"|"default"}
```
* Paths inside the base are absolute and native.
* Paths given as options are reported as given. The INI `DosGameDir` is reported as written.
* `data` is null when the action is `setup`.
* `dos_installed` is `none` when there is no valid manifest or the dos part is null.
* `--setup` with `--data` gives the usage error of §2.1 (exit 1).

### 10.3 Play scripts on the setup screen
`engine [--home DIR] [--save-dir D] --conform play SCRIPT --out DIR2`, without `--data`, is allowed
when the script's first command (after any `seed` lines) is `setup`.

| command | meaning |
|---|---|
| `setup` | start the program on the setup screen for the resolved base, as with `--setup` (§7.3 initial states). Save directory: `--save-dir`, else `DIR2/save` |
| `drop PATH [PATH ...]` | one drop of these paths (§4.3). Paths containing spaces are written in `"…"`. Relative paths are resolved against the script's folder |
| `type TEXT` | `SDL_TEXTINPUT` of the rest of the line after one space, as UTF-8, without Enter |
| `press KEY` | as in README §3.1. New key names: `DELETE`, `END`, `SHIFT+TAB` |
| `click X Y` | a left press and release at canvas (X,Y) |
| `wait CS` | advance virtual time by CS ticks |
| `frame NAME` | write `NAME.png` (the 512×384 setup canvas) |
| `state NAME` | write `NAME.json` with `"screen": "setup"` and the `setup` object below |

**Determinism on the setup screen:**
* `drop`, `type`, `press` and `click` are handled at once, without advancing virtual time.
  Classification and the full checks are synchronous, so `checking` is never observed.
* Install work happens only in ticks advanced by `wait`. Each tick does min(4096, remaining) units
  (§7.6), across file boundaries, in the order copy file 1, verify file 1, copy file 2, and so on.
* The commit happens in the first tick after done reaches total. The phase becomes `complete` at the
  end of that tick.
* The automatic game start happens in the 150th tick after the commit tick. A cancel takes effect
  immediately.

```
"setup": {"phase": "ready"|"installing"|"complete",
          "home": "/abs/base",
          "rows": {"windows": {row as in 10.1, plus "text"}, "dos": {...}},
          "field": "…", "caret": 0, "focus": "field"|"button1"|"button2",
          "button1": {"label": "Install"|"Retry"|"Play", "enabled": false},
          "button2": {"label": "Quit"|"Cancel"},
          "message": {"kind": "none"|"info"|"success"|"error", "code": null, "text": ""},
          "progress": null or {"done": 12288, "total": 65456, "index": 66, "count": 75,
                               "file": "SPEECH.DAT", "step": "copy"|"verify"|"commit"},
          "last_result": "none"|"ok"|"cancelled"|<§6 error code>}
```
A return to the setup screen after `load_failed` starts a fresh screen state: the field is empty,
the focus is on the field, the message is none and `last_result` is `none`.

`progress` is non-null only in phase `installing`:
* `index` and `file` name the file that holds unit done + 1;
* `step` is `commit` once done = total.

All fields except `home`, `name` and `text` are compared.

### 10.4 Fixtures (`fixtures/setup/`, made by the dirty side's `tools/make_setup_fixtures.py`)
All sources are the synthetic files of `fixtures/containers/` (13 §8.3). One extra source,
`setup/dos_invalid.zip`, holds the `fixtures/dos/game_bad` files. The `homes/` folders are prepared
synthetic installations with hand-written manifests (`engine` = `"fixture"`). Copy a home to a
temporary folder before you use it. `expected.json` holds:
* `install` cases: steps of `--conform install`, with the expected result fields of §10.1;
* `resolve` cases: environment, options and the expected result. Tokens: `<home>` = the home passed,
  `<T>` = a temporary folder made by the checker;
* `play`: `play/setup_basic.txt` and the expected `setup` objects.

| case | tests |
|---|---|
| fresh_iso_zip | plain ISO + DOS zip: 73 game files (no README), 2 DOS files |
| iso_only, raw_mode2, joliet, cue (optional) | the same install set from every image kind. Joliet's mixed-case names are stored in upper case |
| folders, game_folder_itself, root_image | folder sources; the depth-2 DOS folder; folder `""`; `BACKUP/` not copied |
| last_wins | two images in one drop: the second one is used |
| dos_nested | the shallowest valid DOS folder is used |
| truncated, no_game, missing_files | Windows row errors; nothing written |
| not_image, nonexistent | messages `unknown_file` / `not_found`; `no_game_source` |
| dos_bad_crc, dos_invalid | DOS row error; the Windows part is still installed |
| dos_without_game | DOS zip on an empty home → `no_game_source` |
| add_dos_later | second run adds `dos/`; game part kept unchanged |
| repair | a damaged home (`homes/crc_mismatch`) is reinstalled; its valid DOS part is kept |
| rerun_no_sources | a valid home and no SRC: ok, nothing copied |
| spaces_home | a home path with spaces and non-ASCII letters |
| resolve: empty (and not created), good, good_no_dos, missing_file, size_mismatch, crc_mismatch (fast and `--verify-install`), bad_manifest, future_format, manifest_missing_required, unsafe_path, dos_broken, explicit_data (with and without installed DOS), forced, env, option_beats_env, save_override, ini_dos (also via `--save-dir`), dos_option, xdg_default, xdg_relative (Linux/BSD) | §2 |
| play setup_basic | unknown drop, a two-path drop, keyboard install, cancel, typed path, install, automatic start → `load_failed` (the placeholders are not loadable game files) |

---------------------------------------------------------------------------------------------------
## 11. Decisions and open points

| # | point | decision |
|---|---|---|
| D1 | `DosGameDir` (INI) vs installed DOS files | **INI wins** when it is not empty; the Audio Options install clears it (§9). No fallback to the installed copy when the INI path fails |
| D2 | Default save directory | `<base>/saves` for every interactive run, also with `--data`. Players are the same however the game is started |
| D3 | Content checks at install | containers, CRCs and 12 §2.6 for the DOS files only. NE resources are not parsed (fixture placeholders, and `load_failed` covers damage) |
| D4 | Images inside a dropped folder | not searched; drop the image itself. This avoids picking the wrong one of several images |
| D5 | Fast vs full check at start-up | fast (sizes) by default, so 15 MB is not re-read on every start; full on `--verify-install`, on the setup screen and in `install` |
| D6 | Template database | copied into `game/`, not into `saves/` (13 §6.1 unchanged) |
| D7 | Load failure from an installed base | setup screen with `load_failed`; the manifest is not changed, so the next start fails the same way until a re-install |
| D8 | Several engine processes | `install.lock`; the game itself needs no lock, since only the installer writes `game/` and `dos/` |
| D9 | Bundled SoundFont | not installed into `<base>`; it stays next to the executable (README) |
| D10 | `.zip` of the Windows CD | still not supported (13 §9 D1) → `unknown_file` |
