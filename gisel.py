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
    'd53af796f739f249.ll', # i128 G_CONSTANT_FOLD_BARRIER
    'd9a96e86552430aa.ll',
    '3e9eda0623e1077c.ll',
    'ebbdacf285ced69f.ll',
    'f0d29dde1d9e8a01.ll',
    '4f5a3658066ed1c9.ll',
    'e2e4dae28ff5c32d.ll',
    '25b2a720c2981a2b.ll',
    'c34993c9b79842eb.ll',
    '7c783ec24f76e21e.ll',
    'e018bafe32c8a03b.ll',
    '7aaa4ecc0ae3110d.ll',
    'f6a530ff6232c41b.ll',
    'ef7f6e6abe93e7ac.ll',
    '5325b5af73e47044.ll',
    'c62769df955cde7a.ll',
    'e0b0208ff170082e.ll',
    'df5cea3342a416c9.ll',
    '29dc0233d03d2215.ll',
    '8beab78855f817bd.ll',
    '730fe1d53f316ab0.ll',
    '699ce8ab26418382.ll',
    '1836a6244aefd948.ll',
    '8df08295457e86bf.ll',
    '5d15c5684cbc6dd4.ll',
    '19013b462b158090.ll',
    '65b314488c726bc6.ll',
    '7be1ec5e808a2482.ll',
    'ff4fe9dad78ced97.ll',
    '3236037936113da9.ll',
    '4b46da2a815b21e9.ll',
    'd3cc9b8f8cf505e7.ll',
    'b70fc46dd3e91d36.ll',
    '0027a7be4206ca9f.ll',
    '9992aa849e30efd3.ll', # i16 G_CONSTANT_FOLD_BARRIER
    'd99063d82f37ff14.ll',
    'd14ef46401fbc5d7.ll',
    'e377109d22d25060.ll',
    'e511469416b4c687.ll',
    '40d7dec55830ce24.ll',
    '96086f426b979942.ll',
    '49840c043de0b80b.ll',
    '471ca563f064cb0c.ll',
    'ceb3277058ca9b95.ll',
    '7166915e284939b4.ll', # i48 G_LSHR
    '487c1e28012c9c89.ll',
    '995a26a466872607.ll',
    '9fa1e758cf77f7c8.ll',
    'ea90742876ea82ea.ll',
    '3b9f76bfa186da6c.ll',
    '10a0e1557af81826.ll',
    '0dd16f0e49117d35.ll',
    '82774c3d8602fe14.ll',
    '1e171541ef7ac305.ll',
    'd1741c3fd8ed55cd.ll',
    '5182b692d021b324.ll',
    '752bd6ab89ce5c44.ll',
    'e468b459d70d29be.ll',
    'f4448f270e0d103e.ll',
    'dfd8b7c2a70fa520.ll',
    'ce4d42afb97304c4.ll',
    '75b1ac514e02322e.ll',
    'b09005387338bde5.ll',
    '7eb15ba3fc2b1d8b.ll',
    '4e521f5cb96776bc.ll',
    '6f559e6ee969e5f2.ll',
    '3074a6a0f8b7fbdd.ll',
    '9c2df43fdc7b4358.ll',
    '9b2a2611b184b4b2.ll',
    '4e36814205d704cd.ll',
    'fc5bfd2cf6f455e7.ll',
    'c8ef39eaa8ce9d58.ll',
    '0ba3c1d5c2264349.ll',
    '7be384eeea404c0a.ll',
    '566ff018d0126689.ll',
    '66f97ea9fe15215b.ll',
    'b960e08ada9b03c5.ll',
    'd64830c9bcb32be9.ll',
    'f0e43f2b052d4b7f.ll',
    '816a3db9d55efcee.ll',
    '31e284e83349a2ba.ll',
    '05a94c096794b122.ll',
    'a6823a1ff185336b.ll',
    '6b5df36396a9b80e.ll',
    'cfaf05a83f5cf85b.ll',
    'ecf87ad09dcbe969.ll',
    'e71f3bc05b95ba1d.ll',
    '7d8ad59972e92426.ll',
    '5c67b43a7dd0b020.ll',
    '669ac9a0202bac1f.ll',
    'a884fec1143b54f4.ll',
    '14c65655e0dcd8ef.ll',
    '169134be9f7f28b2.ll',
    '7153e881ac452ac2.ll',
    'ce03905a0c22b8b5.ll',
    'd9b088669f2e0caa.ll',
    '294047662a6c691c.ll',
    'c7a096a8d48bc4c1.ll',
    '8af1e88f2815b75f.ll',
    'd2d4a8aa8e5b7667.ll',
    'b7e92f89f0c2b349.ll',
    'ee4b953384607ad6.ll',
    '6b0727ea7467c6e3.ll',
    '57b8a6246e9e6161.ll',
    'cc0bce7ac60db12b.ll',
    '93c6582af3662d4d.ll',
    '08a58d1390eeac07.ll',
    'ce968ef25ff0fcc2.ll',
    '832bfbc2eea77bb1.ll',
    'd8cafc4bc6caa315.ll',
    '7cf476ac6f23f232.ll',
    '1af1212829b1da7b.ll',
    '2432089d368c5eea.ll',
    '520acb2c732cbb5f.ll',
    '0f968e1b9424dbd6.ll',
    '7bd103f153c220fb.ll',
    'e7f8c0c5f10fe949.ll',
    '884a4d685f0ed363.ll',
    '3cf4cbaa90f6fc7b.ll',
    '21ef03ca292e5cb4.ll',
    '96f8abd1d3dc1e93.ll',
    '434a00bd1c9b74d9.ll',
    'd00b82429ebc49dd.ll',
    'b3bdbff474383066.ll',
    '851454e123439ffd.ll',
    '860d51d86ad839f8.ll',
    '5235f1cdae0f1d71.ll',
    'c0ce562914bab56e.ll',
    '86121e7380099440.ll',
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
    '4733fda29c9f38d0.ll', # Crash
    'fb5ef30e606d8f75.ll',
    'c9a0dc66985781b7.ll',
    'c3460b1031e32520.ll',
    'f7a7ed20d7926486.ll',
    '8f4ef79c4ac72c2d.ll',
    '5b0b73287b05e115.ll',
    '334be4bbccbc7e30.ll',
    'b62c9d35b3cba33e.ll',
    'eff0a39f7b60438f.ll',
    'dfbaa00f5779c29e.ll',
    'dde7f81c9c9d6425.ll',
    '422df1776def92fb.ll',
    'ef680758965461c8.ll',
    '5ec5559580903376.ll', # G_FREM
    'df8eb60acc2def80.ll',
    '5669ad57bb09c1b6.ll', # G_FPEXT
    '925d24abe5b44ff6.ll', # s16 G_FCMP (olt)
]

def test_gen(file, gisel):
    return subprocess.check_output([llc_exec, file] + flags + (['-global-isel'] if gisel else []) + ['-o', '/dev/null'])

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

    progress = tqdm.tqdm(work_list, mininterval=0.5, maxinterval=1, miniters=1)

    for res in pool.imap_unordered(test, work_list):
        progress.update()
        #if not res:
        #    sys.exit(1)
