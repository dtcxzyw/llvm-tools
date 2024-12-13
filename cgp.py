import os
import sys
import subprocess
import tqdm
from multiprocessing import Pool

opt = sys.argv[1]
path = sys.argv[2]
threads = int(sys.argv[3])

work_list = []
block_list = []

for r,ds,fs in os.walk(path):
    if 'optimized' not in r:
        continue
    for f in fs:
        if f.endswith('.ll'):
            test_file = os.path.join(r,f)
            skip = False
            for key in block_list:
                if key in test_file:
                    skip = True
                    break
            if not skip:
                work_list.append(test_file)

def verify(test_file):
    try:
        subprocess.check_call([opt, '-passes=verify', '-mtriple=x86_64-pc-linux-gnu', '--non-global-value-max-name-size=10000', '--disable-output', test_file],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    except Exception:
        return (test_file, True)

    try:
        subprocess.check_call([opt, '--codegenprepare', '-mtriple=x86_64-pc-linux-gnu', '--non-global-value-max-name-size=10000', '--disable-output', test_file],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        return (test_file, True)
    except Exception:
        pass
    return (test_file, False)

pool = Pool(processes=threads)

progress = tqdm.tqdm(work_list)
for test_file, res in pool.imap_unordered(verify, work_list):
    progress.update()
    if not res:
        progress.write(test_file + '\n')
progress.close()
