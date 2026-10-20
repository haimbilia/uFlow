"""Give shared AVBufferPool real Wii U locks without enabling codec threads."""
from pathlib import Path
import sys

root = Path(sys.argv[1]) / 'libavutil'
p = root / 'buffer_internal.h'
s = p.read_text()
if 'UFlowBufferMutex mutex;' not in s:
    assert s.count('    AVMutex mutex;') == 1
    s = s.replace('#include "thread.h"', '#include "thread.h"\n#include "wiiu-buffer-pool.h"')
    s = s.replace('    AVMutex mutex;', '    UFlowBufferMutex mutex;')
    p.write_text(s)
p = root / 'buffer.c'
s = p.read_text()
if 'uflow_buffer_mutex_lock' not in s:
    assert s.count('ff_mutex_lock(&pool->mutex)') == 2
    for op in ('init', 'lock', 'unlock', 'destroy'):
        assert 'ff_mutex_' + op in s
        s = s.replace('ff_mutex_' + op, 'uflow_buffer_mutex_' + op)
    p.write_text(s)
