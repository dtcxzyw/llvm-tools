import os
import sys
import subprocess
import tqdm

alive_tv = sys.argv[1]
path = sys.argv[2]

work_list = []

for r,ds,fs in os.walk(path):
    for f in fs:
        if f.endswith('.ll'):
            test_file = os.path.join(r,f)
            work_list.append(test_file)

os.makedirs('report_dir', exist_ok=True)
with open('alive2.log', 'w') as f:
    for test_file in tqdm.tqdm(work_list):
        subprocess.run([alive_tv, '--smt-to=300', '--disable-undef-input', '--passes=instcombine', '--save-ir', '--report-dir=report_dir', test_file],stdout=f,stderr=f)
