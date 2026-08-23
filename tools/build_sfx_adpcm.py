#!/usr/bin/env python3
"""Filter Mega Drive captures and bake a PC-FX 32 kHz ADPCM bank."""
import math
import pathlib
import struct
import wave


RATE = 32000
ALIGN_WORDS = 256
KRAM_BUDGET_BYTES = 256 * 1024
KING_ADPCM_MAX_WORD = 0x3FFFF
# Keep the interpolation field derived from the selected hardware rate.  At
# 32 kHz it is zero: each nibble advances the predictor once by its full native
# delta.  The decoder-mirroring helper below still handles the other legal
# rates, so changing RATE cannot silently leave the encoder in a 16 kHz model.
ADPCM_RATE_FIELD = {32000: 0, 16000: 1, 8000: 2, 4000: 3}[RATE]
# Preserve the source effects' relative loudness while using the 14-bit KING
# predictor's range.  The loudest selected capture peaks just below 0x2000.
ENCODE_GAIN = 1.9
SILENCE_THRESHOLD = 8
TRIM_PAD_MS = 20
FIR_TAPS = 48
STEP_SIZES = (16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55,
              60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190,
              209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598,
              658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552)
STEP_DELTAS = (-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8)

# Numeric entries are the gameplay API ids.  Keeping the association here,
# beside the source filenames, makes the generated header data-only.
SOURCES = {
    1: ('jump', 'AE'),
    2: ('crate_throw', 'A8'),
    3: ('blue_crystal', '9E'),
    4: ('boss_hit', '90'),
    5: ('player_death', 'AF'),
    6: ('fireball', '9D'),
    7: ('player_hurt', '99'),
    8: ('menu_confirm', '9C'),
    9: ('boss_defeat', 'A9'),
    # $B7 never rendered from the Z80 driver (the capture failed with a format
    # mismatch), so command_B7.wav is the custom assets/jumpoverenemy.wav in the
    # same stereo 16-bit 48 kHz container as the driver renders.
    10: ('jump_over_enemy', 'B7'),
    # The enemy-defeat pop (stomp, fireball, thrown crate) reuses the custom
    # assets/jumpoverenemy.wav directly rather than any $9x driver capture.
    11: ('enemy_dead', 'assets/jumpoverenemy.wav'),
}


def clamp(value, lo, hi):
    return max(lo, min(hi, value))


def read_mono(path):
    with wave.open(str(path), 'rb') as wav:
        width = wav.getsampwidth()
        channels = wav.getnchannels()
        raw = wav.readframes(wav.getnframes())
        if width == 2:
            data = struct.unpack('<%dh' % (len(raw) // 2), raw)
        elif width == 1:
            # Unsigned 8-bit PCM: expand to signed 16-bit about the center.
            data = [((sample << 8) - 32768) for sample in raw]
        else:
            raise ValueError('%s must be 8- or 16-bit PCM' % path)
        if channels == 2:
            return [(data[i] + data[i + 1]) // 2
                    for i in range(0, len(data), 2)], wav.getframerate()
        if channels == 1:
            return list(data), wav.getframerate()
        raise ValueError('%s must be mono or stereo PCM' % path)


def trim_silence(samples, source_rate):
    """Remove only sample-exact quiet margins, retaining attack and release pads."""
    active = [i for i, sample in enumerate(samples)
              if abs(sample) > SILENCE_THRESHOLD]
    if not active:
        return [0]
    pad = source_rate * TRIM_PAD_MS // 1000
    first = max(0, active[0] - source_rate // 250)  # 4 ms pre-roll
    last = min(len(samples), active[-1] + pad + 1)
    return samples[first:last]


def highpass(samples, source_rate, cutoff=20.0):
    """First-order DC blocker; prevents predictor range being spent on offset."""
    pole = math.exp(-2.0 * math.pi * cutoff / source_rate)
    previous_input = previous_output = 0.0
    output = []
    for sample in samples:
        value = sample - previous_input + pole * previous_output
        output.append(value)
        previous_input, previous_output = sample, value
    return output


def resample(samples, source_rate, lowpass_hz=None):
    """Band-limited windowed-sinc resampling (48 kHz capture -> 32 kHz)."""
    if not samples:
        return [0]
    count = max(1, (len(samples) * RATE + source_rate // 2) // source_rate)
    # Leave a transition band below the 8 kHz output Nyquist frequency.
    cutoff = (min(0.46 * RATE / source_rate, 0.46) if lowpass_hz is None
              else min(lowpass_hz / source_rate, 0.46))
    half = FIR_TAPS // 2
    output = []
    for out_index in range(count):
        position = out_index * source_rate / RATE
        center = int(math.floor(position))
        total = weight_sum = 0.0
        for source_index in range(center - half + 1, center + half + 1):
            distance = position - source_index
            if abs(distance) >= half:
                continue
            angle = 2.0 * math.pi * cutoff * distance
            sinc = 2.0 * cutoff if distance == 0.0 else math.sin(angle) / (math.pi * distance)
            window = 0.5 + 0.5 * math.cos(math.pi * distance / half)
            weight = sinc * window
            sample = samples[clamp(source_index, 0, len(samples) - 1)]
            total += sample * weight
            weight_sum += weight
        output.append(total / weight_sum if weight_sum else 0.0)
    return output


def prepare(samples, source_rate, gain=ENCODE_GAIN, lowpass_hz=None,
            fade_in_ms=1, fade_out_ms=3):
    samples = trim_silence(samples, source_rate)
    samples = resample(highpass(samples, source_rate), source_rate, lowpass_hz)
    fade_in = min(len(samples), RATE * fade_in_ms // 1000)
    fade_out = min(len(samples), RATE * fade_out_ms // 1000)
    for i in range(fade_in):
        samples[i] *= i / max(1, fade_in)
    for i in range(fade_out):
        samples[-1 - i] *= i / max(1, fade_out)
    return [clamp(round(sample * gain), -0x4000, 0x3fff)
            for sample in samples]


def command_tuning(command):
    """Per-command mastering for unusually hot or sharp original effects."""
    if command.upper() == '96':
        # Command 96 is important but has an unusually hard, bright transient.
        # Keep its raw capture untouched; soften only converter/listening output.
        return dict(gain=1.35, lowpass_hz=10500, fade_in_ms=3, fade_out_ms=8)
    return {}


def decode_step(predictor, step_index, nibble):
    """Mirror one KING nibble, including its rate-dependent hold time."""
    delta = STEP_SIZES[step_index] * ((nibble & 7) + 1)
    delta >>= ADPCM_RATE_FIELD
    if nibble & 8:
        delta = -delta
    step_index = clamp(step_index + STEP_DELTAS[nibble], 0, 48)
    for _ in range(1 << ADPCM_RATE_FIELD):
        predictor = clamp(predictor + delta, -0x4000, 0x3fff)
    return predictor, step_index


def encode(samples):
    """Encode the KING/SoundBox 4-bit ADPCM stream (four nibbles per word)."""
    predictor = step_index = 0
    nibbles = []
    for sample in samples:
        target = clamp(sample, -0x4000, 0x3fff)
        best = None
        for nibble in range(16):
            # Score the predictor the decoder will actually reach after the
            # complete interpolation hold.  The old converter scored only
            # the first half-step, so its encoder state drifted away from the
            # hardware state and accumulated harsh noise on later samples.
            candidate, candidate_index = decode_step(predictor, step_index, nibble)
            error = (target - candidate) ** 2
            if best is None or error < best[0]:
                best = (error, nibble, candidate, candidate_index)
        _, nibble, predictor, step_index = best
        nibbles.append(nibble)
    while len(nibbles) % 4:
        nibbles.append(0)
    return b''.join(struct.pack('<H', nibbles[i] | (nibbles[i + 1] << 4) |
                                  (nibbles[i + 2] << 8) | (nibbles[i + 3] << 12))
                    for i in range(0, len(nibbles), 4))


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    source_dir = root / 'megadrive_sfx_wav_lowlevel'
    output = root / 'src' / 'sfx_adpcm_generated.h'
    bank = bytearray()
    metas = [(0, 0)] * 12
    labels = ['unused'] * 12

    for effect_id, (name, command) in SOURCES.items():
        while (len(bank) // 2) % ALIGN_WORDS:
            bank.extend(b'\0\0')
        if command.startswith('assets/'):
            source = root / command
        else:
            source = source_dir / ('command_%s.wav' % command)
        samples, source_rate = read_mono(source)
        prepared = prepare(samples, source_rate, **command_tuning(command))
        encoded = encode(prepared)
        metas[effect_id] = (len(bank) // 2, len(encoded) // 2)
        labels[effect_id] = name
        bank.extend(encoded)
        print('%-12s: %6d PCM samples, %5d ADPCM bytes' %
              (name, len(prepared), len(encoded)))

    if len(bank) > KRAM_BUDGET_BYTES:
        raise ValueError('ADPCM bank exceeds the 256 KiB KRAM budget')
    if any(start > KING_ADPCM_MAX_WORD or count > KING_ADPCM_MAX_WORD or
           (count and start + count - 1 > KING_ADPCM_MAX_WORD)
           for start, count in metas):
        raise ValueError('ADPCM metadata exceeds the KING address range')

    with output.open('w') as f:
        f.write('/* Generated by tools/build_sfx_adpcm.py; do not edit. */\n')
        f.write('#ifndef SMB_PCFX_SFX_ADPCM_GENERATED_H\n')
        f.write('#define SMB_PCFX_SFX_ADPCM_GENERATED_H\n\n')
        f.write('#define SFX_ADPCM_BANK_BYTES %du\n\n' % len(bank))
        # SAL is programmed in 256-word units and the KING end address is
        # 18-bit.  Keep the generated metadata wide enough for a 32 kHz bank;
        # the old 16-bit fields wrapped once the bank crossed 65535 words.
        f.write('typedef struct { unsigned int start_word, word_count; } sfx_adpcm_meta_t;\n')
        f.write('static const sfx_adpcm_meta_t sfx_adpcm_meta[%d] = {\n' % len(metas))
        for i, (start, count) in enumerate(metas):
            f.write('    { %du, %du }, /* %s */\n' % (start, count, labels[i]))
        f.write('};\n\nstatic const unsigned char sfx_adpcm_bank[SFX_ADPCM_BANK_BYTES] = {\n')
        for i in range(0, len(bank), 20):
            f.write('    ' + ', '.join(map(str, bank[i:i + 20])) + ',\n')
        f.write('};\n\n#endif\n')
    print('%s: %d bytes' % (output, len(bank)))


if __name__ == '__main__':
    main()
