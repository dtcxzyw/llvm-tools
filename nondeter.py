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
blockList = {"ipt.NumInstScanned"}

for r,ds,fs in os.walk(path):
    if '/original' not in r:
        continue
    for f in fs:
        if f.endswith('.ll'):
            test_file = os.path.join(r,f)
            work_list.append(test_file)

def filterstr(s: str):
    return s[s.find('{'): s.find('}')+1]

def verify(test_file):
    try:
        out1 = json.loads(filterstr(subprocess.check_output([llvm_bin+"/opt", "-O3", "--stats", "-stats-json", "--disable-output", test_file], stderr=subprocess.STDOUT).decode('utf-8')))
        out2 = json.loads(filterstr(subprocess.check_output([llvm_bin+"/opt", "-O3", "--stats", "-stats-json", "--disable-output", test_file], stderr=subprocess.STDOUT).decode('utf-8')))

        if out1 == out2:
            return (test_file, None)
        x = set()
        for k in out1:
            if k in blockList:
                continue
            if k in out2 and out1[k] != out2[k]:
                x.add(k)
        if len(x) == 0:
            x = None
        return (test_file, x)
    except Exception as e:
        print(e)
        pass
    
    return (test_file, None)

pool = Pool(processes=threads)

progress = tqdm.tqdm(work_list)
s = set()
for test_file, res in pool.imap_unordered(verify, work_list):
    progress.update()
    if res is not None:
        progress.write(test_file + '\n')
        for k in res:
            s.add(k)
        #exit(0)
progress.close()

for k in s:
    print(k)
