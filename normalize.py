import os
import sys
import subprocess
import tqdm
from multiprocessing import Pool

opt = sys.argv[1]
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

def normalize(test_file):
    try:
        subprocess.check_call([opt, '--passes=normalize', '--disable-output', test_file],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=120)
        return (test_file, "Success")
    except subprocess.TimeoutExpired:
        return (test_file, "Timeout")
    # except subprocess.CalledProcessError:
    #     return (test_file, "Crash")
    except Exception:
        pass
    return (test_file, "Success")

pool = Pool(processes=threads)

with open("build/normalize.log", "w") as f:
    progress = tqdm.tqdm(work_list)
    for test_file, res in pool.imap_unordered(normalize, work_list):
        progress.update()
        if res != "Success":
            progress.write(test_file + ' ' + res)
            f.write(test_file + " " + res + "\n")
    progress.close()
