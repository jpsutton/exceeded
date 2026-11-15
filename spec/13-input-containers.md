# 13 — Input containers: CD image and DOS zip (NON-ORIGINAL ENHANCEMENT) [enh]

The original game ran from the mounted CD (or from an installed copy). This chapter lets the engine
use the user's files **as distributed**, without unpacking them first:

* `--data` may be a **folder** (as before), an **ISO 9660 CD image** (`.iso`), a **raw CD image**
  with 2352-byte sectors (`.bin`/`.img`), or (optional) a **`.cue`** sheet that names such an image;
* `--dos-game` and `DosGameDir` (chapter 12) may be a **folder** or a **`.zip`** archive.

Everything here is a design decision of this project. The two readers below are minimal subsets of
public formats: ISO 9660 / ECMA-119, the ZIP application note (PKWARE APPNOTE.TXT) and Deflate
(RFC 1951). Nothing in this chapter changes game behaviour: the engine must see the same logical files
and produce the same results whether the input is a folder or a container (§8).

Byte order is little-endian unless stated. "Sector" means a 2048-byte logical sector.

---------------------------------------------------------------------------------------------------
## 1. Inputs

### 1.1 `--data PATH`: what PATH may be
Decide in this order:
1. PATH does not exist → error `not_found` (§7).
2. PATH is a folder → **folder source**.
3. PATH's name ends in `.cue` (any case) → **cue sheet** (§2.9, optional; without support: `unrecognised`).
4. PATH is a file, decided by its **content** (not by its name extension):
   * file size ≥ 17 × 2048 and bytes 1–5 of sector 16 (file offset 32,769) are `CD001` → **ISO image**
     with 2048-byte sectors (`iso2048`);
   * otherwise, the first 12 bytes are the sync pattern `00 FF FF FF FF FF FF FF FF FF FF 00`, the
     file size ≥ 17 × 2352, raw sector 16 (file offset 37,632) also starts with the sync pattern, and
     its user data (§2.1) has `CD001` at bytes 1–5 → **raw image** (`raw2352`). This also covers
     CloneCD `.img` files;
   * otherwise → error `unrecognised`.

A folder or image may hold the game files at its top or one folder down (§3), so the CD image, the
CD's root folder and the `SSOWINCD` folder itself all work.

Other image formats (2336-byte MODE2 images, `.mdf`/`.mds`, `.nrg`, `.chd`, compressed images) are
**not supported**: they give `unrecognised`. A `.zip` given as `--data` is not required to work
(§9 D1).

### 1.2 `--dos-game PATH` / `DosGameDir`: what PATH may be
1. PATH does not exist → `not_found`.
2. PATH is a folder → **folder source** (§5.2).
3. PATH is a file → a **ZIP archive** (§4), decided by content (an end-of-central-directory record,
   §4.1). Otherwise → `unrecognised`.

### 1.3 Containers and data folders are read-only
The engine never creates, changes, renames or deletes anything inside a `--data` or `--dos-game`
source, whether it is a container or a folder. It does not need write permission there (a read-only
folder or a mounted CD works). Everything the engine writes goes to the save directory (§6).

---------------------------------------------------------------------------------------------------
## 2. ISO 9660 reader (read-only, minimal)

### 2.1 Sector access
* **iso2048:** logical sector *n* = file bytes `n × 2048 … n × 2048 + 2047`.
* **raw2352:** raw sector *n* = file bytes `n × 2352 …` (sector 0 at file offset 0). Each raw sector:
  | offset | size | content |
  |---|---|---|
  | 0 | 12 | sync `00 FF×10 00`; it must be present, else `bad_image` |
  | 12 | 3 | address (BCD minute/second/frame), **ignored** |
  | 15 | 1 | mode: **1** → user data at offset **16**; **2** → (form 1) user data at offset **24** (after the 8-byte subheader); other values → `bad_image` |
  | 16 or 24 | 2048 | the logical sector |
  | rest | 288 / 280 | EDC/ECC, **ignored** (not checked) |
* A sector that does not lie completely inside the file → error `truncated`.
* The image may be **longer** than the volume (the real CD image has 150 sectors more than its
  volume space size); never reject that. An engine may report `truncated` at once when the file is
  shorter than volume space size × sector size (2048 or 2352); otherwise the error appears when a
  missing sector is read.

### 2.2 Volume descriptors
Read sectors 16, 17, … in turn (at most 64). Each must have bytes 1–5 = `CD001`, else `bad_image`.
Byte 0 is the type:
* **1** = primary volume descriptor (PVD). Use the first one.
* **2** = supplementary volume descriptor. It is a **Joliet** descriptor if bytes 88–90 are
  `25 2F 40`, `25 2F 43` or `25 2F 45` (`%/@`, `%/C`, `%/E`). Using Joliet is optional (§2.8).
* **255** = terminator: stop.
* other types (0 boot record, 3 partition) are skipped.

No terminator within 64 sectors, or no PVD → `bad_image`.

### 2.3 PVD fields used
| offset | size | field |
|---|---|---|
| 80 | 8 | volume space size in sectors, both-endian u32 (informational, §2.1) |
| 128 | 4 | logical block size, both-endian u16. Must be **2048**, else `bad_image` |
| 156 | 34 | **root directory record** (§2.4) |

"Both-endian" fields store the value little-endian then big-endian. Use the **little-endian half**;
do not check the big-endian half.

### 2.4 Directory record
| offset | size | field |
|---|---|---|
| 0 | 1 | LEN_DR: record length. **0 = no more records in this sector** (§2.5) |
| 1 | 1 | XAR length: extended-attribute sectors in front of the data |
| 2 | 8 | extent location (sector number), both-endian u32 |
| 10 | 8 | data length in bytes, both-endian u32 |
| 18 | 7 | recording date and time (ignored) |
| 25 | 1 | flags: bit 0 hidden (list it anyway); **bit 1 directory**; bit 2 associated file (**skip the record**); **bit 7 multi-extent** (not the final record of the file); other bits ignored |
| 26 | 1 | file unit size, 27: interleave gap. Either non-zero → interleaved file → `bad_image` (unsupported) |
| 28 | 4 | volume sequence number (ignored) |
| 32 | 1 | LEN_FI: identifier length |
| 33 | LEN_FI | file identifier |
| 33 + LEN_FI | 0 or 1 | one padding byte `00` when LEN_FI is even |
| … | | system-use area (Rock Ridge, XA, …) up to LEN_DR: **ignored** |

The data of the record starts at sector (extent location + XAR length) and is data-length bytes long.

### 2.5 Reading a directory
A directory's records are the bytes of its extent (§2.4), data-length bytes long (a multiple of 2048).
Walk them with a byte position *p* from 0:
1. If *p* ≥ data length, stop.
2. LEN_DR = byte at *p*. If it is **0**, the rest of this sector is padding: set *p* to the start of
   the next sector and go to 1. **Records never cross a sector boundary.**
3. If LEN_DR < 34, or the record would cross the end of its sector (*p* mod 2048 + LEN_DR > 2048), or
   the end of the extent, or 33 + LEN_FI > LEN_DR → `bad_image`.
4. Skip the two special records whose identifier is the single byte `00` (this directory) or `01`
   (the parent), and records with flag bit 2. Otherwise take the record (§2.6, §2.7).
5. *p* += LEN_DR; go to 1.

Recurse into a subdirectory (flag bit 1) through its own record. Follow at most **8** directory
levels below the folder being listed. If two records in one directory have the same name (§2.6,
compared case-insensitively) and the first is not multi-extent, the **first** one wins.

### 2.6 Names
Primary-volume identifiers are bytes (`A`–`Z`, `0`–`9`, `_`, `.` and `;`). Treat them as ASCII.
1. If the identifier contains `;`, drop everything from the **last** `;` on (the version, `;1`).
2. Then, if the name ends in `.`, drop that one `.` (a file without an extension is recorded as
   `NAME.;1`).

Example: `SSOWINCD.EXE;1` → `SSOWINCD.EXE`; `README.;1` → `README`; the directory `MIDI` → `MIDI`.

All matching of names (game files, folder names, the files of 03 and 12) is **case-insensitive for
ASCII letters** only. Path separators inside the engine are `/`; there is no drive letter.

### 2.7 Files
A file's bytes are read from its extent (§2.4): sectors (extent + XAR) onward, data-length bytes.
**Multi-extent:** a record with flag bit 7 is followed in the same directory by further records with
the same name. The file is the concatenation of the extents of all those records in record order, up
to and including the first record without bit 7. A run that ends without such a record, or that
changes name → `bad_image`. Directories are never multi-extent.

### 2.8 Joliet (optional)
An engine may read the tree of a Joliet descriptor instead of the PVD. The descriptor has the same
layout as the PVD (§2.3) and the same directory records. Identifiers are UCS-2 big-endian; apply
§2.6 to the decoded text. Joliet keeps the original letter case and allows long names. The engine
must find the **same logical files** (§3) whichever tree it reads. On the real CD and in the
fixtures the primary tree is enough; the real CD has no Joliet descriptor. Rock Ridge is never needed.

### 2.9 `.cue` sheets (optional)
A text file. Use only:
* the **first** `FILE "<name>" <type>` line. The name may also be unquoted (the first word). It is
  relative to the cue sheet's folder. If no file has that exact name, use the file in that folder
  whose name matches it case-insensitively;
* the first `TRACK <nn> <mode>` line after it. The mode must be `MODE1/2048`, `MODE1/2352` or
  `MODE2/2352` (any case), else `bad_cue` (for example an `AUDIO` first track).

`INDEX`, `PREGAP` and the other lines are ignored. The named file is then opened by **content**
(§1.1 step 4). The declared mode is not trusted. A named file that is missing gives `not_found`. No
FILE or TRACK line gives `bad_cue`. The source is reported as `cue+iso2048` or `cue+raw2352`.

---------------------------------------------------------------------------------------------------
## 3. The game folder and the logical files (`--data`)

### 3.1 Finding the game folder (folder sources and images alike)
The game folder is the folder that directly contains a file named `SSOWINCD.EXE`:
1. the top (the image's root directory, or the given folder) if it contains `SSOWINCD.EXE`;
2. otherwise each **first-level** subfolder, in byte order of the upper-cased names. If several
   qualify, use the **first** one and print a warning to stderr (§7.1);
3. otherwise → error `no_game_folder`. Deeper folders are **not** searched.

On the real CD the game folder is `\SSOWINCD\` (the root holds the installer files). Giving the
extracted `SSOWINCD` folder itself, or its parent, also works.

### 3.2 Logical files
The logical files are all files in the game folder and its subfolders (§2.5 depth limit). Each has a
**logical name**: its path relative to the game folder, with `/` separators, after §2.6, for
example `SSO1.DAT`, `MIDI/EROOM1.MID`. The engine opens every data file of chapters 01, 03 and 12
by logical name, case-insensitively: the resource files `SSOWINCD.EXE`, `SSO1–3.DAT`, `SND.DAT`,
`SPEECH.DAT`, `SSO.FON`, the music `MIDI/*.MID`, and the read-only templates `SSOWINCD.INI` and
`SSOWINCD.DAT` (§6). The checks for missing files are the same as for a folder (03 §1.1 step 7).

* The INI keys `CDDrive` and `MIDIPath` (03 §1.1) name Windows drive paths of the original
  installation. A port **ignores** them: every file comes from the game folder of the data source.
* The engine may read files on demand or load them into memory. If a library needs a real file path
  (for example a MIDI player), pass the bytes from memory instead (FluidSynth:
  `fluid_player_add_mem`), or write a copy to a private temporary folder that is deleted at exit.
  Never write next to the source.

---------------------------------------------------------------------------------------------------
## 4. ZIP reader (read-only, minimal)

All values are little-endian. ZIP64, multi-part (spanned) archives and encryption are **not
required** and give the errors below.

### 4.1 End of central directory (EOCD)
The EOCD record is 22 bytes plus an archive comment of up to 65,535 bytes:
| offset | size | field |
|---|---|---|
| 0 | 4 | signature `50 4B 05 06` |
| 4 | 2 | number of this disk |
| 6 | 2 | disk where the central directory starts |
| 8 | 2 | central-directory entries on this disk |
| 10 | 2 | central-directory entries in total |
| 12 | 4 | central-directory size in bytes |
| 16 | 4 | central-directory offset |
| 20 | 2 | comment length *c* |

Search **backwards** from file offset size − 22 down to max(0, size − 22 − 65,535) for the
signature. Use the first (highest) position where position + 22 + *c* = file size. If there is none,
use the highest position where position + 22 + *c* ≤ file size (trailing junk). If there is none:
the file is `truncated` if it starts with `50 4B 03 04` (a cut-off archive), otherwise
`unrecognised`.

* The 20 bytes in front of the EOCD start with `50 4B 06 07` (ZIP64 locator), or an entry count is
  0xFFFF, or the size or offset is 0xFFFFFFFF → `zip64`.
* A disk number ≠ 0, or entries on this disk ≠ total → `bad_zip` (multi-part).
* Prefix offset Δ = EOCD position − (central-directory offset + size). If Δ < 0 → `bad_zip`. Add Δ to
  every offset read from the archive. Δ is 0 for normal archives. A positive Δ means data was put
  in front of the archive, as in self-extracting archives.

### 4.2 Central directory
Starting at the central-directory offset (+ Δ), read the total number of entries. Each entry:
| offset | size | field |
|---|---|---|
| 0 | 4 | signature `50 4B 01 02`, else `bad_zip` |
| 8 | 2 | general-purpose flags: bit 0 encrypted; bit 3 data descriptor (harmless: sizes come from here); bit 11 UTF-8 name |
| 10 | 2 | compression method |
| 16 | 4 | CRC-32 of the uncompressed data |
| 20 | 4 | compressed size |
| 24 | 4 | uncompressed size |
| 28 | 2 | name length *n* |
| 30 | 2 | extra-field length *m* |
| 32 | 2 | comment length *k* |
| 42 | 4 | local-header offset (+ Δ) |
| 46 | *n* | name |

The next entry follows at +46 + *n* + *m* + *k*. Name handling: replace `\` by `/`, and strip any
leading `/` or `./`. A name ending in `/` is a folder entry. Skip it. Names are compared
case-insensitively (ASCII). Non-ASCII bytes only need to compare equal to themselves. If two entries
have the same name, the first wins.

### 4.3 Reading an entry
Only the entries actually needed are read (§5.1).
1. Compressed size, uncompressed size or local-header offset = 0xFFFFFFFF → `zip64`. Flag bit 0 →
   `encrypted`.
2. Local header at its offset: signature `50 4B 03 04` (else `bad_zip`). It has 30 fixed bytes, the
   name length at +26 and the extra length at +28. Use the **local** lengths: they may differ from
   the central ones. The data starts at offset + 30 + name length + extra length. It is
   compressed-size bytes long and must lie inside the file, else `truncated`.
3. Method **0** (stored): the data as is. The compressed size must equal the uncompressed size, else `bad_zip`.
   Method **8** (deflate): a raw RFC 1951 stream, with no zlib or gzip header. The stream must end
   (final block) within the compressed data. The output must be exactly the uncompressed size. Any
   inflate error → `bad_zip`. zlib: `inflateInit2(&s, -15)`.
   Any other method → `unsupported_method` (for example 12 bzip2, 14 LZMA).
4. **CRC-32** of the output must equal the central-directory CRC, else `bad_crc`. CRC-32 is the
   ISO-HDLC / IEEE 802.3 CRC: reflected polynomial 0xEDB88320, initial value 0xFFFFFFFF, final XOR
   0xFFFFFFFF (zlib `crc32()`; `crc32("123456789")` = 0xCBF43926).

The real archive (informational) is a "TorrentZip" with a 22-byte comment. It has a deflated
folder entry `SSOUTN/` and 15 deflated files in `SSOUTN/`.

---------------------------------------------------------------------------------------------------
## 5. Locating the DOS files (`--dos-game`)

This replaces the folder rule of 12 §1.2 ("DIR itself, or its subfolder `SSOUTN`"). It covers that
rule and allows more layouts.

A **DOS folder** is a folder that directly contains both `SSO.EXE` and `MOCKET.DAT`
(case-insensitive).

### 5.1 In a ZIP archive
Look at every file entry named `…/SSO.EXE` or `SSO.EXE` (at any depth). Its folder qualifies if an
entry `<same folder>/MOCKET.DAT` exists. Choose the qualifying folder with the **fewest path
components** (the archive top = 0). Ties go to the first one in byte order of the upper-cased
folder path. If no folder qualifies → `dos_files_not_found`. Read the two entries (§4.3). Their
errors are the errors of the DOS source. The other entries are never decompressed.

### 5.2 In a folder
Check the given folder (depth 0), then its subfolders (depth 1), then theirs (depth 2), stopping at
the first depth where any folder qualifies. Within a depth, take the first in byte order of the
upper-cased relative path. Nothing qualifies by depth 2 → `dos_files_not_found`.

### 5.3 Use
The two files are read once when the DOS files are loaded (12 §1.3: at start-up and whenever the
folder or a DOS style changes) and kept in memory. Validation (12 §2.6) runs on those bytes. A
container error is reported like a failed check: the style falls back to `windows` and a
`DOS audio unavailable: <reason>` line goes to stderr (12 §1.3, §7.2 below).

---------------------------------------------------------------------------------------------------
## 6. Writes: the save directory

The save directory is unchanged: `--save-dir`, otherwise the per-user data directory (README §3; since
v1.26 that is `<base>/saves`, 14 §1.2);
in `--conform play`, `--save-dir` or `DIR2/save` (README §3.1). It is the **only** place the engine
writes to, for any kind of data source.

### 6.1 Player database `SSOWINCD.DAT` (copy on first write)
* **Existing file:** if the save directory holds `SSOWINCD.DAT` (case-insensitive), use it as now
  (03 §4, README §3.1).
* **No file yet:** the initial contents are the game folder's `SSOWINCD.DAT` from the data source
  (image or folder) **if it is exactly 18,140 bytes and starts with `55 53 52 1A`**. Otherwise the
  initial contents are a fresh database (03 §4.1). The file in the save directory is created at the
  **first write** (any write point of 03 §4.2) and holds all 18,140 bytes. Until then nothing is
  written anywhere.
* The data source's copy is never written. This also applies when `--data` is a writable folder.

The CD's blank template equals a fresh database except for the never-used 100th header list entry,
so with the real CD this rule changes nothing visible. With a folder from an existing installation,
its players carry over to the save directory.

### 6.2 `SSOWINCD.INI`
* **Reading:** the save directory's `SSOWINCD.INI` if it exists, otherwise the game folder's
  `SSOWINCD.INI` from the data source, otherwise none (all keys at their defaults, 03 §1.1 step 6).
* **Writing** (the only write of the original is `wavemix=0`, 03 §1.1 step 6; a port may never
  write): write the save directory's `SSOWINCD.INI`. If it does not exist yet, start from the full text
  of the INI that was read and change only the key. Write rules as in 12 §6.5 (keep other lines,
  temporary file + rename).

### 6.3 `outnumbered.ini`
Unchanged (12 §6.5): in the save directory. `DosGameDir` may hold a folder **or a `.zip` path**. A
non-empty `DosGameDir` takes precedence over DOS files installed by chapter 14 (14 §2.3).

---------------------------------------------------------------------------------------------------
## 7. Errors and messages

### 7.1 `--data` (interactive run)
The run stops before the window opens. The message goes to stderr, with exit status 1. As an
addition, the dialog-129 message ("The OutNumbered! CD could not be found…", 03 §1.1 step 7) may be
shown. Messages name the path. The wording below is recommended. The codes are fixed (they appear in
`list-files`, §8.1).

| code | when | message |
|---|---|---|
| `not_found` | PATH (or a cue's FILE) does not exist | `engine: --data '<P>': no such file or folder.` |
| `unrecognised` | §1.1 step 4 | `engine: '<P>' is not a folder, an ISO 9660 CD image (.iso), a raw CD image (.bin/.img, 2352-byte sectors) or a .cue sheet.` |
| `bad_cue` | §2.9 | `engine: cannot use the cue sheet '<P>': <reason>.` |
| `truncated` | §2.1 | `engine: the CD image '<P>' is incomplete (it ends before sector <n>). Copy the image again.` |
| `bad_image` | §2.1–2.7 | `engine: the CD image '<P>' is damaged or uses an unsupported ISO 9660 feature: <reason>.` |
| `no_game_folder` | §3.1 | `engine: the OutNumbered! CD files were not found in '<P>' (no SSOWINCD.EXE at its top or in a top-level folder).` |
| warning | §3.1 step 2 | `engine: several OutNumbered! folders in '<P>' (<A>, <B>, …); using <A>.` (the run continues) |

Missing individual game files keep their existing message (03 §1.1 step 7).

### 7.2 DOS source (`--dos-game`, `DosGameDir`, Audio Options)
The reason text of `DOS audio unavailable: <reason>` (12 §1.3) and of the status line (12 §6.3):

| code | reason text |
|---|---|
| `not_found` | `<P> does not exist` |
| `unrecognised` | `<P> is not a folder or a .zip archive` |
| `dos_files_not_found` | `SSO.EXE and MOCKET.DAT not found in <P>` |
| `truncated` | `<P>: the zip archive is incomplete` |
| `bad_zip` | `<P>: damaged zip archive (<detail>)` |
| `zip64` | `<P>: ZIP64 archives are not supported` |
| `encrypted` | `<P>: <entry> is encrypted` |
| `unsupported_method` | `<P>: <entry> uses an unsupported compression method (<n>)` |
| `bad_crc` | `<P>: CRC error in <entry>` |

**Audio Options (12 §6.3), amended:**
* The folder field 320 accepts a folder **or a `.zip` file** path. It is checked with §1.2 / §5 and
  then 12 §2.6, at the same moments as before.
* The static text above it becomes `DOS game folder or .zip file:`.
* Status line 321: `not_found` and `dos_files_not_found` → `DOS files not found in:` + path. The
  other codes of this table → `DOS files not usable:` + the reason. Success → `DOS files found:` +
  path. When the DOS folder is not the top of the source, the folder inside follows in brackets:
  `…\outnumbered_dos.zip [SSOUTN]`.
* OK stores the path exactly as typed in `DosGameDir`.

---------------------------------------------------------------------------------------------------
## 8. Conformance

### 8.1 New command `list-files`
```
engine --conform list-files PATH [--role data|dos] --out files.json      # default role: data
```
It lists the logical files the engine sees for PATH used as `--data` (role `data`, §3.2) or as
`--dos-game` (role `dos`: exactly `MOCKET.DAT` and `SSO.EXE`, §5). The engine reads **every**
listed file completely, so truncation and CRC errors show up. Output:
```
{"ok": true, "role": "data", "source": "iso2048", "folder": "SSOWINCD", "folders_found": 1,
 "files": [{"name": "MIDI/B00.MID", "size": 35, "crc32": "1a2b3c4d"}, ...]}
```
* `source`: `folder`, `iso2048`, `raw2352`, `cue+iso2048`, `cue+raw2352` or `zip`.
* `folder`: the chosen game or DOS folder inside the source, upper case, `/`-separated; `""` = the
  top.
* `folders_found`: the number of qualifying folders at the chosen level (§3.1 step 1 or 2, §5.1
  depth, §5.2 depth); greater than 1 means a tie was broken.
* `files`: logical names in **upper case**, sorted by byte order. `size` in bytes. `crc32` as 8
  lower-case hex digits (§4.3 step 4).
* On error: `{"ok": false, "role": …, "error": "<code of §7>", "message": "<free text>"}` and exit
  status 2. Only `error` is compared.

### 8.2 Existing commands
* Every `--data DIR` command of README §3.1 (and `play`) accepts any data source of §1.1. The output
  must equal the output for the extracted folder. File names inside outputs are compared
  case-insensitively.
* `--conform dos-check DIR` and the `--dos-game DIR` commands of 12 §7.1 accept a zip (§1.2).
* `play` follows §6.1. With an empty save directory and a data source whose `SSOWINCD.DAT` holds
  players, `signin NAME` finds those players.

### 8.3 Fixtures (`fixtures/containers/`, built by the dirty side's `make_container_fixtures.py`)
All files are synthetic: placeholder text files, tiny generated MIDI files, the synthetic
`FIXTURE.DAT` (as `SSO1.DAT`), `save_SSOWINCD.DAT` (as `SSOWINCD.DAT`, three players) and the
synthetic DOS files of `fixtures/dos/game` (and `game_bad`). The placeholders are **not** valid NE
files. Use the images to compare outputs with `folder_data/` (§8.2), not to run the game.
`expected.json` gives the `list-files` result of every case.

| case | file | tests |
|---|---|---|
| iso_plain | `data_plain.iso` | primary volume only; game folder `SSOWINCD/` next to root files and a folder without the game; `MIDI/` with 64 files, so that directory spans 2 sectors (zero padding at the sector end); `README.;1` → `README`; a 6000-byte file over 3 sectors |
| iso_joliet | `data_joliet.iso` | primary + Joliet + Rock Ridge; mixed-case source names (`SsoWinCD/ssowincd.exe`, `MiDi/e00.mid`); same result through either tree |
| raw_mode1 / raw_mode2 | `data_raw_mode1.bin`, `data_raw_mode2.bin` | `data_plain.iso` in 2352-byte MODE1 and MODE2-form-1 sectors, EDC/ECC zero |
| cue (optional) | `data_raw_mode1.cue` | names `DATA_RAW_MODE1.BIN` (other letter case) |
| iso_root | `data_root.iso` | game files at the image root; a first-level `BACKUP/SSOWINCD.EXE` does not count as a game folder (it is listed as a file of the root game folder) |
| iso_two_folders | `data_two_folders.iso` | `ALPHA/` and `BETA/` both qualify → `ALPHA`, `folders_found` 2, warning |
| iso_no_game | `data_no_game.iso` | `SSOWINCD.EXE` only at depth 2 → `no_game_folder` |
| iso_truncated | `data_truncated.iso` | image cut one sector into `SPEECH.DAT` → `truncated` |
| not_image | `data_not_image.iso` | text file → `unrecognised` |
| folder | `folder_data/` | the same tree as `data_plain.iso`, as a folder |
| zip_deflate | `dos_deflate.zip` | deflate, folder entry `SSOUTN/`, an extra file, an archive comment |
| zip_stored | `dos_stored.zip` | stored, lower-case names `ssoutn/sso.exe` |
| zip_nested | `dos_nested.zip` | `OutNumbered/SSOUTN/` wins over the deeper `…/BACKUP/` (holding the `game_bad` files) and over `Other/` (SSO.EXE without MOCKET.DAT) |
| zip_no_files | `dos_no_files.zip` | → `dos_files_not_found` |
| zip_bad_crc | `dos_bad_crc.zip` | one stored byte of `MOCKET.DAT` changed → `bad_crc` |
| zip_method | `dos_bzip2.zip` | method 12 → `unsupported_method` |
| zip_truncated | `dos_truncated.zip` | first half of `dos_deflate.zip` → `truncated` |
| dos_folder_depth2 | `dos_folder/` | DOS files at `Games/SSOUTN/` (depth 2) |

Not covered by fixtures: multi-extent files (needs files ≥ 4 GiB), interleaved files, ZIP64,
prefixed (self-extracting) zips.

---------------------------------------------------------------------------------------------------
## 9. Decisions and open points

| # | point | decision |
|---|---|---|
| D1 | `.zip` as `--data` | not required. It would be cheap with §4 and the §3 rule, but it is not specified or tested |
| D2 | Joliet | optional. The primary tree is authoritative. The real CD has no Joliet descriptor |
| D3 | DOS folder search (§5) replaces the "`SSOUTN` subfolder" rule of 12 §1.2 | it covers that rule. With several folders at the same depth, name order decides, not the name `SSOUTN` |
| D4 | Template `SSOWINCD.DAT` from the data source (§6.1) | **required**. An engine that always created a fresh database must now copy a valid template. For the real CD there is no visible difference |
| D5 | `CDDrive` / `MIDIPath` ignored (§3.2) | required for container sources; recommended for folders |
| D6 | Unsupported image formats (§1.1) | error `unrecognised`; convert to `.iso` or `.bin`/`.cue` |
