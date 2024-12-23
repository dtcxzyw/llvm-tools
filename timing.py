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

for r,ds,fs in os.walk(path):
    if '/original' not in r:
        continue
    for f in fs:
        if f.endswith('.ll'):
            test_file = os.path.join(r,f)
            work_list.append(test_file)
# work_list = work_list[:100]

def extract_dist(err: str):
    dist = dict()
    for line in err.splitlines():
        if line.startswith('@Time '):
            line = line.removeprefix('@Time ')
            pos = line.find(' ')
            t = float(line[:pos])
            key = line[pos+1:]
            dist[key] = dist.get(key, 0) + t

    return dist

def verify(test_file):
    try:
        ret = subprocess.run([llvm_bin+"/opt", "-O3", "--disable-output", "--time-passes", test_file], capture_output=True)
        if ret.returncode == 0:
            err = ret.stderr.decode('utf-8')
            dist = extract_dist(err)
            return (test_file, dist)
        # print(ret.returncode)
        return (test_file, None)
    except Exception as e:
        print(e)
        pass

    return (test_file, None)

pool = Pool(processes=threads)

progress = tqdm.tqdm(work_list)
dist = dict()
timeout_cnt = 0
for test_file, res in pool.imap_unordered(verify, work_list):
    progress.update()
    if res is None:
        progress.write(test_file + '\n')
        exit(0)
    for k, v in res.items():
        dist[k] = dist.get(k, 0) + v
progress.close()

with open('build/timing.json', 'w') as f:
    json.dump(dist, f)

print("Timeout", timeout_cnt)
