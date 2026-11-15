# 15 — Audio levels: bus balance, "Even out volume", volume sliders (NON-ORIGINAL ENHANCEMENT)

> **Everything in this chapter is new.** The original game sets no levels at all (§1). This chapter
> defines a fixed balance between music, effects and speech for every audio style (Windows, Tandy,
> PC speaker), an optional per-asset loudness normalisation, three volume sliders, and a conformance
> command. It amends 12 §4.3 (DOS levels), 12 §6.3 (dialog template) and 12 §6.5 (INI keys).
> Tags: [enh] throughout; the measurements in §1–§3 are the dirty side's (H for the original's
> calls, measured values ±0.2 dB).

## 1. Background

### 1.1 What the original did (H)
* **Waves:** a WaveMix session (mono 22 kHz) or a single waveOut stream (03 §5.1). The game never
  calls `waveOutSetVolume` (not imported). Of the WaveMix API it uses only ConfigureInit, Activate,
  OpenWave, OpenChannel, Play, FlushChannel, CloseChannel, FreeWave, CloseSession and Pump. WaveMix
  1.x has no volume parameter in these calls. Every WAVE plays at its authored sample level.
* **MIDI:** MCI sequencer with OPEN / PLAY / SEEK / STOP / PAUSE / CLOSE only. No `MCI_SET`
  audio, no `midiOutSetVolume` (only `midiOutGetNumDevs` / `midiOutGetDevCaps` are imported). The
  MIDI files contain **no CC7, no CC11 and no SysEx**. Note velocities: median 87–108, max 111–127.
  The music level was whatever the user's synth (FM, wavetable, GS module) and the Windows mixer
  made of it. **There is no single "right" music level.**
* **DOS:** the Tandy chip has per-voice attenuation driven by the songs (12 §3.11), the speaker has
  none. There is no master level.

### 1.2 What the engine did until now (measured)
Reference renders (§2.2), integrated loudness (§4.1), per category: min / median / max LUFS.

| style | bus | n | reference | as played by build h35 |
|---|---|---|---|---|
| Windows | effects (SND.DAT + SSO3 WAVEs) | 62 | −30.3 / −13.8 / −3.1 | same (full scale) |
| Windows | speech (SPEECH.DAT) | 68 | −16.8 / −14.1 / −11.5 | same |
| Windows | music (E set, GeneralUser GS) | 8 | −33.2 / −32.6 / −28.7 | ≈ −39 / −38.6 / −34.7 (gain 0.2, (L+R)/4) |
| Tandy | music (songs 0–5, 40, 49) | 8 | −18.3 / −16.5 / −14.1 | ≈ −10.6 / −9.1 / −7.0 (×2.48, soft clip) |
| Tandy | effects (41 songs) | 41 | −31.5 / −20.8 / −13.9 | ≈ −23.8 / −13.3 / −6.5 |
| speaker | music | 8 | −11.3 / −8.8 / −7.2 | ≈ −16.9 / −14.4 / −12.9 (×0.53) |
| speaker | effects | 41 | −19.6 / −9.2 / −6.0 | ≈ −25.2 / −14.9 / −11.7 |

What makes it jarring:
1. **Windows music is ≈ 25 dB below effects and speech.** Every effect jumps out of near-silence.
2. **Style jumps:** Tandy music is ≈ 30 dB louder than Windows music and 4 dB above Tandy effects.
3. **Effects spread 27 dB (Windows) / 18 dB (Tandy).** Loudest Windows WAVEs: 1702 knock / invalid
   key (−3.1), 1737 sparks (−5.2), 1731 LiveWire crackle (−5.5, requested every frame), 1762
   lightning, 1708 energy refusal, 1760 wrong answer (≈ −7.5). Quietest used ones: 1721 get-up
   (−21.0), 1754 Telly hum (−20.3), 1703 tally tick, 1756 UI click (≈ −19.4). Tandy: songs 24, 25,
   17 (≈ −31) vs 6, 7, 28 (≈ −14).
4. The final mix clips hard when several full-scale WAVEs overlap.

Speech is consistent (5 dB spread) and sits at the effects' median. It is the anchor.

## 2. Model

### 2.1 Buses and targets
Three buses. Target integrated loudness (§4.1) of one asset played alone with its slider at 100:

| bus | carries | target T |
|---|---|---|
| music | MIDI songs; DOS music songs 0–5, 40, 49 | **−20 LUFS** |
| effects | SND.DAT / SSO3 WAVEs; DOS effect songs (all others) | **−16 LUFS** |
| speech | SPEECH.DAT WAVEs (every style) | **−15 LUFS** |

Music sits 4 dB under effects (background); speech 1 dB over effects (it stops all waves anyway).

### 2.2 Reference renders (what "loudness of an asset" is measured on)
Full scale = 1.0, mono.
* **WAVE:** 8-bit unsigned `(u − 128) / 128` (16-bit: `s / 32768`; stereo: mean of L and R), at the
  file's own sample rate. No resampling.
* **MIDI:** FluidSynth 2.x with the SoundFont in use (bundled: GeneralUser GS v2.0.3), sample rate =
  mixer rate, `synth.gain` 0.2, default reverb and chorus, mono = **(L + R) / 2**. One pass of the
  file from the start to its last event (no tail), at most 120 s. Looped songs: one pass.
* **DOS song:** 12 §7.5 `dos-render` of the song from a reset driver at the **reference levels** of
  12 §4.3 (Tandy voice A = 0.25; speaker square 0.5, voicing on), one pass, at most 120 s.

The engine's music bus output is the reference render times the gain. (Build h35 mixes FluidSynth as
(L+R)/4, which is 6 dB below the reference; the gains below are relative to the reference.)

## 3. Default balance: style gains (Even out off)
Each (style, bus) has a fixed gain **G** = T − (median reference loudness of the category in the
dirty side's measurement), rounded to 0.1 dB:

| style | music | effects | speech |
|---|---|---|---|
| Windows | **+12.6 dB** (×4.27) | **−2.2 dB** (×0.776) | **−0.9 dB** (×0.902) |
| Tandy | **−3.5 dB** (×0.668) | **+4.8 dB** (×1.738) | — (speech is always Windows) |
| PC speaker | **−11.2 dB** (×0.275) | **−6.8 dB** (×0.457) | — |

These replace the DOS in-game gains ×2.48 / ×0.53 of 12 §4.3. With "Even out" off, every asset of
a category gets G. The category medians then hit the targets (±0.1 dB); the spread inside a category
stays as authored.

## 4. "Even out volume" (default ON): per-asset normalisation

### 4.1 Loudness measure L (ITU-R BS.1770-4 integrated, mono)
Input: reference render x[n] (§2.2) at rate fs. Use double precision.
1. **K-weighting:** two biquads in series, zero initial state, direct form
   `y = b0·x + b1·x₁ + b2·x₂ − a1·y₁ − a2·y₂`. For any fs:
   * Stage 1 (shelf): G = 3.999843853973347, Q = 0.7071752369554196, fc = 1681.974450955533;
     K = tan(π·fc/fs), Vh = 10^(G/20), Vb = Vh^0.4996667741545416, a0 = 1 + K/Q + K²;
     b0 = (Vh + Vb·K/Q + K²)/a0, b1 = 2(K² − Vh)/a0, b2 = (Vh − Vb·K/Q + K²)/a0,
     a1 = 2(K² − 1)/a0, a2 = (1 − K/Q + K²)/a0.
   * Stage 2 (high-pass): Q = 0.5003270373253953, fc = 38.13547087613982; K, a0 as above;
     b = (1, −2, 1), a1 = 2(K² − 1)/a0, a2 = (1 − K/Q + K²)/a0.
   (At 48 kHz these are the BS.1770 table values; a 1 kHz sine of RMS −20 dBFS measures −20.0.)
2. **Blocks:** B = round(0.4·fs), hop S = round(0.1·fs). Blocks start at 0, S, 2S, … while
   start + B ≤ N. zⱼ = mean of y² over the block. If N < B: one block = the whole asset. N = 0 →
   silent.
3. lⱼ = −0.691 + 10·log10(zⱼ). **Absolute gate:** keep lⱼ > −70. None left → silent.
4. **Relative gate:** Γ = −0.691 + 10·log10(mean of kept zⱼ) − 10. Keep lⱼ > Γ.
5. **L** = −0.691 + 10·log10(mean of kept zⱼ).

**Peak P** = 20·log10(max |x[n]|) of the reference render (sample peak; none for silence).

### 4.2 When it is measured
* **WAVE:** at its first load in the run (load cost is negligible). Cache by (file, id).
* **DOS song:** when the DOS files are loaded (12 §1.3), all 49 used songs of the active
  variant(s), offline (≈ 0.1 s CPU in total). Cache by (variant, song).
* **MIDI:** when a SoundFont is loaded, the 8 songs of the active B/E set are rendered offline
  (§2.2) by a **second** FluidSynth synth (same SoundFont and settings) on a worker thread, faster
  than real time. Cache in memory by (SoundFont path, size, mtime, file name).
* **Song requested before its measurement is done (v1.29; MIDI and DOS songs):** measure that song
  on demand, synchronously, before it starts (it takes priority over the background work; if the worker
  is already measuring it, wait for that result). Cap the wait at **1.5 s** from the request. If the
  cap is hit, play the song at once with G for this play; the measured gain applies from its next
  start. Either way the start is delayed by at most 1.5 s, and the level never changes in the
  middle of a song. (Without this rule the title song, which starts during the initial measurement,
  played its first time at G: intro.mid was 3.9 dB above target.)

### 4.3 Gain per asset
For an asset of (style, bus) with style gain G and range R (effects 12 dB, speech 12 dB, music 6 dB):
```
if even_out off or the asset is silent:   g = G
else:  g = clamp(T − L, G − R, G + R)
       if the asset is a WAVE:            g = min(g, +3.0 − P)      # sample peak at most +3 dBFS
g = floor(g·10 + 0.5) / 10                                           # 0.1 dB steps
```
The peak cap lets loud peaks into the master soft clip (§5) by at most 3 dB; DOS buses have their own
soft clip. Result on the real data (all assets alone, after soft clips): every category at its target
within 0.1 dB, except two that hit a limit: 1702 (−17.3, cut limit) and the unused 1741 (−20.5,
boost limit). PC-speaker effect song 24 lands at −16.9 (its soft clip).

## 5. Mixing (amends 12 §4.3)
```
voice out = sample · 10^((g + slider_dB(bus)) / 20)         # per WAVE voice; per MIDI synth output; per DOS bus
DOS bus   = softclip(dos_out · 10^((g + slider_dB)/20), knee 0.6, ceiling 0.95)   # as 12 §4.3, new gains
mix       = Σ voices + Σ DOS buses
output    = softclip(mix, knee 0.8, ceiling 1.0) → 16-bit
softclip(x, k, c) = x                                     if |x| < k
                  = sign(x) · (k + (c−k)·tanh((|x|−k)/(c−k)))  otherwise
```
* The master soft clip replaces the hard clip.
* **DOS shared voice (12 §5.1):** the bus gain is that of the song it is playing. It switches when a
  new song is accepted (play / force), with a 5 ms linear ramp. Hanging notes of the previous song
  (12 §3.14) take the new gain.
* WAVE playback rate conversion is unchanged; the gain applies to the converted samples.

## 6. Volume sliders and the Audio Options dialog (amends 12 §6.3 / §6.5)

### 6.1 Slider law
Values 0–100, integers. slider_dB(v) = **40·log10(v/100)** for v ≥ 1 (100 → 0 dB, 80 → −3.88,
50 → −12.04, 10 → −40, 1 → −80); **v = 0 → muted** (gain 0). Defaults: Music **100**, Effects
**100**, Speech **100**. The per-player Music / Sound Effects / Voice switches (03 §5.1) are
independent: a slider at 0 silences the bus, but requests and events.log lines still happen.

### 6.2 Dialog template (replaces the 12 §6.3 / 14 §9 template)
`DIALOG style 0x90C000C2, rect (0,0,285,132), caption "Audio Options", font TLC Keycaps 11`
(this non-original template keeps 1 DLU = 1.75 px horizontally, see 12 §6.3: 285 × 1.75 = 499 px wide; 132 DLU high; centred as before).

| id | class | rect (x,y,cx,cy) DLU | style | text |
|---|---|---|---|---|
| 301–303, 311–313 and their labels | | **unchanged** (12 §6.3) | | |
| −1 | STATIC | 200,5,80,8 | 0x50000000 | `Volume:` |
| −1 | STATIC | 205,15,28,8 | 0x50000000 | `Music` |
| 340 | SCROLLBAR | 234,15,32,8 | 0x50010000 (horizontal, tab stop) | range 0–100 |
| 343 | STATIC | 268,15,14,8 | 0x50000002 (right-aligned) | value, e.g. `100` |
| −1 | STATIC | 205,25,28,8 | 0x50000000 | `Effects` |
| 341 | SCROLLBAR | 234,25,32,8 | 0x50010000 | range 0–100 |
| 344 | STATIC | 268,25,14,8 | 0x50000002 | value |
| −1 | STATIC | 205,35,28,8 | 0x50000000 | `Speech` |
| 342 | SCROLLBAR | 234,35,32,8 | 0x50010000 | range 0–100 |
| 345 | STATIC | 268,35,14,8 | 0x50000002 | value |
| 346 | BUTTON checkbox | 205,46,78,8 | 0x50010003 (auto check box, tab stop) | `&Even out volume` |
| −1 | STATIC | 5,57,275,1 | 0x50000004 | (separator; was y 47) |
| −1 | STATIC | 5,62,275,8 | 0x50000000 | `DOS game folder (SSO.EXE, MOCKET.DAT):` |
| 320 | EDIT | 5,72,225,12 | 0x50810080 | current DosGameDir |
| 331 | BUTTON | 235,71,45,14 | 0x50010000 | `&Install` (14 §9) |
| 321 | STATIC | 5,87,275,16 | 0x50000000 | status, 2 lines |
| −1 | STATIC | 5,106,275,1 | 0x50000004 | (separator) |
| 1 | BUTTON | 5,112,50,15 | 0x50030001 | `&OK` |
| 330 | BUTTON | 117,112,50,15 | 0x50010000 | `&Test` |
| 2 | BUTTON | 230,112,50,15 | 0x50010000 | `&Cancel` |

* **Scroll bars:** line step 1 (arrow keys when focused, arrow buttons), page step 10 (PgUp/PgDn,
  click in the trough), Home = 0, End = 100, thumb drag. The value static updates as the value changes.
* **Live:** every slider or checkbox change applies at once (5 ms ramp) to everything that is
  sounding, including the Test sample. **Cancel / Esc** restores the values the dialog opened with.
  **OK** keeps them and writes the INI (12 §6.4). The dialog still stops effects and pauses music
  while open (12 §6.3), so the Test button is how the user hears a change.
* **Test** (now required): effect (12 §6.3), then, if Voice is on, speech 1862 (first intro
  narration line; at most 4 s), then the first 4 s of LEVEL1. Each part uses the current
  (unsaved) styles, sliders and Even-out setting.

### 6.3 `outnumbered.ini` [Audio] keys (added to 12 §6.5)
```
MusicVolume=100     ; 0..100
EffectsVolume=100   ; 0..100
SpeechVolume=100    ; 0..100
EvenOutVolume=1     ; 1 = on (default), 0/no/false = off
```
Integers outside 0–100 are clamped; a non-numeric value gives the default. Written only on OK (12
§6.5 rules). Command-line overrides (optional): `--music-volume N`, `--effects-volume N`,
`--speech-volume N`, `--even-out 0|1`; precedence as 12 §6.5.

## 7. Conformance

### 7.1 `audio-levels`
```
engine --data DIR [--dos-game DIR] [--soundfont F] --conform audio-levels [--even-out 0|1]
       [--volumes M,E,S] [--music-volume N] [--effects-volume N] [--speech-volume N] --out a.json
engine --conform audio-levels --wav FILE BUS [--wav FILE BUS ...] [--even-out 0|1] --out a.json
```
Defaults: even-out 1, volumes 100,100,100. BUS = `effects` | `speech` | `music` (style Windows).
* **Options in any position (v1.29):** every option (`--data`, `--dos-game`, `--soundfont`,
  `--even-out`, the volume options, `--out`, …) may come before or after `--conform CMD`, with the
  same effect (README §3.1).
* `audio-levels` honours `--music-volume N`, `--effects-volume N` and `--speech-volume N` (§6.3) as
  well as `--volumes M,E,S`. If both are given for a bus, the later one on the command line wins.
  `audio-levels` does not read the INI; buses that are not set are 100.
Output:
```
{"targets": {"music": -20, "effects": -16, "speech": -15},
 "even_out": true, "volumes": {"music": 100, "effects": 100, "speech": 100},
 "slider_db": {"music": 0.0, "effects": 0.0, "speech": 0.0},          # null = muted
 "style_gain_db": {"windows": {"music": 12.6, "effects": -2.2, "speech": -0.9},
                   "tandy": {"music": -3.5, "effects": 4.8}, "speaker": {"music": -11.2, "effects": -6.8}},
 "midi_set": "e", "soundfont": "/path/GeneralUser-GS.sf2" | null,
 "assets": [{"source": "SND.DAT", "bus": "effects", "style": "windows", "id": 1702,
             "lufs": -3.12, "peak_dbfs": -0.07, "gain_db": -14.2, "total_db": -14.2}, ...]}
```
* `assets`: every WAVE of SND.DAT, SSO3.DAT (`effects`) and SPEECH.DAT (`speech`) in id order; the
  8 MIDI files of the active set (`source` "MIDI", `id` like `"level1.mid"`; only if a SoundFont is
  found); DOS songs 0–51 except 41, 44 for `tandy` then `speaker` (`source` "DOS", only if the
  DOS files are valid). With `--wav`: one entry per file, `source` "file", `id` = the file name.
* `lufs`, `peak_dbfs`: rounded to 0.01, null when silent. `gain_db` per §4.3 (computed from the
  unrounded L), `total_db` = gain_db + slider_db (null when muted). The command waits for the MIDI
  measurements.
* **Tolerances:** WAVE lufs/peak ±0.05 dB and gains ±0.1; MIDI ±0.5 (FluidSynth version).
  **DOS (v1.29):** `lufs` and `gain_db` (Even out on) ±1.5 dB against a render that meets 12 §7.5;
  `gain_db` with Even out off exact; **`peak_dbfs` is not compared** (band-limiting decides the sample
  peak of a square wave, and DOS peaks enter no gain). The engine's DOS values must equal §4.1 applied
  to its own `dos-render` output (±0.05 dB). The dirty side compares the full list against its
  reference (`tools/audio_levels.py`).

### 7.2 Play mode
* Script command `volume <music> <effects> <speech> [<even 0|1>]`: as if set in Audio Options and
  confirmed with OK (INI written).
* `state` JSON, `audio` object gains `"volumes": {"music", "effects", "speech"}, "even_out": bool`.

### 7.3 Fixtures (`fixtures/audio_levels/`)
Synthetic 8-bit WAVEs (1 kHz sines at −20 / −6 dBFS RMS at 22.05 / 11.025 kHz, a quiet 100 Hz
sine, a full-scale square, a 68 ms click shorter than one block, gated noise bursts that exercise the
relative gate, a near-silent file with a full-scale spike (peak cap), digital silence, an empty file
and a speech-bus case) with `expected.json`: L, P, the gains with Even out on and off, and the
slider law table. Check them with the `--wav` form of §7.1.
