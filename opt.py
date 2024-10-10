import os
import sys
import subprocess
import tqdm
import json
from multiprocessing import Pool

llvm_bin = sys.argv[1]
path = sys.argv[2]
threads = int(sys.argv[3])

work_list = []
ignored = ["SwitchLookupTable::SwitchLookupTable"]

for r,ds,fs in os.walk(path):
    if '/original' not in r:
        continue
    for f in fs:
        if f.endswith('.ll'):
            test_file = os.path.join(r,f)
            work_list.append(test_file)

def verify(test_file):
    try:
        ret = subprocess.run([llvm_bin+"/opt", "-O3", "--disable-output", test_file], capture_output=True)
        if ret.returncode == 0:
            return (test_file, True)
        err = ret.stderr.decode('utf-8')
        for key in ignored:
            if key in err:
                return (test_file, True)
        return (test_file, False)
    except Exception as e:
        print(e)
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
