import os
import sys
import subprocess
import tqdm
import json
from multiprocessing import Pool

llvm_bin = sys.argv[1]
path = sys.argv[2]
plugin = sys.argv[3]
threads = int(sys.argv[4])

work_list = []

for r, ds, fs in os.walk(path):
    if "/original" not in r:
        continue
    for f in fs:
        if f.endswith(".ll"):
            test_file = os.path.join(r, f)
            work_list.append(test_file)


def verify(test_file):
    try:
        ret = subprocess.run(
            [
                llvm_bin + "/opt",
                "-O3",
                "--disable-output",
                test_file,
                f"--load-pass-plugin={plugin}",
            ],
            capture_output=True,
            check=True,
        )
        return (test_file, ret.stderr.decode())
    except Exception:
        pass

    return (test_file, "")


pool = Pool(processes=threads)
log = open("speculate-ub.log", "w")

progress = tqdm.tqdm(work_list)
for test_file, res in pool.imap_unordered(verify, work_list):
    progress.update()
    if "rank increased from" in res:
        line = test_file
        if "JumpThreading" in res:
            line += " (JumpThreading)"
        if "SimplifyCFG" in res:
            line += " (SimplifyCFG)"
        log.write(line + "\n")
        log.flush()
progress.close()
log.close()
