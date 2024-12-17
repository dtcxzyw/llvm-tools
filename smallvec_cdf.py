import json
import numpy
from matplotlib import pyplot as plt

prefix = "/home/dtcxzyw/WorkSpace/Projects/compilers/llvm-project/"
threshold = 1000000
with open('build/smallvec_dist.json', 'r') as f:
    dist = json.load(f)

advs = dict()
for k, v in dist.items():
    k = k.removeprefix(prefix)
    arr = []
    sum = 0
    pairs = []
    p = 0.95
    cnt_sum = 0
    for size, cnt in v.items():
        pairs.append((int(size), cnt))
        cnt_sum += cnt
    if cnt_sum < threshold:
        continue
    tgt_sum = cnt_sum * p
    pairs.sort(key=lambda x: x[0])
    dest = None
    for size, cnt in pairs:
        while len(arr) <= size:
            arr.append(sum)
        sum += cnt
        if dest is None and sum >= tgt_sum:
            dest = size
        arr[size] = sum
    default = int(k.split(' ')[1])
    if dest <= default:
        continue
    dest = int(2**numpy.ceil(numpy.log2(dest)))
    p0 = arr[default]/cnt_sum*100
    p1 = arr[min(dest, len(arr) - 1)]/cnt_sum*100
    key = k[:k.find('.cpp')]
    adv = (k, default, p0, dest, p1, cnt_sum)
    if key in advs:
        advs[key].append(adv)
    else:
        advs[key] = [adv]
    # suggest = f"{k}(p = {p0:.2f}) suggested size {dest}(p = {p1:.2f})"
    # print(suggest)
    # plt.title(suggest)
    # y = numpy.array(arr)
    # x = numpy.arange(len(y))
    # plt.plot(x, y, label=k)
    # plt.axvline(default, color='red')
    # plt.axvline(dest, color='green')
    # plt.waitforbuttonpress()
    # plt.clf()

for key, val in advs.items():
    val.sort(key=lambda x: x[4]/max(0.001,x[2]))
    for v in val:
        print(f"{v[0]}(p = {v[2]:.2f}) suggested size {v[3]}(p = {v[4]:.2f}) samples = {v[5]}")
