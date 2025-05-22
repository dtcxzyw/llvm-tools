import os
import sys
import subprocess
import tqdm
import shutil
from multiprocessing import Pool

# python3 ../fcmp-nsz.py ../../llvm-build/bin/opt ../../../llvm-project/llvm/test/Transforms/InstSimplify/ 16
# python3 ../fcmp-nsz.py ../../llvm-build/bin/opt ../../../llvm-project/llvm/test/Transforms/InstCombine/ 16

opt = sys.argv[1]
path = sys.argv[2]
threads = int(sys.argv[3])

work_list = []

for r, ds, fs in os.walk(path):
    for f in fs:
        if f.endswith(".ll"):
            test_file = os.path.join(r, f)
            work_list.append(test_file)
work_list = list(enumerate(work_list))


def drop_nsz(src: str):
    lines = src.splitlines()
    new_lines = []
    for line in lines:
        if " = fcmp " in line:
            line = line.replace(" nsz ", " ")
        new_lines.append(line)
    return "\n".join(new_lines)


def add_nsz(src):
    lines = src.splitlines()
    new_lines = []
    for line in lines:
        if " = fcmp " in line:
            line = line.replace(" = fcmp ", " = fcmp nsz ")
        new_lines.append(line)
    return "\n".join(new_lines)


def verify(task):
    idx, test_file = task
    with open(test_file, "r") as f:
        content = f.read()
    if " = fcmp " not in content:
        return None
    content_without_nsz = drop_nsz(content)
    content_with_nsz = add_nsz(content_without_nsz)
    assert content_without_nsz != content_with_nsz
    try:
        opt_without_nsz = subprocess.check_output(
            [opt, "--passes=instcombine<no-verify-fixpoint>", "-S"],
            timeout=10.0,
            input=content_without_nsz.encode(),
        ).decode()
        opt_with_nsz = subprocess.check_output(
            [opt, "--passes=instcombine<no-verify-fixpoint>", "-S"],
            timeout=10.0,
            input=content_with_nsz.encode(),
        ).decode()
        opt_without_nsz = drop_nsz(opt_without_nsz)
        opt_with_nsz = drop_nsz(opt_with_nsz)
        base_path = os.path.join(
            "report_dir", str(idx) + "_" + os.path.basename(test_file)
        )
        if opt_without_nsz != opt_with_nsz:
            with open(base_path + ".src.nonsz", "w") as f:
                f.write(content_without_nsz)
            with open(base_path + ".src.nsz", "w") as f:
                f.write(content_with_nsz)
            with open(base_path + ".out.nonsz", "w") as f:
                f.write(opt_without_nsz)
            with open(base_path + ".out.nsz", "w") as f:
                f.write(opt_with_nsz)
    except Exception as e:
        print(e)
        pass
    return None


if os.path.exists("report_dir"):
    shutil.rmtree("report_dir")

os.makedirs("report_dir")
pool = Pool(processes=threads)
progress = tqdm.tqdm(work_list)
for test_file in pool.imap_unordered(verify, work_list):
    progress.update()
progress.close()
