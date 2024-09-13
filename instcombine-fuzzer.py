import os
import sys
import tqdm
import shutil
import subprocess
from multiprocessing import Pool

gen = sys.argv[1]
alive_tv = sys.argv[2]
work_dir = sys.argv[3]
test_count = int(sys.argv[4])
threads = int(sys.argv[5])

if os.path.exists(work_dir):
    shutil.rmtree(work_dir)
os.makedirs(work_dir)

def fuzz(idx):
    file = os.path.join(work_dir, f't{idx}')
    try:
        subprocess.check_call([gen, file], timeout=120.0)
        src = file + '.src'
        tgt = file + '.tgt'
        if not os.path.exists(src) or not os.path.exists(tgt):
            return
        ret = subprocess.check_output([alive_tv, '--smt-to=300', '--disable-undef-input', src, tgt], timeout=120.0)
        if '0 incorrect transformations' in ret.decode('utf-8'):
            os.remove(src)
            os.remove(tgt)
            return True
    except Exception as e:
        # print(e)
        pass

    return False

pool = Pool(processes=threads)
tasks = range(test_count)
progress = tqdm.tqdm(tasks)
failed = 0
for res in pool.imap_unordered(fuzz, tasks):
    if not res:
        failed += 1
    progress.update()
progress.close()

print(f'Failed: {failed}')
exit(1 if failed != 0 else 0)
