import os
import sys
import subprocess
import tqdm
from multiprocessing import Pool

llvm_bin = sys.argv[1]
path = sys.argv[2]
threads = int(sys.argv[3])

work_list = []

for r,ds,fs in os.walk(path):
    if 'optimized' not in r:
        continue
    for f in fs:
        if f.endswith('.ll'):
            test_file = os.path.join(r,f)
            work_list.append(test_file)

def verify(test_file):
    try:
        subprocess.check_call([llvm_bin+"/opt", '--passes=verify', '--disable-output', test_file],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    except Exception:
        return (test_file, True)
    
    try:
        subprocess.check_call([llvm_bin+"/opt", "-mattr=+cf", "-passes=simplifycfg<hoist-loads-stores-with-cond-faulting>", '--disable-output', test_file],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        return (test_file, True)
    except Exception as e:
        # print(e)
        pass
    
    return (test_file, False)

pool = Pool(processes=threads)

progress = tqdm.tqdm(work_list)
for test_file, res in pool.imap_unordered(verify, work_list):
    progress.update()
    if not res:
        progress.write(test_file + '\n')
        exit(0)
progress.close()
