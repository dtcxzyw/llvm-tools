import os
import sys
import subprocess
import tqdm
import re
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
work_list = work_list[:10000]

def extract_dead_func(err: str):
    passcnt = dict()
    funcmap = dict()
    funcset = set()
    for line in err.splitlines():
        if line.startswith('Dead func '):
            line = line.removeprefix('Dead func ')
            pos = line.find(' ')
            func = line[:pos]
            if func in funcmap:
                continue
            passid = line[pos+1:]
            funcmap[func] = passid
        elif line.startswith('Dead run '):
            func = line.removeprefix('Dead run ')
            if func in funcset:
                continue
            funcset.add(func)
            passid = funcmap[func]
            passcnt[passid] = passcnt.get(passid, 0) + 1

    return passcnt

def verify(test_file):
    try:
        ret = subprocess.run([llvm_bin+"/opt", "-O3", "--disable-output", test_file], capture_output=True, timeout=30)
        if ret.returncode == 0:
            err = ret.stderr.decode('utf-8')
            dead = extract_dead_func(err)
            return (test_file, dead)
        # print(ret.returncode)
        return (test_file, None)
    except subprocess.TimeoutExpired:
        return (test_file, "timeout")
    except Exception as e:
        print(e)
        pass

    return (test_file, None)

pool = Pool(processes=threads)

progress = tqdm.tqdm(work_list)
passcnt = dict()
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
    # if 'SROAPass' in res:
    #     print(test_file)
    #     exit(0)
    for k, v in res.items():
        passcnt[k] = passcnt.get(k, 0) + v
progress.close()

result = []
for k, v in passcnt.items():
    result.append((k, v))
result.sort(key=lambda x:x[1], reverse=True)

for k, v in result:
    print(k, v)

print("Timeout", timeout_cnt)
