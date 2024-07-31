import os
import sys
import subprocess
import tqdm
from multiprocessing import Pool

alive_tv = sys.argv[1]
path = sys.argv[2]
threads = int(sys.argv[3])

work_list = []

for r,ds,fs in os.walk(path):
    for f in fs:
        if f.endswith('.ll'):
            test_file = os.path.join(r,f)
            work_list.append(test_file)

def verify(test_file):
    try:
        out = subprocess.check_output([alive_tv, '--smt-to=1000', '--disable-undef-input', test_file],timeout=120.0).decode('utf-8')
        if out.find('Transformation seems to be correct!') != -1:
            return (test_file, True)
    except subprocess.CalledProcessError as e:
        return (test_file, None)
    except Exception:
        pass
    return (test_file, False)

pool = Pool(processes=threads)

with open('alive2.log', 'w') as f:
    progress = tqdm.tqdm(work_list)
    for test_file, res in pool.imap_unordered(verify, work_list):
        if res is None:
            f.write(f'ERROR: {test_file}\n')
        if res:
            f.write(f'PASS: {test_file}\n')
        progress.update()
    progress.close()
