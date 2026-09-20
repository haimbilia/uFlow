"""Find candidate loop periods in a mono PCM16 4 kHz analysis WAV.

Use ffmpeg to create the analysis WAV first. Reports correlation, not a claim of
musical correctness; inspect candidate periods and boundaries before trimming.
Requires numpy; does not modify the audio.
"""
import argparse
import json
import wave
import numpy as np

p = argparse.ArgumentParser()
p.add_argument('wav')
p.add_argument('--min-period', type=float, default=8)
p.add_argument('--max-period', type=float, default=150)
p.add_argument('--start', type=float, default=10)
p.add_argument('--window', type=float, default=10)
a = p.parse_args()
with wave.open(a.wav, 'rb') as w:
    assert w.getsampwidth() == 2 and w.getnchannels() == 1
    rate = w.getframerate()
    signal = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(float)
signal -= signal.mean()
start = round(a.start * rate)
size = round(a.window * rate)
template = signal[start:start + size]
nfft = 1 << (len(signal) + len(template) - 1).bit_length()
cross = np.fft.irfft(np.fft.rfft(signal, nfft) * np.conj(np.fft.rfft(template, nfft)), nfft)
energy = np.concatenate(([0.], np.cumsum(signal * signal)))
count = len(signal) - size + 1
norm = np.sqrt(np.maximum(energy[size:] - energy[:-size], 1) * np.dot(template, template))
scores = cross[:count] / norm
lo = start + round(a.min_period * rate)
hi = min(count - 1, start + round(a.max_period * rate))
peaks = np.where((scores[lo:hi] > scores[lo-1:hi-1]) & (scores[lo:hi] >= scores[lo+1:hi+1]))[0] + lo
candidates = []
for i in peaks[np.argsort(scores[peaks])[::-1]]:
    period = (int(i) - start) / rate
    if all(abs(period - x['period_seconds']) > 1 for x in candidates):
        candidates.append({'period_seconds': period, 'correlation': round(float(scores[i]), 6)})
        if len(candidates) == 12:
            break
print(json.dumps({'duration': len(signal)/rate, 'template_start': a.start,
                  'template_seconds': a.window, 'candidates': candidates}, indent=2))
