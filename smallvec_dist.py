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
# work_list = work_list[:1000]

def extract_dist(err: str):
    dist = dict()
    for line in err.splitlines():
        if line.startswith('SmallVec '):
            line = line.removeprefix('SmallVec ')
            pos = line.find(' ')
            size = int(line[:pos])
            key = line[pos+1:]
            tmp = dist.get(key, dict())
            tmp[size] = tmp.get(size, 0) + 1
            dist[key] = tmp

    return dist

def verify(test_file):
    try:
        ret = subprocess.run([llvm_bin+"/opt", "-O3", "--disable-output", "--dump-small-vec-size", test_file], capture_output=True, timeout=30)
        if ret.returncode == 0:
            err = ret.stderr.decode('utf-8')
            dist = extract_dist(err)
            return (test_file, dist)
        # print(ret.returncode)
        return (test_file, "error")
    except subprocess.TimeoutExpired:
        return (test_file, "timeout")
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
    if type(res) is str:
        timeout_cnt += 1
        progress.set_description(f"Timeout: {timeout_cnt}")
        continue
    for k, v in res.items():
        tmp = dist.get(k, dict())
        for size, cnt in v.items():
            tmp[size] = tmp.get(size, 0) + cnt
        dist[k] = tmp
progress.close()

with open('build/smallvec_dist.json', 'w') as f:
    json.dump(dist, f)

print("Timeout", timeout_cnt)
