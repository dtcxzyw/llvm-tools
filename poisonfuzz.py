import os
import sys
import tqdm
import shutil
import subprocess
from multiprocessing import Pool

# python3 ../poisonfuzz.py ./poisonfuzz ~/WorkSpace/Projects/alive2/build/alive-tv ~/WorkSpace/Projects/compilers/llvm-project/llvm/test/Transforms/InstCombine ./fuzz 1 1
gen = sys.argv[1]
alive_tv = sys.argv[2]
test_dir = sys.argv[3]
work_dir = sys.argv[4]
rounds = int(sys.argv[5])
threads = int(sys.argv[6])

tests = []
#known_issue = set(['select-cmp-cttz-ctlz.ll', 'shift-cttz-ctlz.ll','select-binop-cmp.ll','phi.ll','bit_ceil.ll','ispow2.ll','select.ll'])
known_issue = ['phi.ll', 'select-binop-cmp.ll', 'simplify-libcalls-inreg.ll', 'memcmp-1.ll', 'opaque-ptr.ll', 'intptr2.ll', 'minmax-fp.ll', 'simplify-demanded-fpclass.ll']
known_issue += ['select-cmp-cttz-ctlz.ll', 'shift-cttz-ctlz.ll', 'bit_ceil.ll'] # 112068 112076
known_issue += ['sub-of-negatible.ll', 'sub-of-negatible-inseltpoison.ll'] # 112666
known_issue.append('funnel.ll') # Too complex
known_issue.append('select.ll') # Too complex
known_issue = set(known_issue)
for r,ds,fs in os.walk(test_dir):
    for f in fs:
        if f.endswith('.ll') and f not in known_issue:
            test_path = os.path.join(r, f)
            with open(test_path) as ir:
                val = ir.read()
                if 'volatile' not in val and 'int2ptr' not in val and '@llvm.amdgpu.' not in val and '@llvm.x86.' not in val:
                    # if '@llvm.abs' in val:
                    # if ' icmp ' in val:
                    if 'shr ' in val:
                        tests.append(test_path)


print("Total tests:", len(tests))

if os.path.exists(work_dir):
    shutil.rmtree(work_dir)
os.makedirs(work_dir)

idx = 0

def fuzz(path):
    global idx
    name = str(os.path.basename(path)).removesuffix(".ll")
    file = os.path.join(work_dir, f'{name}-{idx}')
    try:
        subprocess.check_call([gen, path, file], timeout=20.0)
        src = file + '.src'
        tgt = file + '.tgt'
        if not os.path.exists(src) or not os.path.exists(tgt):
            return (path, True)
        ret = subprocess.run([alive_tv, '--smt-to=300', '--disable-undef-input', src, tgt], timeout=20.0, capture_output=True)
        out = ret.stdout.decode('utf-8')
        ok = False

        # if 'incorrect transformations' not in out:
        #     ok = True
        
        if 'Unsupported attribute' in out:
            ok = True
        
        if '0 incorrect transformations' in out:
            ok = True

        if ok:
            os.remove(src)
            os.remove(tgt)
            return (path, True)
    except subprocess.TimeoutExpired:
        os.remove(src)
        os.remove(tgt)
        return (path, True)
    except Exception as e:
        # print(e)
        pass

    return (path, False)

failed = 0
for i in range(rounds):
    idx = i
    dead = []
    with Pool(processes=threads) as pool:
        progress = tqdm.tqdm(tests)
        for path, res in pool.imap_unordered(fuzz, tests):
            if not res:
                dead.append(path)
                failed += 1
            progress.update()
        progress.close()
    for k in dead:
        tests.remove(k)

print(f'Failed: {failed}')
exit(1 if failed != 0 else 0)
