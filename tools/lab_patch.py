#!/usr/bin/env python3
"""Dump a feature's static writes as `addr hexbytes` lines (for the Dolphin lab's `patch` command)."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..', 'src'))
import features
region, out = sys.argv[1], sys.argv[2]
f = features.load('pad', region)
with open(out, 'w') as fh:
    for a, d in sorted(f.writes()):
        for i in range(0, len(d), 0x200):
            fh.write('%X %s\n' % (a + i, d[i:i + 0x200].hex()))
print(out)
