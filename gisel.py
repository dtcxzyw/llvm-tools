import os
import subprocess
import sys

llc_exec = sys.argv[1]
dataset = sys.argv[2]

flags = ['-O3', '-mtriple=riscv64', '-mattr=+m,+a,+f,+d,+c']

def test_gen(file, gisel):
    return subprocess.check_output([llc_exec, file] + flags + (['-global-isel'] if gisel else []))

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
    
if __name__ == '__main__':
    for file in os.listdir(dataset):
        if file.endswith('.ll'):
            path = os.path.join(dataset, file)
            if not test(path):
                sys.exit(1)
