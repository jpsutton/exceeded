# 12 — Optional DOS audio (1990 MS-DOS music and sound effects)

This chapter describes a **non-original, optional feature** of the reimplementation. The engine can
play the music and the sound effects of the 1990 MS-DOS release of *OutNumbered!* in place of the
Windows CD's MIDI music and SND.DAT effects. The DOS sound is produced **live** by an exact model of
the DOS game's music driver. That model reads the user's own DOS game files at run time, either for
the Tandy 1000 / PCjr sound chip (3 square voices plus noise) or for the plain PC speaker. Nothing
from the DOS game ships with the engine.

What the DOS program did is described as behaviour: formats, timing and arithmetic. The sources
are marked: **H** = read directly from the DOS program, **M** = strong inference, **L** = guess.
The engine options, the Audio Options screen (§6) and the mapping of Windows events to DOS songs
(§5) are design decisions of this project. They are marked **[enh]**.

Byte order is little-endian throughout. "Word" = 16-bit unsigned unless stated. Hex values are
written 0x…; offsets are byte offsets.

---------------------------------------------------------------------------------------------------
## 1. Feature overview and options [enh]

### 1.1 What it replaces
Two independent **audio styles** can be chosen, one for music and one for sound effects. Each is
`windows | tandy | speaker`:

| style | music (the 8 MIDI slots of 03 §5.2) | sound effects (SND.DAT WAVEs of 03 §5.3, 04 §14.3) |
|---|---|---|
| `windows` | original behaviour (MIDI) | original behaviour (WAVE mixing) |
| `tandy` | DOS songs, Tandy/PCjr chip model (§4.1) | DOS effect songs, Tandy model |
| `speaker` | DOS songs, PC-speaker model (§4.2) | DOS effect songs, PC-speaker model |

* **Speech** (SPEECH.DAT, intro WAVEs in SSO3) is never replaced. It follows the Voice option as
  before. The other WAVEs that are not in the SND.DAT effect table also stay Windows WAVEs.
* The existing player options **Music / Sound Effects / Voice** (03 §5.1) still switch their
  categories on and off whatever the style (§5.7).
* Style `windows` for both is the default and is byte-for-byte the original engine behaviour.
* The DOS files are original game files (README §1, rule 1). The clean side develops against
  `fixtures/dos/` only. The dirty side runs the engine on real DOS files.

### 1.2 Command line
| option | effect |
|---|---|
| `--dos-audio=windows\|tandy\|speaker` | sets both styles |
| `--music-style=windows\|tandy\|speaker` | music style only (overrides `--dos-audio` for music) |
| `--sfx-style=windows\|tandy\|speaker` | effects style only |
| `--dos-game DIR` | folder of the user's DOS installation (holds `SSO.EXE` and `MOCKET.DAT`) |

Every option takes its value in either form, `--name=value` or `--name value`. Style values are
case-insensitive. An unknown style value, or an option given without a value, is an error: print
a message to stderr and exit with status 1.

Precedence for each setting: **command line > `outnumbered.ini` (§6.5) > default** (`windows`,
`windows`, no folder). File names inside DIR are matched case-insensitively. If DIR itself does not
hold the two files but a subfolder `SSOUTN` (the DOS installer's folder name) does, use that.
DIR may also be a `.zip` archive, and the folder search is extended; see chapter 13 §5 (it replaces
this rule) and 13 §7.2 for the Audio Options field.

### 1.3 Loading and fallback
At start-up, and again whenever the folder or a DOS style changes (§6), the engine loads and checks
the DOS files (§2.6). If a DOS style is requested and the files are missing or fail a check:
* that style falls back to `windows` for this run, and the INI is not changed;
* one line goes to stderr: `DOS audio unavailable: <reason>`;
* the Audio Options screen shows the reason (§6.3).
The game never stops because of DOS audio.

---------------------------------------------------------------------------------------------------
## 2. File formats

### 2.1 SSO.EXE — MS-DOS MZ executable (H; public format)
Header, 14 words at offset 0: `e_magic` ("MZ"), `e_cblp` (bytes in last page), `e_cp` (pages of
512), `e_crlc`, `e_cparhdr` (header size in 16-byte paragraphs), `e_minalloc`, `e_maxalloc`, `e_ss`,
`e_sp`, `e_csum`, `e_ip`, `e_cs`, `e_lfarlc`, `e_ovno`.
* File image size = `e_cp × 512 − (e_cblp ? 512 − e_cblp : 0)`.
* **Load image** = bytes `[e_cparhdr × 16, image size)` of the file.
* SSO.EXE is compressed with Microsoft **EXEPACK**. The engine unpacks it (§2.2) and reads data
  tables from the unpacked image. Nothing is executed. Relocations are not needed (the tables hold
  no segment values) and are ignored.

### 2.2 EXEPACK decompression (H; public format)
The EXEPACK header sits at **load-image offset `e_cs × 16`**. It has two variants:

| variant | layout (words) | signature |
|---|---|---|
| 16-byte | real_IP, real_CS, mem_start, exepack_size, real_SP, real_SS, dest_len | "RB" at header + 14 |
| 18-byte | the same 7 words, then **skip_len** | "RB" at header + 16 |

Check header + 16 first, then header + 14. Neither "RB" → not EXEPACK (error). In the 16-byte
variant skip_len = 1. dest_len is the unpacked size in paragraphs. The original SSO.EXE uses the
18-byte variant with skip_len = 1.

```
src  = load_image[0 : e_cs*16 − (skip_len−1)*16]          # compressed area
out  = buffer of dest_len*16 bytes; out[0:len(src)] = src  # (error if src is longer)
s    = len(src) − 1
skip at most 16 trailing 0xFF bytes:  while src[s] == 0xFF (≤ 16 times): s −= 1
d    = len(out)
loop:
    cmd = src[s];  len = src[s−1]·256 + src[s−2];  s −= 3
    if cmd & 0xFE == 0xB0:            # fill
        v = src[s]; s −= 1
        repeat len times: d −= 1; out[d] = v
    elif cmd & 0xFE == 0xB2:          # copy (backwards)
        repeat len times: d −= 1; out[d] = src[s]; s −= 1
    else: error "bad EXEPACK command"
    if cmd & 1: stop                  # last command
result = out    # out[0 : d] keeps the uncompressed prefix copied at the start
```
Any index that leaves its buffer is an error. Read forwards, the compressed area is the
uncompressed prefix followed by blocks `[payload][len lo][len hi][cmd]`, where the payload is one
fill byte or `len` literal bytes. The first block in file order is decoded last and carries bit 0.
The original unpacks to 105,040 bytes (dest_len 0x19A5). This size is informational and is not a
check.

### 2.3 Driver tables in the load image (H)
All tables live in the program's **data segment**, which starts at **load-image offset 0x13F30**.
Below, `DS:x` means unpacked-image offset `0x13F30 + x`.

| data | location | content |
|---|---|---|
| PC-speaker divisor table | DS:0x44D2, 12 words | PIT divisors of notes 1–12 (C … B) at octave 0 |
| Tandy divisor table | DS:0x44EA, 12 words | SN76489 divisors of notes 1–12 at octave 1 |
| E-event base pointers | DS:0x4502, 2 words | DS offsets of the two tables above (device 0 = speaker, device 1 = Tandy) |
| song priority table | DS:0x4506, one signed word per song | **52 entries are read** (songs 0–51). The table proper has 51 words, so song 51 reads the next word, which is the first envelope pointer (a large positive value). Keep this: song 51 outranks everything (§3.3) |
| envelope pointer table | DS:0x456C, 36 words | DS offsets of the 36 volume envelopes |
| envelopes | at the DS offsets above | byte lists of attenuation values 0–15, each ended by 0xFF. Envelopes may share or overlap bytes |

The two divisor tables are standard equal-temperament constants. The engine uses them as a
**signature**:
```
speaker: 8E88 8689 7EFC 77DB 7121 6AC8 64C9 5F21 59CB 54C0 4FFF 4B81
tandy:   06B8 0658 05FD 05A7 0555 0509 04C0 047C 043C 03FF 03C5 038F
```
The Tandy E-event constant 0xD70 (§3.9) is twice the Tandy table's first entry. It is built into
the driver, not read from the file. The speaker E-event base is read through the pointer for
device 0, which gives speaker table[0] = 0x8E88.

**Locating the tables.** First test data segment 0x13F30. If the checks of §2.6 (items 3–6) fail
there, scan the image for the 48-byte sequence "speaker table immediately followed by the Tandy
table" (the two are adjacent). Each hit gives a candidate data segment = hit − 0x44D2; test it the
same way. This tolerates other builds whose data segment moved but whose driver data is unchanged
(M: no other build has been seen).

### 2.4 MOCKET.DAT — resource container (H)
```
u16 count
count × { u16 id; u32 offset }      # offset from file start
```
The last entry is an end marker: id 0xFFFE with offset = file size. The entries' offsets increase.
A block's extent runs from its offset to the next larger offset in the directory (or the file end).
Two blocks are used:

| id | content |
|---|---|
| 0x15E0 | Tandy/PCjr music block |
| 0x15E1 | PC-speaker music block |

The other entries (graphics, etc.) are ignored. The original file has 10 entries and is 36,140 bytes
(informational).

### 2.5 Music blocks (H)
All offsets in a block are relative to the **block start**.
* **Song table** at block offset 0: Tandy, **4 words per song** (channel 0, 1, 2 = tone voices,
  channel 3 = noise); speaker, **1 word per song**. Each word is the offset of that channel's event
  stream. Song *s* uses the words at `s × 4 × 2` (Tandy) or `s × 2` (speaker).
* The game uses songs **0–51**. The tables are longer (70 Tandy, 75 speaker entries, = first word ÷
  entry size). Entries above 51 are unused and may be 0.
* **Channel event stream**: 2-byte events `b0 b1` (§3.5). A channel whose **first byte is 0xFF** is
  unused by that song.
* The Tandy and speaker arrangements are different scores. They share song numbers and meanings, but
  their lengths and parts differ (the speaker parts are one-voice re-arrangements).

### 2.6 Validation (what "the DOS files are valid" means) [enh, built on H facts]
1. `SSO.EXE` and `MOCKET.DAT` exist (case-insensitive).
2. SSO.EXE is MZ, has an EXEPACK header with "RB", and unpacks without error (§2.2).
3. The two divisor tables match the signature (§2.3).
4. The E-event base pointer words are 0x44D2 and 0x44EA.
5. All 36 envelope pointers point inside the image, and each envelope reaches 0xFF within 1024 bytes.
6. Priority words for songs 0–50 are in 0..255.
7. MOCKET.DAT parses (§2.4). Blocks 0x15E0 and 0x15E1 exist inside the file.
8. For each block: first word w0 is a multiple of the entry size (8 Tandy, 2 speaker) and w0 ÷ entry
   size ≥ 52. For songs 0–51, each channel offset is inside the block, and walking that channel's
   events (2 bytes at a time) reaches an end event (low nibble 0xF) or a first byte 0xFF before the
   block ends.
Report the first failed check as the reason.

---------------------------------------------------------------------------------------------------
## 3. The music driver (H unless marked)

There is one driver for both devices. It plays one **song** at a time. A song has 4 channels
(Tandy) or 1 channel (speaker), and every channel runs its own event stream.

### 3.1 Timing
The driver runs on a fixed **tick of 1193182 / 5041 Hz ≈ 236.69 Hz** (4.2248 ms). All durations,
the gate and the envelopes count ticks. (The DOS game's own timer is the same tick; one DOS hall
frame = 24 ticks.)

### 3.2 State
Global (one driver): `state` (IDLE / START / RUNNING), `current` song (or NONE), `base` (tempo base),
`adj` (tempo adjustment, 16-bit), `loop_flag`, `sound_on`, `music_off`.

Per channel: `ptr` (event pointer), `loop_ptr` (the song's start pointer), `active`, `cnt`
(remaining ticks, 16-bit), `d3` (length of E events), `nogate`, `gate`, `rest`, `tie`, `newnote`,
`env_sel` (start of the selected envelope), `env_pos` (read position), `env_done`, `sus` (sustain
attenuation).

Output state (the "chip"): Tandy, per channel, a tone divisor or noise control plus an attenuation
0–15 (15 = silent); speaker, a PIT divisor and the speaker gate on/off. The chip state persists
until the driver writes it again (§3.14).

### 3.3 Requests (made by the game)
```
play(song):
    if not sound_on: ignore
    if current != NONE and prio[current] >= prio[song]: ignore        # signed 16-bit compare
    if song < 6 and music_off: ignore
    current = song
    for each channel c: ptr[c] = loop_ptr[c] = block_start + word(song table entry for c)
    state = START                     # nothing is written to the chip here

force(song):  stop(); play(song)      # "restart": ignores the priority of the playing song

stop():
    state = IDLE; current = NONE
    for each channel: silence(channel)

set_loop(flag): loop_flag = flag
```
A new song therefore replaces the playing one only if its priority is **strictly higher**. A finished
or stopped song leaves `current = NONE`, so any request is then accepted. A request takes effect at
the next tick. (The DOS game has a second, never-used driver context for overlay effects. It is not
part of this spec.)

### 3.4 Per-tick order
```
tick():
    if state == IDLE: return
    if state == START: init()
    step()
    while state == IDLE and loop_flag:          # song ended this tick and loops
        for each channel: ptr = loop_ptr
        init(); step()                          # same tick
init():
    state = RUNNING; adj = 0
    for each channel c:
        active[c] = (byte at ptr[c] != 0xFF)
        if active[c]: cnt=0; d3=1; nogate=0; rest=0; tie=0; gate=6
        # env_sel, env_pos, env_done, sus, newnote, and the global base are NOT reset (§3.14)
step():
    for c in channels (0..3 or 0):
        if not active[c]: continue
        if cnt[c] == 0: fetch(c)                 # may set active[c] = 0
        if not active[c]: continue
        cnt[c] = (cnt[c] − 1) & 0xFFFF
        speaker only: if nogate[c] != 1 and cnt[c] == gate[c] − 1: silence(c)     # §3.12
    if no channel is active:                     # end of song
        state = IDLE; current = NONE
        silence every channel                    # (not listed in traces, §7.3)
    elif Tandy: volume_pass()                    # §3.11
```
A looped song keeps `current = NONE` after its first pass. Any request then replaces it.
Do not loop a song in which no channel ever waits a tick (the original would hang). The engine
stops it instead.

### 3.5 Event encoding
Each event is 2 bytes `b0 b1`; `n = b0 & 0x0F`, `o = b0 >> 4`.

| n | event | b1 |
|---|---|---|
| 0 | rest | duration byte |
| 1–12 | note C, C♯ … B; o = octave | duration byte |
| 0xD | command `o` (D0–D6; o ≥ 7: no effect) | argument |
| 0xE | free-pitch "E" event; o = octave | pitch parameter (0 = silent) |
| 0xF | end of channel | ignored |

**Duration byte:** bits 0–2 = shift `k`; bit 3 = dotted; bit 4 = tie; bits 5–7 unused.

### 3.6 Fetch
```
fetch(c):
  loop:
    (b0, b1) = event at ptr[c]
    n == 0:    silence(c); duration(c, b1); rest = 1
    n == 0xD:  command(c, o, b1)                      # cnt stays 0 → keep fetching
    n == 0xE:  e_event(c, o, b1)                      # sets cnt = d3
    n == 0xF:  active = 0; silence(c); return         # ptr not advanced
    else:      note(c, n, o); duration(c, b1); rest = 0
    ptr += 2
    if cnt != 0: return                               # zero-length events fall through
```

### 3.7 Durations and ties
```
duration(c, b1):
    len = ((base + adj) & 0xFFFF) >> (b1 & 7)
    if b1 & 8: len = (len + (len >> 1)) & 0xFFFF       # dotted
    if b1 & 0x10:                                     # tie bit
        if tie == 3: (unchanged)
        elif tie == 1: tie = 3
        else: tie += 1
    elif tie != 0: tie += 1
    cnt = len; newnote = 1
```
`base` and `adj` are **global** (shared by all channels). A length of 0 makes the event pass through
in the same tick. The tie counter's effect is in the volume pass (§3.11). In practice, a run of tied
notes is played legato: no envelope restart and no gate cut, and the note after the last tie
continues the envelope.

### 3.8 Commands
| cmd | effect |
|---|---|
| D0 a | a = 0: `nogate = 1`; else `nogate = 0`, `gate = a` (per channel) |
| D1 a / D2 a | a = 0: `adj = 0`. Else `q, r = divmod(base × a, 100)`; if `r > 49`: `q += 1`; D1: `adj = +q`, D2: `adj = −q` (global; 16-bit). D1 slows the music down by a %, D2 speeds it up |
| D3 a | `d3 = a` (per channel): length of the following E events |
| D4 a | `base = a × 4` (global) |
| D5 a | `env_sel = envelope_pointer[a]` (per channel). Only the **selection** changes; the read position moves to it at the next envelope restart. a ≥ 36 never occurs (the original would read past the table; treat it as invalid data) |
| D6 a | `sus = a` (per channel). Values are 0–15 in the game |

### 3.9 Pitch — Tandy (channels 0–2 tone, 3 noise)
```
note(c, n, o):
    if c == 3:  write noise control (n − 1)            # octave ignored; chip uses bits 0–2
    else:
        sh = (o == 0) ? 0 : o − 1
        v  = tandy_table[n−1] >> sh
        if v > 0x3FF: v >>= 1                          # only octaves 0 and 1 of the low notes
        write tone divisor v
e_event(c, o, b1):                                      # Tandy
    if b1 == 0: silence(c); rest = 1
    else:
        x  = ((3·b1) >> 2) + 1
        sh = (o == 0) ? 1 : o
        v  = 0xD70 >> sh
        if sh < 4: x = (x << (4 − sh)) & 0xFFFF
        if sh > 4: x = x >> (sh − 4)
        v = (v − x) & 0xFFFF
        if v (signed) > 0x400: v >>= 1
        write tone divisor v; newnote = 1; rest = 0
    cnt = d3
```
For every b1 and octave the E result stays within 0..0x6B8 before halving, so it is never negative.
The game never uses E events on channel 3. If one occurs, the value goes to the noise register; the
trace still lists it as `f`.

### 3.10 Pitch — PC speaker
```
note(c, n, o):  write PIT divisor speaker_table[n−1] >> o; speaker on    # no "o−1", no halving
e_event(c, o, b1):
    if b1 == 0: silence(c); rest = 1
    else: write PIT divisor ((speaker_table[0] − 128·b1) & 0xFFFF) >> o; speaker on
                                                     # no octave 0 → 1 fix-up, no halving
    cnt = d3
```
`silence(c)` on the speaker = speaker off. The speaker path has **no volume control**: envelopes
(D5), sustain (D6) and ties are stored but have no audible effect.

### 3.11 Tandy volume pass (after every step of a running song)
```
volume_pass():
  for c in 0..3:
    if not active[c]: continue
    if newnote[c]:
        newnote = 0
        if tie >= 2:  (if tie != 3: tie = 0)           # legato: no envelope restart
        else:         env_done = 0; env_pos = env_sel   # restart the envelope
        goto ENV
    if rest == 1:                                write 15; continue
    if tie in (1,3) or nogate == 1:              goto ENV
    if cnt (signed) <= gate:                     write 15; continue    # staccato cut
  ENV:
    if env_done == 0:
        a = byte at env_pos; env_pos += 1
        if a == 0xFF: env_done = 1; continue           # no write this tick
        write a
    elif env_done == 1: write sus
    else: write 15
```
"write a" sets the channel's attenuation (0 = loudest, 15 = off). The original writes on every tick.
A rest that starts a note slot still takes the `newnote` branch on its first tick, so **one
envelope value is written on the first tick of every rest**. That value is overwritten by 15 on the
next tick. This short click is part of the original sound. Keep it.

### 3.12 PC-speaker gate
After the per-tick decrement (§3.4): unless `nogate == 1`, the speaker is switched off on the tick
where `cnt == gate − 1`. Tied notes are cut too, because the tie state is never consulted. The next
note or E event switches the speaker on again.

### 3.13 silence(c)
* Tandy: attenuation 15, then tone divisor 0. On channel 3 the divisor-0 write sets noise control
  0 (and resets the noise generator, §4.1).
* Speaker: speaker off.

### 3.14 What persists between songs (H)
`play` and `init` do **not** write to the chip and do not reset `base`, `env_sel`, `env_pos`,
`env_done`, `sus` or `newnote`. Consequences:
* **Unused channels keep sounding.** When a song that uses fewer channels replaces a playing one
  (e.g. a 1-voice effect interrupting 3-voice music), the channels the new song does not use keep
  their last tone and attenuation. They are silenced only when the new song ends or is stopped (both
  silence every channel), or by a forced restart (`stop` first).
* A song that does not set D4 before its first timed event uses the tempo base left by the previous
  song. A channel that rests before its D5 restarts the previous song's envelope selection. In the
  game this only affects the one-tick rest value of §3.11.
* Initial values for a freshly started driver (and for every conformance trace): `base = 0`,
  `adj = 0`, `env_sel = env_pos = envelope 0`, `env_done = 0`, `sus = 0`, `newnote = 0`, chip
  attenuations all 15, speaker off. (The original's power-on selection points at unrelated memory.
  Envelope 0 is this spec's choice; the game's first song sets its own envelopes.)

---------------------------------------------------------------------------------------------------
## 4. Sound synthesis

The driver produces register states that change only on tick boundaries. Tick *k* starts at
`k × 5041 / 1193182` s of driver time.

### 4.1 Tandy / PCjr: SN76489-type chip (clock 3,579,545 Hz)
* **Tone voices 0–2:** 50 % pulse, f = 3579545 / (32 × N). The chip uses N & 0x3FF, and N = 0
  sounds as 1024. When N changes, the phase continues (no reset).
* **Output polarity (v1.33, H):** every voice is **unipolar**. Its output is 0 or +2·amp, never
  negative: the tone flip-flop (or the noise output bit) gates the attenuated level. 2·amp keeps the
  peak-to-peak level of a sustained tone the same as a ±amp square. The sum of the 4 voices passes
  through an AC coupling, a one-pole **high-pass at 20 Hz** (`x − lowpass(x)` with the one-pole
  low-pass of §4.2). The corner is a recommendation (M); the polarity is not.
  Audible consequence: a voice that is switched on and off every tick or two produces a strong
  118 Hz (tick-rate) thump, because each switch-on is a DC step. In songs made of 1-tick notes
  alternating with 1-tick rests this thump dominates and the melody is heard only faintly. The
  elevator ride songs 21 and 22 are the clearest case: on the real machine they sound like a buzz at
  the tick rate under the hanging music note (§3.14), not like a clean rising or falling sweep. Other
  songs with long on/off runs (Tandy): 11, 16, 18, 19, 27, 35, 36, 45. Short notes in most other songs
  gain a soft click at each start. A ±amp square (the model before v1.33) loses all of this.
* **Attenuation:** amp = A × 10^(−2·a/20) for a = 0..14 (2 dB per step); a = 15 is silent.
* **Noise voice (channel 3):** control value bits 0–2 (higher bits ignored). Bit 2: 1 = white noise,
  0 = periodic. Bits 0–1 = shift rate: 0 → clk/512, 1 → clk/1024, 2 → clk/2048, 3 → the current
  frequency of tone voice 2 (clk/(32·N2)).
  15-bit shift register, reset to 0x4000 on **every write** to the noise register (each `noise`
  and each `off` on channel 3). Each shift: output = bit 0; feedback = bit0 XOR bit1 (white) or
  bit 0 (periodic); register = (register >> 1) | (feedback << 14). Output bit 1 gives +2·amp,
  bit 0 gives 0 (periodic noise = a 15-step pulse).
* Mix the 4 voices by summing them, then apply the AC coupling above.

### 4.2 PC speaker (PIT clock 1,193,182 Hz)
* While the speaker is on: full-amplitude 50 % square, f = 1193182 / N (N = 0 → 65536). Off → 0.
  There are no volume levels. A divisor change keeps the phase.
* **Recommended voicing** (the dirty side's reference; it imitates the small cone in a PC case): first
  band-limit the square, then a one-pole **high-pass at 250 Hz**, then a one-pole **low-pass at
  5000 Hz**. One-pole low-pass: `y[n] = (1−α)·x[n] + α·y[n−1]`, `α = exp(−2π·fc/sr)`. High-pass =
  `x − lowpass(x)`. Make the voicing switchable (off = the raw square).

### 4.3 Output rate, band-limiting and levels
* Render at the engine's mixer rate (44.1 or 48 kHz) with band-limited oscillators (BLEP, or a 4×
  oversampled render plus a low-pass and decimation). The reference renders use 4× oversampling and
  a windowed-sinc decimator (cut-off 0.45 of the output rate).
* **Reference levels** (used by the conformance renders, §7.5): Tandy, each voice A = 0.25 of full
  scale; speaker, square peak 0.5 before voicing.
* **In-game level** (dirty side's measurement): the dirty side rendered the DOS effects with gain
  **2.48 (Tandy)** and **0.53 (speaker)** on top of the reference levels. At those gains the median
  active level of the effect set (RMS over 10 ms windows that are not silent) equals that of the
  Windows WAVEs it replaces, −14.2 dBFS. The balance against speech is therefore the Windows one.
  At this gain the loudest Tandy chords exceed full scale (up to +6.6 dB). Put a limiter (or a
  per-voice gain of 0.25 × 2.48 with soft clipping) on the DOS bus at 0.95 peak, and apply the same
  gain to DOS music. Keep the engine's master/music/effect volume controls on top.
* **Superseded (v1.27):** the in-game gains are now per style **and** bus, from loudness
  measurements: Tandy music −3.5 dB, Tandy effects +4.8 dB, speaker music −11.2 dB, speaker effects
  −6.8 dB, relative to the reference levels, plus optional per-song normalisation and the volume
  sliders. The soft clip (knee 0.6, ceiling 0.95) stays. See **chapter 15** §3–§5.

---------------------------------------------------------------------------------------------------
## 5. Game integration [enh, from the DOS game's behaviour (H/M as marked)]

### 5.1 Voice model
* **Shared voice (default when the music style equals the effects style, both DOS):** one driver
  (§3) plays music *and* effects, exactly like DOS (H): one song at a time; a request replaces the
  playing song only if its priority is strictly higher; a finished or stopped song frees the driver.
  **While an effect plays, the music is silent.** The DOS game re-requests the hall/room song every
  frame, so the music **starts again from its beginning** at the first request after the effect ends
  (at most one DOS frame = 24 ticks later). Channels the effect does not use keep their last music
  note until the effect ends (§3.14). The music has a low priority, so the low-priority effects
  cannot interrupt it and are not heard while it plays (they are ignored, as in DOS).
* **Separate drivers** (the two styles differ, or `DosSharedVoice=0`, §6.5): one driver gets only
  music requests and one gets only effect requests, each on its own device model. The two drivers do
  not interact. When only one category is DOS, the other category uses the Windows path.
* **Timeline.** Each driver has its own clock, **driver time** = game time minus the total time
  that driver has spent paused (§5.6). Without pauses, driver time = game time from program start.
  Driver tick *k* happens at driver time `k × 5041/1193182` s. A request made at game time *t* applies
  before the first tick at or after the corresponding driver time. Scheduled requests (§5.4 repeats,
  delayed stops, background re-requests) are counted in driver ticks. In `--conform play` all of this
  is deterministic.

### 5.2 Music slots → DOS songs (H for where DOS plays each song)
| Windows slot (TEXT id) | DOS song | how DOS mode plays it |
|---|---|---|
| INTRO 1724 | 0 | looped: requested when it becomes current and again whenever the driver is idle while it is current (DOS restarts it in the title loop) |
| LEVEL1 1728 | 1 | looped background (hall, floor 1) |
| LEVEL2 1729 | 2 | looped background (hall, floor 2) |
| ROOM1 1745 | 4 | looped background (rooms of floor 1) |
| ROOM2 1746 | 5 | looped background (rooms of floor 2) |
| ENTER 1717 | 3 | once, waited (room entry, Decide door, floor start) |
| MIDNITE 1735 | 49 | depends on why midnight came (§5.5): **clock ran out in play**: the DOS clock-flash timeline (10 plain requests); **after a wrong decision**: **no request** (song 49 already came from the lose timeline). Waited, see §5.5 |
| VICTORY 1757 | 40 | once, waited (MoM caught; Telly drill set) |

* **Looped background (LEVEL/ROOM):** while the action game runs its frame loop and the slot is
  current, `play(song)` once per action frame, **but only on frames where the player's movement
  state (04 §5.2) is 0, 1 or 2 (standing or walking)**. During a jump or flip (8), a fall (9),
  shoved / knocked down / getting up (26–28) and every other state above 2, no request is made, so
  music that an effect interrupted comes back only once the player stands or walks again (H).
  Use the 24-tick form: request every 24 driver ticks (ticks with k mod 24 = 0) while the
  condition holds, and once when the slot becomes current, so the result does not depend on the
  engine's frame rate. Outside the action-game loop (challenges, drills, decoder, scripted
  sequences) the current song is **not** re-requested. It plays to its end once, as in DOS, where
  these screens do not run the per-frame music call (M).
* **Waited jingles** (ENTER, VICTORY; MIDNITE follows §5.5 instead): `force(song)` (the Windows
  logic stops everything before them; DOS used a plain play, M). The Windows wait ends at the
  **earlier** of two moments:
  * the driver no longer plays that song (end, stop or replacement);
  * the song's length in ticks (from a fresh trace of that song on the active device) has passed in
    **game time** since the request.

  The second limit must apply even if the driver is paused or not advancing, so the game can never
  hang in a wait.
* **Music stop / change** (Windows stops or changes the song): `stop()` if the driver's current song
  is a music song (0–5, 40, 49). Otherwise do nothing.
* Durations differ from the MIDI files (Tandy: INTRO 56.0 s, LEVEL1 16.7, LEVEL2 40.5, ROOM1 17.0,
  ROOM2 19.7, ENTER 2.4, MIDNITE 1.5, VICTORY 10.0; speaker: 12.7, 16.7, 40.5, 17.0, 19.7, 2.2, 1.2,
  10.0 s). Waits use the DOS length.

### 5.3 Sound effects → DOS songs
Every Windows `PlaySFX(id, …)` call, **including** a call the Windows rules would ignore, becomes the
DOS action in the table. The Windows priority and loop flag are not used; the DOS priority table is.
Kinds:
* `once` = `play(song)`;
* `force` = `force(song)`;
* `—` = no sound;
* `keep` = the Windows WAVE (these ids are never played by the game).

"stop" lists the only Windows stops that are passed on (§5.6). All times are driver ticks.

| id | Windows event | DOS song | DOS mode action |
|---|---|---|---|
| 1700 / 1727 / 1706 / 1750 | trick 22 / 23 / 24 / 25 launch + flight | 10 / 11 / 12 / 13 | `play` at launch, then every 72 ticks while the Windows trick sound is active; the Windows stop at hit/removal is **not** passed on (DOS lets the song finish) (H/M). The 72-tick repeats are ignored while the trick song is still playing (equal priority), so each song is normally heard once per 220–300 ticks. The rapid "re-starts" one hears in song 10 are its own 44-tick sweep, repeated five times, not new requests |
| 1701 | bar graph grows | 27 | once |
| 1702 | knock light (ending ESEQ list[0]); invalid-key beep | 26 | ending knock: `force` per cue (§5.5); elsewhere: once |
| 1703, 1747 | tally tick; score change | 37 | once |
| 1704 | Telly crash | 51 | once |
| 1705 | drill question start | 24 | once |
| 1707 / 1708 | energy fill / refusal | 35 / 36 | once at start; stop: yes |
| 1709 | code-piece reveal frame | 48 | once |
| 1710, 1713, 1719, 1721, 1725, 1733, 1756 | reveal end, doors open, get-up ×2, clock chime, LiveWire zapped, UI click | — | no sound (DOS makes none there) |
| 1711 | Telly drill failed | 9 | once |
| 1712 | decoder slot-machine spin | 25 | `set_loop(1)`, `play`; on the Windows stop: `stop()`, `set_loop(0)` |
| 1714 | **elevator, departure** doors close (04 §5.5 step 6) | **22 / 21** | the ride song, `play` once, requested when the departure close animation has **finished** (04 §5.5 step 7 done, before Telly/trick removal is fine). **22** if the departure area is 0 or 4 (going up), **21** if it is 5 or 9 (going down). No stop: it runs through the slide and the arrival to its natural end (§5.6a) (H) |
| 1714 | **elevator, arrival** doors close (04 §5.5 step 15) | — | **no sound**. DOS makes no sound at any elevator door open or close; the only sound of a ride is the ride song above (H) |
| 1718 | equipment list; DfS try-again B | 34 | once |
| 1720, 1723 | bump/thud; splat | 20 | once |
| 1722 | Great Eight | 33 | once |
| 1726 | landing | 38 | once |
| 1727 | see tricks; **intro beam** (intro ASEQ 1700 cue −500); **score-screen Champion extra** (07 Part B §3.3 step 9) | 11; 11; **14** | intro: `force(11)`; the intro's −501 stop: stop: yes. Score-screen Champion extra: `force(14)` (H). Note: the DOS script plays 14 at +210 and 7 at +390 ticks from the start of step 9; the per-cue remapping (this row and 1739) matches that to within one 30-tick picture frame. A timeline from the step-9 start (cues ignored) would match it exactly |
| 1730 | line graph segment | 29 | once |
| 1731 | LiveWire crackle | 18 | once per request (Windows requests it every frame, like DOS) |
| 1732 | LiveWire shock | 19 | once |
| 1734 | map display; DfS try-again A | 28 | once |
| 1736 | **Volume display** (one slider knob moving; 06 §8) | **27** | once per moving slider (DOS plays its slider sound, the bar-graph song, per slider) |
| 1751 | **Spinner display** pointer tick (06 §8) | **30** | the **first** 1751 of a spin: once song 30 (the DOS spinner tune). Every later 1751 of the same spin, and every Windows stop of 1751: nothing. A new spin begins with the next display of the Spinner object |
| 1737 | ending: sparks | 45 | `force` per cue |
| 1738 | MoM zapped | — | ignored: the award and Champion endings use the DOS timelines (§5.5) |
| 1739 | MoM electrified | — / **7** | award and Champion ending: ignored (DOS timelines, §5.5). **Score-screen Champion extra** (07 Part B §3.3 step 9): `force(7)` (H) |
| 1740, 1752 | monitor display static; drill TV static | 23 | once. 1752 only: `stop` 160 ticks later, or earlier at the next 1705, 1711 or 1709 request, if 23 is still the current song |
| 1742 | prize board | 31 | once |
| 1743 | math challenge correct | 7 | once |
| 1744 | drill answer right | 6 | once |
| 1748 / 1749 | bonus panel in / out | 46 / 47 | `play` every 5 ticks for 295 ticks from the request (59 steps); the song repeats whenever it is free |
| 1753 | Telly materialises | 50 | once (lowest priority: hall music blocks it, and the hum requested on the next frame replaces it). DOS also requests 50 on every materialise and dematerialise frame, but always after that frame's hum request, so those requests are always ignored. The engine makes none of them, and nothing is requested at dematerialise |
| 1754 | Telly hum | 17 | once per request (Windows requests it every Telly update frame, like DOS). Song 17 is two short chirp bursts (about 0.17 s each, starting 0.36 s apart) and lasts 124 ticks (Tandy); requested every frame, it restarts within 24 ticks of ending. This repeating chirp is the DOS Telly sound. Also see 1763 |
| 1755 | score-screen rank marker | 26 | once; stop: yes |
| 1758 | VCR tape | 32 | once |
| 1759 | ending: MoM whirlwind (Windows loops it) | 39 | `force` at the request and every 30 ticks while Windows keeps 1759 playing, at most 20 times |
| 1760 | math challenge wrong; wrong-decision (lose) ending | 9; — | challenge: once. Lose ending: **ignored**, the DOS lose timeline plays instead (§5.5) |
| 1761 | drill answer wrong | 8 | once |
| 1762 | lose ending: lightning | — | **ignored**, the DOS lose timeline plays instead (§5.5) |
| 1763 | zap hits an unlit Telly | 42, then 17 | `force(42)` (DOS stops the zap song before the hit sound); then `play(17)` every 24 ticks while Telly is flashing (DOS requests the hum in every Telly state, flashing included, so the hum takes over at once) (H) |
| — (no SFX id) | **zap hits a lit Telly** (04 §14.3 "Zap hit, Telly lit": Windows stops all sounds and the music and starts the drill) | **16** | at the zap result (after the bolt steps), **after** the Windows stop-all: `force(16)` (the big zap). The drill's first static requests (1752 → 23) are then ignored until song 16 ends (84 ticks), as in DOS. The 1752 stop (160 ticks) is counted from the **first** 1752 request of the static, even if that request was ignored (H) |
| 1764 | **hall zap** (energy left) | 14 | `play`; `stop` 60 ticks later if 14 is still current (6 bolt steps × 10 ticks) (H) |
| 1764 | **award zap** (ending ESEQ 1002 seq 3, cue −503 → hall list[0]) | **16** | ignored; song 16 comes from the DOS award timeline (§5.5) (H) |
| 1764 | **score screen, before the statue beam** (07 score step 5) | **16** | once (H) |
| 1765 | zap, empty | 15 | `play`; `stop` 60 ticks later if still current |
| 1768 | Champion ending: explosion | — | ignored (Champion timeline, §5.5) |
| 1769 | Champion ending: TV set beamed away | — | ignored (Champion timeline, §5.5) |
| 1741, 1766, 1767 | not played by the game | — | keep |

Contexts in **bold** use a different song than the same Windows id elsewhere. The engine's DOS mode
must choose the song from the **game event** that requests the sound. The Windows id alone is not
enough. This is where the live mode is more faithful than replacement WAVE files. Unused DOS songs:
41, 44.

Confidence: hall, drill, decoder, bonus and ending ids are matched by identical triggers and
behaviour (H). The math-display ids are matched by display type (H for map / Great Eight / spinner /
volume (sliders) / equipment / prize / bar; M for monitor / VCR / line). The award, Champion and lose endings use the
DOS timelines of §5.5, whose songs and tick offsets come from the DOS animation scripts (H). Only
their start points are matched to the Windows animation (M).

### 5.4 Requests that repeat
"Every N ticks while X" means: a request at the start, then one at each later multiple of N ticks
after it, for as long as X holds. A repeat that the driver ignores (a higher or equal priority song
is playing) is simply lost. This is how DOS makes its "re-triggered" sounds (hum, crackle, tricks,
bonus panel).

### 5.5 Scripted ending sequences (H for DOS, M for the cue match)
The DOS ending animations request songs from the animation script as **forced restarts** (stop +
play; priority ignored). One script time unit there is 30 ticks. The DOS timelines are:
* knock light: song 26 every 60 ticks; MoM sparks: song 45 every 60 ticks; whirlwind: song 39 every
  30 ticks, 20 times;
* award sequence, from the start of the award script: song **16** at +90; song 10 at +300, +360,
  +420; song 50 at +510, +540, +570;
* bonus panel in/out: songs 46/47 requested on each of 59 slide steps of 5 ticks (plain play).

The Windows ending requests the same events from its ESEQ cues at its own times (knock cues about
20 cs apart, electric cues 10–30 cs apart). DOS mode maps **each Windows cue** to the DOS action in
§5.3 (`force` for 1702 / 1737; the 1759 repeat; the 1748/1749 span). The DOS timbre and the forced-restart cut-offs are therefore exact,
and the timing follows the Windows animation. The number of electric blips follows the Windows cues
(6), not the DOS script (3) (M; acceptable). **Exceptions: the award, the Champion branch and the lose ending (below), which play the DOS timelines.** (DOS also requests song 17 at the start of the correct-decision ending; the first forced knock replaces it in the same tick, so it is never heard and the engine makes no request.)

**Award (lifetime < 500,000; 07 Part B §2.2 step 8, first branch): the DOS timeline instead of the
Windows cues.**
1. **Start (T0).** The award animation starts when the engine starts ESEQ 1002 seq 3 (the player's
   zap). At T0 the effect (or shared) driver gets the DOS award timeline, counted in that driver's
   ticks from T0:

   | tick after T0 | request |
   |---|---|
   | +90 | `force(16)` (big zap) |
   | +300, +360, +420 | `force(10)` (MoM zapped) |
   | +510, +540, +570 | `force(50)` (MoM electrified) |

2. **Ignored cues.** While the award runs, the Windows cues 1764, 1738 and 1739 make **no** DOS
   request. That includes the −503 award zap and any repeats derived from them. The award runs from
   T0 until the timeline is finished (below).
3. **Finished** = the +570 request has been made and song 50 is no longer the driver's current
   song (it ended or was replaced).
4. **Holding the victory step.** The DOS timeline (about 631 ticks ≈ 2.7 s including the last
   song 50) is longer than the Windows award clips. Step 9 (victory, `victory.mid` / song 40) starts
   only when both the Windows award clips have ended **and** the DOS timeline is finished. Until then
   the last award frame stays on screen and nothing else changes. Only in DOS effect style; with
   Windows effects the step timing is unchanged.
5. **Interruptions.** If the ending is left early (Esc / exit), cancel the rest of the timeline with
   the other sounds.
6. **Other requests.** Requests not listed in step 2 during the award still go through the
   one-voice rules as usual, e.g. a score change → song 37 (low priority, usually ignored).

The award's DOS timeline is known from the DOS award animation script (H for the songs and the tick
offsets). The start point T0 = "the player's zap begins" is the match between the two animations (M).

**Champion (lifetime ≥ 500,000; 07 Part B §2.2 step 8, second branch): the DOS timeline instead of
the Windows cues.**
1. **Start (T0)** = when the engine starts ESEQ 1002 seq 1 (the player aims). The DOS Champion
   animation is two scripts run back to back; ticks after T0, all requests forced:

   | tick after T0 | request |
   |---|---|
   | +60 | `force(43)` (explosion / big zap) |
   | +500 | `force(22)` |
   | +820, +880, +940, +1000, +1060, +1120 | `force(13)` (six times, 60 ticks apart) |
   | +1450 | `force(14)` (zap) |
   | +1750 | `force(48)` (TV set beamed away) |

2. **Ignored cues.** In the Champion branch the Windows cues 1738, 1739, 1768 and 1769 make no
   DOS request.
3. **Finished** = the +1750 request has been made and song 48 is no longer current (+2070 ticks
   ≈ 8.7 s on both devices).
4. **Victory hold, interruptions, other requests:** as for the award (items 4–6).

The songs and tick offsets come from the DOS Champion animation scripts (H). T0 is matched to the
Windows aim clip, which shows the same aim cels (M).

**MoM wins (wrong decision; 07 Part B §2.3): the DOS timeline instead of the Windows cues.**
1. **Start (T0)** = the start of 07 §2.3 step 2 (lightning clip and player drop). Requests on the
   effect (or shared) driver, ticks after T0:

   | tick after T0 | request |
   |---|---|
   | +0 | `play(17)` |
   | +60 | `force(11)` (lightning) |
   | +180 | `force(20)` (thud: the player lands in the trap) |
   | +300 | `force(49)` (the midnight jingle) |
   | +330 | `play(49)`: always ignored, because 49 is still playing; may be omitted |

2. **Ignored cues.** During this sequence the Windows cues 1762 and 1760 make no DOS request, and
   the MIDNITE of the following midnight sequence (07 §4.1) makes none either.
3. **Finished** = the +300 request has been made and song 49 is no longer current: +647 ticks
   (≈ 2.7 s) on Tandy, +577 on the speaker.
4. **Waits.** Windows waits for SND 1760 in step 4 ("Voice on: wait for 1760 to finish"). That wait,
   which precedes the gloat speech, now ends when the timeline is finished, as the award's victory
   hold does. The step-4 wait of the midnight sequence ("wait until midnite.mid has finished") ends
   at once, because MIDNITE made no request.
5. Interruptions and other requests: as for the award (items 5–6).

**Midnight when the clock runs out in play (07 §4.1, normal play): the DOS clock-flash timeline.**
1. **Start (T0)** = 07 §4.1 step 2 (where `midnite.mid` starts). Requests: `play(49)` at +0, +100,
   +200, …, +900 ticks (10 plain requests). Song 49 lasts 347 ticks on Tandy, so the requests at
   +0, +400 and +800 are accepted: **3 starts, 1.69 s apart**. On the speaker (277 ticks) the
   accepted ones are +0, +300, +600 and +900: **4 starts, 1.27 s apart**.
2. **Wait (step 4).** It ends when the +900 request has been made and song 49 is no longer
   current, at the latest at T0 + 900 + the song's length. The Windows flashes (10 × 200 units
   ≈ 8.4 s) last longer than this timeline (≤ 1247 ticks ≈ 5.3 s), so in practice the wait ends
   as soon as the flashes end.
3. The flash pictures and their timing stay as in 07 §4.1. Only the sound follows DOS.
4. No other sound follows in DOS (no "MoM wins" sequence after a timeout).

### 5.6 Windows stops, speech, dialogs, pause
* `StopSFX(id)`: passed on (`stop()`) only for ids with "stop: yes" in §5.3, and only if the
  driver's current song was started by that id. All other per-id stops are dropped; DOS lets those
  songs finish.
* "Stop all sounds", starting speech, and opening a dialog (03 §1.2) → `stop()` if the current song
  is an effect song. In separate-driver mode this applies to the effect driver.
  **Exception:** the elevator's arrival "stop all sounds" (04 §5.5 step 12) does **not** stop the
  ride song (21 / 22). DOS never stops it; see §5.6a.
* **Pauses.** A DOS driver pauses only where the Windows engine pauses its MIDI music. These are the
  dialog-open pause of 03 §1.2, the inactive window, and the open menu.
  * The music driver (or the shared driver) stops ticking and outputs silence. Resume → it continues
    where it was.
  * A separate effect driver never pauses. Its effects are already stopped when a dialog opens.
  * The **game-clock pause never pauses a DOS driver.** That pause covers the decoder, math
    challenges, the Decide / ending / bonus / score sequences entered from the decoder, and the
    clock pause around hint dialogs. Only the dialog's own music pause (above) applies there.
  * Effects requested while the clock is paused must sound and finish normally.
* Music changes during elevator rides / room entry follow the Windows logic (04 §14). DOS-mode
  requests are generated only from the Windows events.

### 5.6a Elevator ride (H)
The complete DOS sound of one elevator ride (04 §5.5), in order:

| ride phase (04 §5.5 step) | DOS action |
|---|---|
| approach, turn (1–2) | the usual per-frame requests only (hall music, Telly hum) |
| departure "stop all sounds" (3) | as §5.6 (stops a current effect song) |
| departure doors open, 1713 (3–4) | none |
| step in, 1714 (5–6) | none at the request itself |
| departure doors closed (end of 7) | **`play(22)`** if the departure area is 0 or 4 (up), **`play(21)`** if 5 or 9 (down) |
| Telly/trick removed, slide (8–10) | none (the slide makes no per-frame requests) |
| arrival "stop all sounds", doors open 1713 (12–13) | none; the ride song is **not** stopped |
| step out, arrival close 1714 (14–16) | none |
| end (17) | music requests resume as usual |

The ride song is a plain play of priority 5. It replaces the hall music (priority 1) and is never cut.
Hall-music requests that come while it still sounds are ignored by the driver, so the new floor's music
starts from its beginning at the first request after the ride song ends. Lengths: song 21 is 352 ticks
on Tandy and 239 on the speaker; song 22 is 360 / 240. The slide plus the arrival door opening take 276
ticks from the request, so the Tandy ride song still sounds while the arrival doors open and ends around
the step out; the speaker version ends during the step out.

In separate-driver mode (`DosSharedVoice=0`) the ride song plays on the effect driver; the music driver
follows the Windows music logic of 04 §5.5 (music off for the ride, the new floor's music after it).

### 5.7 Options
* Music option off → no music requests. Effects option off → no effect requests. A song already
  playing in that category is stopped (`stop()` if it is current). (DOS had its own sound-off and
  music-off flags, §3.3. The engine keeps them at `sound_on = 1`, `music_off = 0`, and filters
  requests by the Windows options instead.)
* Voice off: unchanged Windows behaviour.

### 5.8 Known differences that remain
* The Windows game's action timing (frame rate, sequence lengths) is kept. Only the sound comes from
  DOS.
* In shared mode the engine reproduces DOS's single voice. Effects silence the music, and
  low-priority effects are lost while music plays. This is intended. Separate-driver mode exists for
  users who prefer continuous music.

---------------------------------------------------------------------------------------------------
## 6. Audio Options (NON-ORIGINAL ENHANCEMENT) [enh]

> **Everything in §6 is new.** None of it exists in the original Windows or DOS game. It reuses the
> original art and dialog look so that it fits in.

### 6.1 The OPTIONS button (replaces CANCEL on the sign-in screen)
The sign-in screen's CANCEL button (SSO3 ASEQ **16003** at (318,331), 73×48; 03 §2.3) becomes
an **OPTIONS** button at the same place. It is **always enabled**. Clicking it plays the click (as
the other buttons do: SFX 1756, which is silent in a DOS effects style), shows the pressed cel and
opens the Audio Options dialog (§6.3).

The art is made at run time from the three CANCEL cels (0 normal, 1 pressed, 2 disabled; cel 2 is
built for completeness). Cel coordinates, palette indices of RRGB 128:
1. Copy the cel (it has no transparent pixels).
2. **Erase the baked-in label:** for y = 16..27 and x = 5..66, pixel(x, y) = original pixel(x,
   y + 12) of the same cel. This copies the plain face texture from rows 28–39 over the label; the
   texture's left-to-right shading and the side bevels line up.
3. **Draw "OPTIONS"** with NFNT **28625** (the "small bold sans"; its cap height of 9 px equals the
   original lettering), pen x = **10**, baseline y = **27** (glyph tops at y 14; 02 §4.1 text
   rendering). First draw the string in the **shadow colour** at offsets (−1,0), (0,−1) and (−1,−1),
   then in the **letter colour** at (0,0). The original lettering has the same dark outline on its
   top and left.

| cel | state | letter colour | shadow colour |
|---|---|---|---|
| 0 | normal | 135 | 42 |
| 1 | pressed | 137 | 42 |
| 2 | disabled | 77 | 67 |

Fit check: the ink of "OPTIONS" in 28625 spans x 11–60 and y 18–26 (with the shadow x 10–60, y
17–26). This is centred on the face (centre 35.5, the same centre as CANCEL's x 8–63). It lies
inside the erased rectangle in all three cels; the pressed cel's face starts one pixel further right
and still clears it. No smaller font is needed.

### 6.2 CANCEL's old function
CANCEL cleared the typed name and removed the provisional new-player entry. It was enabled only when
the list was not empty and a provisional entry existed. **Rule:** at sign-in, **Esc** does what
CANCEL did whenever CANCEL would have been enabled. Otherwise Esc quits the program as before (03
§2.3). Nothing else changes.

### 6.3 The Audio Options dialog
A modal dialog drawn by the engine's own dialog system (03 §1.2: TLC Keycaps, 1 DLU = 1.75 × 2.125 px for this non-original dialog: its layout was designed in those units, so it keeps them even though the original dialogs use 2.0 px horizontally (03 §1.2, v1.28),
centred on the 512×384 game area). It is defined here because there is no resource for it.
Template, Win16 notation of 01 §5:

`DIALOG style 0x90C000C2, rect (0,0,200,122), caption "Audio Options", font TLC Keycaps 11`

| id | class | rect (x,y,cx,cy) DLU | style | text |
|---|---|---|---|---|
| −1 | STATIC | 5,5,90,8 | 0x50000000 | `Music:` |
| 301 | BUTTON radio | 10,15,80,8 | 0x50030004 (group start) | `Windows` |
| 302 | BUTTON radio | 10,25,80,8 | 0x50000004 | `Tandy 1000` |
| 303 | BUTTON radio | 10,35,80,8 | 0x50000004 | `PC speaker` |
| −1 | STATIC | 105,5,90,8 | 0x50000000 | `Sound effects:` |
| 311 | BUTTON radio | 110,15,80,8 | 0x50030004 (group start) | `Windows` |
| 312 | BUTTON radio | 110,25,80,8 | 0x50000004 | `Tandy 1000` |
| 313 | BUTTON radio | 110,35,80,8 | 0x50000004 | `PC speaker` |
| −1 | STATIC | 5,47,190,1 | 0x50000004 | (separator line) |
| −1 | STATIC | 5,52,190,8 | 0x50000000 | `DOS game folder (SSO.EXE, MOCKET.DAT):` |
| 320 | EDIT | 5,62,190,12 | 0x50810080 (border, tab stop, auto-scroll) | current DosGameDir |
| 321 | STATIC | 5,77,190,16 | 0x50000000 | status, 2 lines (below) |
| −1 | STATIC | 5,96,190,1 | 0x50000004 | (separator line) |
| 1 | BUTTON | 5,102,50,15 | 0x50030001 (default) | `&OK` |
| 330 | BUTTON | 75,102,50,15 | 0x50010000 | `&Test` (optional) |
| 2 | BUTTON | 145,102,50,15 | 0x50010000 | `&Cancel` |

* **Status line 321:** `DOS files found:` + path, or `DOS files not found in:` + path, or `DOS files
  not usable:` + the §2.6 reason. An empty field shows `No DOS game folder set.`. Long paths are
  shortened in the middle with "…".
* The folder field is checked (§2.6) when the dialog opens, when it loses focus, and on OK. While the
  check fails, radios 302/303/312/313 are **disabled**. If one of them was selected, that group
  switches to Windows.
* Chapter 14 §9 adds an **Install** button (id 331) next to field 320, which copies the DOS files into
  the per-user installation, and lets the user drop a path onto the dialog.
* **Keyboard:** Tab / Shift+Tab move between controls; the arrow keys move within a radio group;
  Enter = OK (also inside the edit field); Esc = Cancel. Mnemonics O / T / C as underlined.
* **Test** (optional): plays a sample in the selected styles: effect DOS song 7 (Windows: SFX 1743),
  then the first 4 s of LEVEL1 in the selected music style. The sample stops at any key or click
  and when the dialog closes. It does not change the settings.
* The dialog sets the "dialog open" audio state of 03 §1.2 (effects stopped, music paused) like any
  dialog.
* **v1.27:** chapter 15 §6.2 replaces this template (wider dialog with Music / Effects / Speech
  volume sliders and an "Even out volume" check box; Test becomes required).

### 6.4 When choices take effect
* **OK:** validate; store the settings in the INI (§6.5); apply at once. Stop all effects and the
  music. If a music slot is **currently playing or paused** (not merely remembered after a stop or
  after the song ended), restart it from the beginning in the new style. Otherwise start no music.
  Later effects
  use the new style. The settings now replace any command-line overrides for the rest of the run.
  **(v1.29)** This stop-and-restart happens **only** when the music style, the sound-effects style, the
  DOS game folder or DosSharedVoice changed. An OK that changes only the volume sliders or Even out
  volume (chapter 15 §6) stops and restarts nothing: the music keeps playing (paused while the dialog
  is open, resumed when it closes), and the new levels stay applied as they were live.
* **Cancel / Esc / close box:** no change.
* The styles are independent of the per-player Music / Sound Effects / Voice options (§5.7). The
  styles are per installation (INI), not per player.

### 6.5 Settings file `outnumbered.ini`
The original files are never modified (a read-only data folder works). The engine keeps its own file
**`outnumbered.ini` in the save directory** (`--save-dir`, or the default per-user data directory;
in `--conform play`, the `save` directory used for the player database).
```
[Audio]
MusicStyle=windows        ; windows | tandy | speaker
SfxStyle=windows          ; windows | tandy | speaker
DosGameDir=               ; path; empty = not set
DosSharedVoice=1          ; optional; 1 = one shared DOS voice when both styles are the same DOS device
```
* **Parsing:** lines `key=value`; `;` or `#` starts a comment; section and key names are
  case-insensitive; values are trimmed and case-insensitive. A missing file, section or key gives the
  default. An unknown style value gives `windows`. DosSharedVoice: `0`/`no`/`false` = 0, anything
  else = 1.
* **Writing:** only on OK in Audio Options. Rewrite the file and keep any other sections, keys and
  comments. Update or insert the keys of `[Audio]`. Write to a temporary file, then rename it.
* **v1.27:** keys `MusicVolume`, `EffectsVolume`, `SpeechVolume`, `EvenOutVolume` (chapter 15 §6.3).
* **Precedence:** command line > INI > defaults (§1.2). A DOS style whose files fail validation falls
  back to `windows` for the run (§1.3) without rewriting the INI.

### 6.6 Optional menu item
Optional: add **"Audio Options…"** to the Options menus 600 (sign-in), 1100 (game) and 1400 (drill),
after "Voice" and before the next separator, with new ids 610 / 1110 / 1410. It opens the same
dialog. In the game, the dialog's audio state applies, and the game clock pauses as for other dialogs
opened from the menu.

---------------------------------------------------------------------------------------------------
## 7. Conformance interface

### 7.1 New commands
```
engine          --conform dos-unpack EXE                     --out img.bin
engine          --conform dos-check DIR                      --out c.json
engine --dos-game DIR --conform dos-trace SONG  --variant tandy|speaker --ticks N --out t.json
engine --dos-game DIR --conform dos-script FILE --variant tandy|speaker           --out s.json
engine --dos-game DIR --conform dos-render SONG --variant tandy|speaker [--ticks N] [--rate 44100] --out x.wav
```
* `dos-unpack`: the unpacked load image (§2.2). On an error, exit status 2 and the reason on stderr.
* `dos-check`: `{"ok": true, "tables_found": "fixed"|"scan", "data_segment", "unpacked_size",
  "unpacked_sha256", "speaker_divisors", "tandy_divisors", "priorities" (52 values, §2.3),
  "envelope_pointers", "envelopes" (each up to and including 0xFF), "block_tandy": {"size",
  "table_entries"}, "block_speaker": {…}}`, or `{"ok": false, "reason": "…"}` (the reason text is
  free).

### 7.2 dos-trace
A fresh driver (§3.14 initial values), `play(SONG)`, then ticks 0, 1, …, N−1. The run stops early
when the song ends. Output:
```
{"variant": "tandy", "song": S, "ticks": N, "end_tick": T or null,
 "channels": [[[tick, kind, value], ...], ...]}        # 4 channel lists (Tandy) or 1 (speaker)
```
`end_tick` = the tick in which the last channel ended (null if N ticks ran out first).

### 7.3 Event kinds (the trace is the driver's chip-write log)
| kind | value | logged when |
|---|---|---|
| `f` | tone divisor (Tandy, as computed; the chip uses & 0x3FF) or PIT divisor (speaker) | every pitch write |
| `noise` | noise control n − 1 (0–11; the chip uses & 7) | every noise write (Tandy channel 3) |
| `v` | attenuation 0–15 | only when it differs from the channel's current attenuation (initially 15; `off` sets it to 15) |
| `on` | 0 | speaker switched on (after each `f` of a note or E event) |
| `off` | 0 | every `silence(c)`: rest, E 00, end of channel, speaker gate cut, and each channel at `stop()` |

Events are in write order within a tick. The end-of-song silencing of all channels (§3.4) is **not**
logged: every channel has already logged its own `off`.

### 7.4 dos-script
Request scripts test priorities, forced restarts, stop, the loop flag and the persistent chip
state. One command per line, `#` starts a comment:
```
<tick> play <song> | force <song> | stop | loop 0|1 | musicoff 0|1 | soundoff 0|1 | end
```
Commands at tick t are applied in file order **before** tick t runs. The run executes ticks 0 … E−1,
where E is the tick of `end`. Output: `{"variant", "ticks": E, "requests": [[tick, op, args…,
result]], "channels": […]}`. result = `accepted`, `ignored (priority)`, `ignored (music off)`,
`ignored (sound off)` or `done`. The channel lists use the kinds of §7.3 over the whole run, so a
`v` after a song change is relative to the attenuation the previous song left.

### 7.5 dos-render (optional; tolerance test)
Renders `dos-trace`'s register stream with §4 at the reference levels (§4.3), speaker voicing on,
mono 16-bit. Compare with the fixture WAVs:
* length = ⌈T × 5041/1193182 × rate⌉ ± 1 sample, where T = end_tick;
* 10 ms RMS envelope within ±1.5 dB wherever the reference is above −40 dBFS. Leave out every window
  that contains, or is next to, a tick with a sound-on/off change: a note start, an `off`, a speaker
  gate cut, or an attenuation change to or from 15. Below −60 dBFS where the reference is silent;
* the dominant frequency per 50 ms window of tone-only passages within 0.5 %. Windows where two
  voices have nearly equal amplitude may pick either voice; compare those against both voices'
  frequencies.
Noise passages are compared by RMS only (the generator phase is free).

### 7.6 Play mode, events.log and state
* In `--conform play`, extra script command: `audio <music-style> <sfx-style> [DIR]`. It sets the
  styles (and folder) as if they had been chosen in Audio Options and confirmed with OK, including the
  INI write in `DIR2/save`. The dialog itself is reached with `click 360 345` (the OPTIONS button)
  and answered with `press ENTER/ESC` or clicks.
* events.log keeps the existing `SFX/MUSIC/SPEECH/DIALOG` lines (the Windows-level events). In DOS
  mode it adds one line per driver request:
  `<tick> DOS <PLAY|FORCE|STOP|LOOP> <song|flag> <accepted|ignored> [music|sfx]`. The last field names
  the driver in separate-driver mode.
  * `<tick>` is always **game time** in centiseconds, the same time base as the other log lines.
  * A scheduled request executed at driver tick k is stamped with the game time of that tick
    (driver time + total pause so far, rounded down).
  * Stamps never decrease across **all** log lines (`SFX`, `MUSIC`, `SPEECH`, `DIALOG` and `DOS`). Before any line is written, the engine runs the DOS drivers up to the current game time, so pending scheduled requests are logged first with their earlier stamps.
* `state` JSON gains `"audio": {"music_style", "sfx_style", "dos_game_dir", "dos_status":
  "ok"|"missing"|"invalid"|"unset", "shared_voice": bool, "current_song": n or -1}`.
* Audio stays muted in play mode. The DOS drivers still run on the virtual timeline, so the request
  results are deterministic.

### 7.7 Fixtures (`fixtures/dos/`, see its README)
Synthetic game folders (`game/` valid, `game_bad/` with a broken table), EXEPACK unit files in both
header variants and with skip_len 3, expected `dos-check`, `dos-trace` (every event type, command,
tie case, gate case, E branch, noise control, zero-length event, empty song) and `dos-script` results
(priority, force/stop, loop, music/sound off, carry-over), and two reference renders.

---------------------------------------------------------------------------------------------------
## 8. Open points and decisions

| # | point | recommendation |
|---|---|---|
| A1 | Shared single voice (faithful: effects silence the music, which restarts afterwards) vs. continuous music | **shared by default**; `DosSharedVoice=0` for separate drivers |
| A2 | Hanging notes of channels an effect does not use (§3.14) | **keep** (DOS behaviour). Not yet confirmed by ear on real hardware |
| A3 | Re-requesting room music during challenges (§5.2): DOS does not run its per-frame music call there (M) | **no re-request outside the action-game loop** |
| A4 | Waited jingles use `force` (DOS used a plain play where nothing higher was playing) | **force** |
| A5 | Ending cue counts follow the Windows ESEQ (e.g. 6 electric blips instead of DOS's 3) | decided: knocks, sparks and whirlwind follow the Windows cues; the award, the Champion branch and the lose ending play the DOS timelines (§5.5) |
| A6 | VICTORY after a failed Telly drill set: DOS plays song 9 ("No Code", via 1711) and its success tune only after a perfect set; the Windows rule for victory.mid is kept | keep Windows rule (M) |
| A7 | Lose ending: DOS also plays song 20 between the lightning and the midnight jingle; there is no Windows cue to attach it to | resolved: song 20 is part of the lose timeline (§5.5, +180) |
| A8 | Noise generator: the reference renderer resets the generator only when the control value changes; §4.1 (reset on every write) is the chip behaviour | follow §4.1; renders are compared with tolerance |
| A9 | DOS file version: only one DOS build has been seen | the table scan of §2.3 covers moved data segments |
