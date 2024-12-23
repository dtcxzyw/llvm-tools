import json

# https://gist.github.com/dtcxzyw/14406ad0e186072a115071cdb2701695
dist = json.load(open("build/timing.json"))
arr = list(dist.items())
arr.sort(key = lambda x: x[1], reverse=True)
total = sum(map(lambda x: x[1], arr))
acc = 0
threshold = 0.999
for k, v in arr:
    if acc / total > threshold:
        continue
    acc += v
    print(f"{k} {v:.2f}s ({v/total*100:.2f}%/{acc/total*100:.2f}%)")
