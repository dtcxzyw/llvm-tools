import os
import subprocess
import sys
import tqdm
from multiprocessing import Pool

llc_exec = sys.argv[1]
dataset = sys.argv[2]

flags = ['-O3', '-mtriple=riscv64', '-mattr=+m,+a,+f,+d,+c']
skip_list = [
    'f4005d2d007f5217.ll', # half ARG
    'e14d47edb181ff07.ll',
    '5e855016b7e0cb88.ll',
    '40839983bb22e8fc.ll',
    'adb15aa867720f4f.ll',
    '98fbf0f80b26fa66.ll',
    '4ddd4e19163ad8d9.ll',
    '3e0d19923bc022da.ll',
    'a0356dbf241e45bc.ll',
    'b3cadb7cfce172f2.ll',
    '3c55ee9dc70eb3a6.ll',
    'ec50e18d65bcd37e.ll',
    '530623c58ad09fe0.ll', # s128 G_UITOFP G_SITOFP G_FPTOSI
    '9f62f0cad1cfd56a.ll',
    'a9b59b4f5959fa2d.ll',
    '3af3f1ff15f22939.ll',
    'bf6f61785c891091.ll',
    'eb72f881c5b81c0f.ll',
    'fc397b83b8a3158d.ll',
    'e6a736437fcd48df.ll',
    '4676f6c73ade6612.ll',
    '6e82cb32f3feb907.ll',
    '53a77aeafbdf8f74.ll',
    '83a4531a113f82f9.ll',
    '794b44439831b048.ll',
    'e06997b283e35460.ll',
    '003fa228f00a8db8.ll',
    '89f749d200b9d17f.ll', # s64 G_UDIVREM G_SDIVREM
    '2ba04ef83e7c3709.ll',
    '305553eaec928d67.ll',
    '7d923920e5a78177.ll',
    '57cd99ffbbc24d11.ll',
    '2d98bd5358331c6d.ll',
    '6cc5f5e5bbff5d75.ll',
    'a96301703bf8dd4f.ll',
    '5a89c155902c883d.ll',
    '41f14355b4d3bd0c.ll',
    'a4ab16d80b783ef8.ll',
    'd57553fa1982a765.ll',
    'b9ad2e36d71bf6db.ll',
    '063f380a596290f0.ll',
    'ef680758965461c8.ll', # G_CTPOP
    'fb5ef30e606d8f75.ll',
    'dfbaa00f5779c29e.ll',
    'dde7f81c9c9d6425.ll', # G_CTTZ
    '8f4ef79c4ac72c2d.ll',
    '5669ad57bb09c1b6.ll', # G_FPEXT
    '925d24abe5b44ff6.ll', # s16 G_FCMP (olt)
]

def test_gen(file, gisel):
    return subprocess.check_output([llc_exec, file] + flags + (['-global-isel'] if gisel else []) + ['-o', '/dev/stdout']).decode('utf-8')

def test(file):
    try:
        res1 = test_gen(file, False)
        res2 = test_gen(file, True)
        if res1 != res2:
            print('Mismatch:', file)
            print('Without GISel:')
            print(res1)
            print('With GISel:')
            print(res2)
            return False
        return True
    except Exception as e:
        print('Failed:', file, e)
        return False
    
pool = Pool(processes=16)

if __name__ == '__main__':
    work_list = []
    for file in os.listdir(dataset):
        if file.endswith('.ll'):
            path = os.path.join(dataset, file)
            is_skipped = False
            for skip in skip_list:
                if skip in path:
                    is_skipped = True
                    break
            if not is_skipped:
                work_list.append(path)

    # progress = tqdm.tqdm(work_list, mininterval=0.5, maxinterval=1, miniters=1)

    # for res in pool.imap_unordered(test, work_list):
    #     progress.update()
    #     if not res:
    #         sys.exit(1)

    for file in work_list:
        if not test(file):
            sys.exit(1)
