#!/usr/bin/env python3

from multiprocessing import Pool
from tqdm import tqdm
import os
import sys
import subprocess
import shutil
import tqdm
import datetime

csmith_dir = "/home/dtcxzyw/WorkSpace/Projects/compilers/csmith/install/"
test_count = int(sys.argv[1])
csmith_ext = ""
csmith_command = (
    csmith_dir
    + "/bin/csmith --max-funcs 3 --max-block-depth 5 --quiet --builtins --no-packed-struct --no-unions --no-bitfields {}--output ".format(
        csmith_ext
    )
)
common_opts = (
    "-Wno-narrowing -DNDEBUG -g0 -ffp-contract=on -w -I" + csmith_dir + "/include "
)
gcc_command = "clang -O0 " + common_opts
clang_command = (
    "/home/dtcxzyw/WorkSpace/Projects/compilers/LLVM/llvm-build/bin/clang -fsanitize=undefined -O3 -mllvm -inline-threshold=100000 "
    + common_opts
)
exec_timeout = 6.0
exec_qemu_timeout = 10.0
comp_timeout = 30.0

cwd = "build/csmith" + datetime.datetime.now().strftime("%Y-%m-%d@%H:%M")
os.makedirs(cwd)


def build_and_run(basename, file_c, ref_output):
    file_out = basename

    try:
        comp_command = clang_command + " -o " + file_out + " " + file_c
        subprocess.check_call(comp_command.split(" "), timeout=comp_timeout * 10)
    except subprocess.TimeoutExpired:
        # ignore timeout
        return True
    except subprocess.SubprocessError:
        with open(file_out + "_comp.sh", "w") as f:
            f.write(comp_command)
        return False

    try:
        out = subprocess.check_output([file_out], timeout=exec_qemu_timeout)
    except subprocess.TimeoutExpired:
        # ignore timeout
        os.remove(file_out)
        return True
    except subprocess.SubprocessError:
        with open(file_out + "_run.sh", "w") as f:
            f.write(" ".join([file_out]))
        return False

    if out == ref_output:
        os.remove(file_out)
        return True
    else:
        with open(file_out + "_run.sh", "w") as f:
            f.write(" ".join([file_out]))
        return False


def csmith_test(i):
    basename = cwd + "/test" + str(i)
    file_c = basename + ".c"
    try:
        subprocess.check_call((csmith_command + file_c).split(" "))
    except subprocess.SubprocessError:
        return None

    file_ref = basename + "_ref"
    try:
        subprocess.check_call(
            (gcc_command + "-o " + file_ref + " " + file_c).split(" "),
            timeout=comp_timeout,
        )
    except subprocess.SubprocessError:
        os.remove(file_c)
        return None

    try:
        ref_output = subprocess.check_output(file_ref, timeout=exec_timeout)
    except subprocess.SubprocessError:
        os.remove(file_c)
        os.remove(file_ref)
        return None

    result = True

    if not build_and_run(basename, file_c, ref_output):
        result = False

    if result:
        os.remove(file_c)
        os.remove(file_ref)

    return result


L = list(range(test_count))
pbar = tqdm.tqdm(L)
error_count = 0
skipped_count = 0
pool = Pool(16)

for res in pool.imap_unordered(csmith_test, L):
    if res is not None:
        error_count += 0 if res else 1
    else:
        skipped_count += 1

    pbar.set_description(
        "Failed: {} Skipped: {}".format(error_count, skipped_count), refresh=False
    )
    pbar.update(1)
pbar.close()

if error_count == 0:
    shutil.rmtree(cwd)
