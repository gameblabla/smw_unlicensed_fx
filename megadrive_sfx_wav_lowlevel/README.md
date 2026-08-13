# Low-level isolated Mega Drive sound effects

These WAV files are rendered directly by the ROM's original Z80 SMPS-like
driver, with music disabled inside the driver before each sound starts.

For every command, the renderer installs a one-shot Z80 RAM trampoline that:

1. calls the driver's native all-stop routine at Z80 `$07E1` (clears music and
   effect tracks, keys off all six FM channels, and silences PSG channels),
2. writes one requested command to mailbox `$1C0A`,
3. restores the original main-loop instructions, and
4. resumes the driver so only that command is synthesized.

No mix subtraction, filtering, source separation, or noise gating is used.
Files are stereo 16-bit PCM at 48 kHz and are named after their hexadecimal
mailbox command. `render_log.csv` records all commands tested from `$90` through
`$F8`; invalid dispatch values have no WAV.

The reproducible extraction utility is `../tools/render_md_sfx.py`.
