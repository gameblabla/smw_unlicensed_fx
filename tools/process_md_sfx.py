#!/usr/bin/env python3
"""Master every audible low-level MD capture to 32 kHz PCM and PC-FX ADPCM."""
import argparse
import csv
import pathlib
import struct
import wave

import build_sfx_adpcm as converter


MIN_SOURCE_PEAK = 512
MIN_RELEASE_SILENCE_MS = 100


def write_wav(path, samples):
    # The prepared signal uses the SoundBox's signed 14-bit predictor range.
    pcm = [converter.clamp(sample * 2, -32768, 32767) for sample in samples]
    with wave.open(str(path), 'wb') as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(converter.RATE)
        wav.writeframes(struct.pack('<%dh' % len(pcm), *pcm))


def decode_adpcm(data, sample_count):
    predictor = step_index = 0
    output = []
    for offset in range(0, len(data), 2):
        word = struct.unpack_from('<H', data, offset)[0]
        for shift in (0, 4, 8, 12):
            nibble = (word >> shift) & 15
            delta = converter.STEP_SIZES[step_index] * ((nibble & 7) + 1)
            if nibble & 8:
                delta = -delta
            predictor = converter.clamp(predictor + delta, -0x4000, 0x3fff)
            step_index = converter.clamp(
                step_index + converter.STEP_DELTAS[nibble], 0, 48)
            output.append(predictor)
    return output[:sample_count]


def main():
    parser = argparse.ArgumentParser()
    root = pathlib.Path(__file__).resolve().parent.parent
    parser.add_argument('--input', type=pathlib.Path,
                        default=root / 'megadrive_sfx_wav_lowlevel')
    parser.add_argument('--output', type=pathlib.Path,
                        default=root / 'megadrive_sfx_32khz_processed')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for pattern in ('command_*.wav', 'command_*.adpcm'):
        for stale in args.output.glob(pattern):
            stale.unlink()
    rows = []
    rejected = []
    for source in sorted(args.input.glob('command_*.wav')):
        command = source.stem.split('_')[-1].upper()
        samples, source_rate = converter.read_mono(source)
        source_peak = max((abs(value) for value in samples), default=0)
        if source_peak < MIN_SOURCE_PEAK:
            rejected.append((command, source.name, source_peak, 'silent/diagnostic'))
            continue
        active = [index for index, value in enumerate(samples) if abs(value) > 8]
        release_silence = (len(samples) - active[-1] - 1) * 1000 / source_rate
        if release_silence < MIN_RELEASE_SILENCE_MS:
            # Looping music/control commands and malformed dispatches never
            # return to silence. They are not safe one-shot sample material.
            rejected.append((command, source.name, source_peak,
                             'non-terminating/looping'))
            continue
        prepared = converter.prepare(
            samples, source_rate, **converter.command_tuning(command))
        adpcm = converter.encode(prepared)
        stem = 'command_%s' % command
        write_wav(args.output / (stem + '.wav'), prepared)
        (args.output / (stem + '.adpcm')).write_bytes(adpcm)
        write_wav(args.output / (stem + '_adpcm_preview.wav'),
                  decode_adpcm(adpcm, len(prepared)))
        rows.append((command, source.name, len(prepared), '%.6f' %
                     (len(prepared) / converter.RATE), source_peak,
                     max(map(abs, prepared)), len(adpcm),
                     'softened' if converter.command_tuning(command) else 'standard'))
        print('%s: %.3fs, %d-byte ADPCM%s' %
              (command, len(prepared) / converter.RATE, len(adpcm),
               ' (softened)' if converter.command_tuning(command) else ''))
    with (args.output / 'manifest.csv').open('w', newline='') as fp:
        writer = csv.writer(fp)
        writer.writerow(('command_hex', 'source_wav', 'pcm_samples_32khz',
                         'duration_seconds', 'source_peak', 'processed_peak_14bit',
                         'adpcm_bytes', 'mastering'))
        writer.writerows(rows)
    with (args.output / 'rejected.csv').open('w', newline='') as fp:
        writer = csv.writer(fp)
        writer.writerow(('command_hex', 'source_wav', 'source_peak', 'reason'))
        writer.writerows(rejected)
    print('%s: %d audible commands' % (args.output, len(rows)))


if __name__ == '__main__':
    main()
