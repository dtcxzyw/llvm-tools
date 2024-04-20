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
        subprocess.run([alive_tv, '--smt-to=300', '--disable-undef-input', '--passes=instcombine<no-verify-fixpoint>', '--save-ir', '--report-dir=report_dir', test_file],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=120.0)
    except Exception:
        pass
    return test_file

os.makedirs('report_dir')
pool = Pool(processes=threads)

with open('alive2.log', 'w') as f:
    progress = tqdm.tqdm(work_list)
    for test_file in pool.imap_unordered(verify, work_list):
        progress.update()
    progress.close()
