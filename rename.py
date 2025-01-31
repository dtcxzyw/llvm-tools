import unidiff
import os
import sys

file = sys.argv[1]
diff = open(file).read()
patch = unidiff.PatchSet(diff)


def extract_name(line):
    if line.startswith("%") or line.startswith("@") or line.startswith("!"):
        pos = line.find(" =")
        if pos != -1:
            return (line[0], line[:pos].strip())
        return None
    pos = line.find(":")
    if pos != -1:
        return (":", line[:pos].strip())
    return None


for file in patch:
    mapping = dict()
    for hunk in file:
        added = dict()
        removed = dict()
        src_lineno = 0
        tgt_lineno = 0
        for line in hunk:
            if line.is_added:
                added[tgt_lineno] = line.value
                tgt_lineno += 1
            if line.is_removed:
                removed[src_lineno] = line.value
                src_lineno += 1
            if line.is_context:
                src_lineno = tgt_lineno = max(src_lineno, tgt_lineno) + 1
        print("===================")
        print(added)
        print(removed)
        for k, v in added.items():
            if k in removed:
                a = extract_name(removed[k].strip())
                b = extract_name(v.strip())
                if a is not None and b is not None and a[0] == b[0] and a[1] != b[1]:
                    if b[1] not in mapping:
                        mapping[b[1]] = {a[1]}
                    else:
                        mapping[b[1]].add(a[1])
    print(file.source_file, mapping)
    exit(0)
