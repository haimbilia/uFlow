"""Report the wrap discontinuity of decoded PCM16 loops; requires numpy."""
import json
import sys
import wave
import numpy as np

for path in sys.argv[1:]:
    with wave.open(path, 'rb') as w:
        assert w.getsampwidth() == 2
        rate, channels = w.getframerate(), w.getnchannels()
        audio = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').reshape(-1, channels).astype(float)/32768
    step = float(np.max(np.abs(audio[-1] - audio[0])))
    normal = float(np.quantile(np.abs(np.diff(audio, axis=0)), .999))
    print(json.dumps({'file': path, 'duration_seconds': len(audio)/rate,
                      'peak': float(np.max(np.abs(audio))), 'wrap_step': step,
                      'normal_step_p999': normal, 'wrap_within_normal_range': step <= normal}))
