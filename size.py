import os
import sys

bench = sys.argv[1]

original = 0
optimized = 0

for r, ds, fs in os.walk(bench):
    if r.endswith('original'):
        for f in fs:
            original += os.path.getsize(os.path.join(r, f))

    if r.endswith('optimized'):
        for f in fs:
            optimized += os.path.getsize(os.path.join(r, f))

print('Original:', original)
print('Optimized:', optimized)
print('Ratio:', optimized / original)
