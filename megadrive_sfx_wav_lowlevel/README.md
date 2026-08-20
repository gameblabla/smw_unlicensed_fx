# Low-level isolated Mega Drive sound effects

These WAV files are rendered directly by the ROM's original Z80 SMPS-like
driver, with music disabled inside the driver before each sound starts.

For every command, the renderer installs a one-shot Z80 RAM trampoline that:

1. calls the driver's native all-stop routine at Z80 `$07E1` (clears music and
   effect tracks, keys off all six FM channels, and silences PSG channels),
2. writes one requested command to mailbox `$1C0A`,
3. restores the original main-loop instructions, and
4. resumes the driver so only that command is synthesized.

The renderer subtracts a second all-stop/no-command pass sample-for-sample. This
only cancels BlastEm's deterministic post-stop floor (about -39 dBFS in the
original captures); it does not subtract music or attempt source separation.
Files are stereo 16-bit PCM at 48 kHz and are named after their hexadecimal
mailbox command. `render_log.csv` records the requested commands.

The PC-FX converter in `../tools/build_sfx_adpcm.py` then removes DC, applies a
windowed-sinc anti-alias filter, resamples to 32 kHz, trims silent tails with a
short release pad, fades the boundaries, and encodes the result as native
KING/SoundBox 4-bit ADPCM.

The reproducible extraction utility is `../tools/render_md_sfx.py`. Build its
required fixed headless renderer with `../tools/build_md_sfx_renderer.sh`.
The patch makes the offline mix buffer exactly one emulated frame (800 samples
for NTSC) instead of 1024; the latter appended 224 stale samples per block and
was the cause of the formerly skippy/jumbled captures.

The seven PC-FX effects can be recaptured in one deterministic pass with
`--commands 90,96,99,9D,9E,A8,A9`; use an ending frame of 3300 with the bundled
gameplay state so long releases finish before capture stops.

Run `../tools/process_md_sfx.py` after a full `$90-$F8` capture to create 32 kHz
mono listening WAVs, raw PC-FX ADPCM streams, decoded ADPCM preview WAVs and a
manifest for every audible command. Silent and malformed dispatch values are
excluded. Command `$96` receives gentler gain, a 10.5 kHz low-pass and longer
edge fades in processed output; its archival 48 kHz capture remains untouched.
