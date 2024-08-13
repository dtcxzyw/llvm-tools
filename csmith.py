#!/usr/bin/env python3

from concurrent.futures import ThreadPoolExecutor
from tqdm import tqdm
import os
import sys
import subprocess
import shutil
import tqdm
import datetime

csmith_dir = "/home/dtcxzyw/WorkSpace/Projects/compilers/csmith/install/"
qemu_command = '/home/dtcxzyw/WorkSpace/Projects/compilers/tmp/qemu-install/bin/qemu-riscv64 -L /usr/riscv64-linux-gnu -cpu rv64,zicldst=true,zicond=true'.split()
test_count = int(sys.argv[1])
csmith_ext = ""
csmith_command = csmith_dir +"/bin/csmith --max-funcs 3 --max-block-depth 5 --quiet --builtins --no-packed-struct --no-unions --no-bitfields --no-volatiles --no-volatile-pointers {}--output ".format(
    csmith_ext)
common_opts = "-Wno-narrowing -DNDEBUG -g0 -ffp-contract=on -w -I" + csmith_dir + "/include "
gcc_command = "clang -O0 " + common_opts
clang_command = "/home/dtcxzyw/WorkSpace/Projects/compilers/LLVM/llvm-build/bin/clang -O3 --target=riscv64-linux-gnu " + common_opts
clang_arch_list = [
"rv64gc_zicldst_zicond",
]
exec_timeout = 6.0
exec_qemu_timeout = 30.0
comp_timeout = 30.0

cwd = "build/csmith"+datetime.datetime.now().strftime("%Y-%m-%d@%H:%M")
os.makedirs(cwd)

def build_and_run(arch, basename, file_c, ref_output):
    file_out = basename + "_" + arch.split()[0]

    try:
        comp_command = clang_command + "-march="+arch+" -o "+file_out+" "+file_c
        subprocess.check_call(comp_command.split(' '), timeout=comp_timeout*10)
    except subprocess.SubprocessError:
        with open(file_out+"_comp.sh", "w") as f:
            f.write(comp_command)
        return False
    
    try:
        out = subprocess.check_output(qemu_command+[file_out], timeout=exec_qemu_timeout)
    except subprocess.TimeoutExpired:
        # ignore timeout
        os.remove(file_out)
        return True
    except subprocess.SubprocessError:
        with open(file_out+"_run.sh", "w") as f:
            f.write(" ".join(qemu_command+[file_out]))
        return False
    
    if out == ref_output:
        os.remove(file_out)
        return True
    else:
        with open(file_out+"_run.sh", "w") as f:
            f.write(" ".join(qemu_command+[file_out]))
        return False

def csmith_test(i):
    basename = cwd+"/test"+str(i)
    file_c = basename + ".c"
    try:
        subprocess.check_call((csmith_command+file_c).split(' '))
    except subprocess.SubprocessError:
        return None
    
    file_ref = basename + "_ref"
    try:
        subprocess.check_call((gcc_command+"-o "+file_ref+" "+file_c).split(' '),timeout=comp_timeout)
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
    for arch in clang_arch_list:
        if not build_and_run(arch, basename, file_c, ref_output):
            result = False

    if result:
        os.remove(file_c)
        os.remove(file_ref)

    return result


L = list(range(test_count))
pbar = tqdm.tqdm(L)
error_count = 0
skipped_count = 0

with ThreadPoolExecutor(max_workers=16) as p:
    for res in p.map(csmith_test, L):
        if res is not None:
            error_count += 0 if res else 1
        else:
            skipped_count += 1

        pbar.update(1)
        pbar.set_description("Failed: {} Skipped: {}".format(
            error_count, skipped_count))

if error_count == 0:
    shutil.rmtree(cwd)
