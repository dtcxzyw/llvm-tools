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
pattern = re.compile("[*][*][*] IR Dump After ([^ ]+) on (loop [^ ]+ in function )?([^ ]+) (omitted because no change )?[*][*][*]")
assert pattern.match("*** IR Dump After LoopDistributePass on unix_recv_io omitted because no change ***")
assert pattern.match("*** IR Dump After InferAlignmentPass on unix_recv_io ***")
assert pattern.match('*** IR Dump After LoopDeletionPass on loop %for.cond1.preheader in function crc32_gentab omitted because no change ***')
assert pattern.match('*** IR Dump After IPSCCPPass on [module] ***')

def extract_pass_seq(log: str):
    nochange_pass = dict()
    dup_pass = dict()
    run_pass = dict()
    for line in log.splitlines():
        res = pattern.match(line)
        if not res:
            continue
        name = res[1]
        loop = res[2]
        func = res[3]
        nochange = res[4]
        run_pass[name] = run_pass.get(name, 0) + 1 
        # print(name, loop, func, change)
        if nochange is None:
            if func == '[module]':
                nochange_pass = dict()
            if loop is None:
                nochange_pass[func] = {name}
            else:
                # ignore loop passes
                nochange_pass[func] = set()
        elif loop is None:
            if func not in nochange_pass:
                nochange_pass[func] = set()
            if name in nochange_pass[func]:
                dup_pass[name] = dup_pass.get(name, 0) + 1
            nochange_pass[func].add(name)
    # print(dup_pass)
    return (dup_pass, run_pass)

for r,ds,fs in os.walk(path):
    if '/original' not in r:
        continue
    for f in fs:
        if f.endswith('.ll'):
            test_file = os.path.join(r,f)
            work_list.append(test_file)
work_list = work_list[:1000]

def verify(test_file):
    try:
        ret = subprocess.run([llvm_bin+"/opt", "-O3", "--print-changed", "--disable-output", test_file], capture_output=True, timeout=300)
        if ret.returncode == 0:
            err = ret.stderr.decode('utf-8')
            return (test_file, extract_pass_seq(err))
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
dup_res = dict()
run_res = dict()
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
    dup_pass, run_pass = res
    for k, v in dup_pass.items():
        dup_res[k] = dup_res.get(k, 0) + v
    for k, v in run_pass.items():
        run_res[k] = run_res.get(k, 0) + v
progress.close()

result = []
for k, v in dup_res.items():
    result.append((k, v, run_res[k], v/run_res[k]*100))

result.sort(key=lambda x:x[3], reverse=True)

with open("build/dup_pass.log", "w") as f:
    for v1, v2, v3, v4 in result:
        print(f"{v1} {v2} {v3} {v4:.2f}%")
        f.write(f"{v1} {v2} {v3} {v4:.2f}%\n")
print("Timeout", timeout_cnt)
