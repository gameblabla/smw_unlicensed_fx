#!/usr/bin/env python3
"""Render Mega Drive sound commands by patching a BlastEm native state."""

from __future__ import annotations

import argparse
import array
import csv
import subprocess
import wave
from pathlib import Path


MAGIC = b"BLSTSZ\x01\x07"


def patch_z80_mailbox(source: bytes, command: int, low_level: bool = True) -> bytes:
    if not source.startswith(MAGIC):
        raise ValueError("not a supported BlastEm native state")
    data = bytearray(source)
    pos = len(MAGIC)
    while pos + 6 <= len(data):
        sid = int.from_bytes(data[pos : pos + 2], "big")
        size = int.from_bytes(data[pos + 2 : pos + 6], "big")
        payload = pos + 6
        if sid == 11:
            if size != 8193 or data[payload] != 8:
                raise ValueError("unexpected Z80 RAM state section")
            zram = payload + 1
            if low_level:
                # One-shot hook at the driver's main loop. It calls the native
                # all-stop routine ($07E1), queues the requested command, restores
                # the overwritten loop bytes, and resumes normal execution.
                data[zram + 0x006F : zram + 0x0072] = b"\xC3\x01\x11"  # JP $1101
                trampoline = bytes((
                    0xCD, 0xE1, 0x07,              # CALL $07E1
                    0x3E, command,                 # LD A,command
                    0x32, 0x0A, 0x1C,              # LD ($1C0A),A
                    0x21, 0x6F, 0x00,              # LD HL,$006F
                    0x36, 0xFB, 0x23,              # restore EI; INC HL
                    0x36, 0xCD, 0x23,              # restore CALL opcode; INC HL
                    0x36, 0x79,                    # restore low address byte
                    0xC3, 0x6F, 0x00,              # JP $006F
                ))
                data[zram + 0x1101 : zram + 0x1101 + len(trampoline)] = trampoline
                data[zram + 0x1C0A : zram + 0x1C0D] = b"\0\0\0"
            else:
                data[zram + 0x1C0A] = command
            data[zram + 0x1C0B] = 0
            data[zram + 0x1C0C] = 0
            return bytes(data)
        pos = payload + size
    raise ValueError("state has no Z80 RAM section")


def subtract_wav(capture: Path, baseline: Path, output: Path) -> tuple[int, int]:
    with wave.open(str(capture), "rb") as src, wave.open(str(baseline), "rb") as ref:
        params = src.getparams()
        if params[:4] != ref.getparams()[:4] or params.sampwidth != 2:
            raise ValueError("capture and baseline WAV formats do not match")
        a = array.array("h", src.readframes(src.getnframes()))
        b = array.array("h", ref.readframes(ref.getnframes()))
    count = min(len(a), len(b))
    diff = array.array("h", (max(-32768, min(32767, a[i] - b[i])) for i in range(count)))
    peak = max((abs(x) for x in diff), default=0)
    with wave.open(str(output), "wb") as dst:
        dst.setparams(params)
        dst.writeframes(diff.tobytes())
    return peak, count // params.nchannels


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--state", type=Path, required=True)
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--first", type=lambda x: int(x, 0), default=0x90)
    parser.add_argument("--last", type=lambda x: int(x, 0), default=0xF8)
    parser.add_argument("--frames", type=int, default=3000,
                        help="absolute ending frame; must exceed the state's frame counter")
    parser.add_argument("--mix-subtraction", action="store_true",
                        help="legacy mode: subtract a no-command music capture")
    args = parser.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    original = args.state.read_bytes()
    rows = []
    commands = ([0] if args.mix_subtraction else []) + list(range(args.first, args.last + 1))
    baseline = args.output / ".baseline.wav"
    for command in commands:
        state = args.output / f".command_{command:02X}.state"
        raw_wav = baseline if command == 0 else args.output / f".raw_{command:02X}.wav"
        wav = args.output / f"command_{command:02X}.wav"
        state.write_bytes(patch_z80_mailbox(original, command,
                                            low_level=not args.mix_subtraction))
        proc = subprocess.run(
            [str(args.emulator), "-s", str(state), "-b", str(args.frames),
             "-w", str(raw_wav), str(args.rom)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
        )
        state.unlink(missing_ok=True)
        if command == 0 and args.mix_subtraction:
            print(f"baseline: rc={proc.returncode} wav={raw_wav.exists()}")
            continue
        try:
            if args.mix_subtraction:
                peak, frames = subtract_wav(raw_wav, baseline, wav)
            else:
                raw_wav.replace(wav)
                with wave.open(str(wav), "rb") as isolated:
                    if ((isolated.getnchannels(), isolated.getsampwidth(),
                         isolated.getframerate(), isolated.getnframes()) !=
                            (2, 2, 48000, 50176)):
                        raise ValueError(f"unexpected WAV parameters: {isolated.getparams()}")
                    samples = array.array("h", isolated.readframes(isolated.getnframes()))
                    peak = max((abs(x) for x in samples), default=0)
                    frames = isolated.getnframes()
            status = "ok"
        except (EOFError, wave.Error, ValueError) as exc:
            peak, frames = 0, 0
            status = f"invalid capture: {exc}"
            wav.unlink(missing_ok=True)
        raw_wav.unlink(missing_ok=True)
        diagnostic = (status + " | " + proc.stderr.strip().replace("\n", " | "))[:1000]
        rows.append((f"{command:02X}", wav.name, proc.returncode,
                     wav.stat().st_size if wav.exists() else 0, peak, frames,
                     diagnostic))
        print(f"{command:02X}: {status} peak={peak} frames={frames}")
    baseline.unlink(missing_ok=True)

    with (args.output / "render_log.csv").open("w", newline="") as fp:
        writer = csv.writer(fp)
        writer.writerow(("command_hex", "wav", "return_code", "bytes",
                         "difference_peak_s16", "sample_frames", "stderr"))
        writer.writerows(rows)


if __name__ == "__main__":
    main()
