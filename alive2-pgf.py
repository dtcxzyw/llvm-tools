import sys
import os
import subprocess
import tqdm
import hashlib
from pathlib import Path
import random
from multiprocessing import Pool

tv_exec = sys.argv[1]
test_dir = sys.argv[2]
cache_dir = sys.argv[3]
threads = int(sys.argv[4])

def verify(test):
    if not test.endswith('.ll'):
        return None
    name = test
    test = os.path.join(test_dir, test)

    with open(test, 'rb') as f:
        test_hash = hashlib.sha256(f.read()).hexdigest()
        path = Path(cache_dir + '/' + test_hash)
        if path.exists():
            return None
    try:
        output = subprocess.check_output([tv_exec, '-refine-tgt', '--passes=sccp', '--smt-to=100', test])
        fail = output.find(b'0 incorrect transformations') == -1
        miss = output.find(b"potential optimization") != -1
        if fail or miss:
            if fail:
                name = name + ' fail'
            if miss:
                name = name + ' miss'
            return name
        else:
            path.touch(exist_ok=False)
    except Exception as e:
        return None

os.makedirs(cache_dir, exist_ok=True)
fail_list = []
work_list = os.listdir(test_dir)
random.shuffle(work_list)
progress = tqdm.tqdm(work_list)

pool = Pool(processes=threads)

for test in pool.imap_unordered(verify, work_list):
    if test:
        fail_list.append(test)
        progress.write(test)
    progress.update()
progress.close()

with open('fail_list.txt', 'w') as f:
    for test in fail_list:
        f.write(test + '\n')
