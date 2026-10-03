"""Build the controller feature for one release from src/pad.c and src/hooks.S.

Needs devkitPPC.  The USA addresses are written down below; the other releases
are found by masked-signature search (tools/sig.py), which must match exactly
once or the build stops.
"""
import os
import re
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
from layout import PAD_BASE, PAD_END
from ops import Feature, Hook
from sig import find_unique

DEVKIT = os.environ.get('DEVKITPPC', '/opt/devkitpro/devkitPPC')
CC = DEVKIT + '/bin/powerpc-eabi-'

USA_DOL = None                       # set by gen_prebuilt.py

# (name, USA address, words of context before / after used to find it elsewhere)
SITES = {
    'POLL':   (0x8040DBC0, 0, 14),   # KPADiRead entry:                 stwu r1,-0xc0(r1)
    'SAMPLE': (0x8040DCC4, 6, 8),    # KPADiRead, queued-sample check:  lbz r0,0x10f(r31)
    'POST':   (0x8040E27C, 8, 6),    # KPADiRead, status copy:          addi r5,r14,0x80
    'PROBE':  (0x80411B60, 0, 14),   # WPADProbe entry:                 stwu r1,-0x10(r1)
}
ORIG = {'POLL': 0x9421FF40, 'SAMPLE': 0x881F010F, 'POST': 0x38AE0080, 'PROBE': 0x9421FFF0}
# helper functions the C code calls (USA address, instructions to match)
FN = {
    'SIGETTYPE': (0x803E28F8, 0, 16),
    'OSDISABLE': (0x803DA278, 0, 5),
    'OSRESTORE': (0x803DA2A0, 0, 6),
}
SI_USA = dict(types=0x80539818, busy=0x80539800, shadow=0x80539804)
WPAD_TBL_USA = 0x805AF270
WPAD_STATUS = 0x7FC
WPAD_EXT = 0x801
STATE = 0x80005D40                   # zeroed padding in every release's init section


def _w(dol, a):
    return struct.unpack('>I', dol.read(a, 4))[0]


def _pair(dol, start, n, reg_hi_op=0x3C00, lo_ops=(0x3800, 0x6000)):
    """Find `lis rX,hi` followed by `addi/ori rX,rX,lo` in n words from start."""
    for i in range(n):
        w = _w(dol, start + 4 * i)
        if (w >> 26) != 15:
            continue
        rd = (w >> 21) & 31
        for j in range(i + 1, min(n, i + 8)):
            x = _w(dol, start + 4 * j)
            if (x >> 26) in (14, 24) and (x >> 21) & 31 == rd and (x >> 16) & 31 == rd:
                lo = x & 0xFFFF
                if (x >> 26) == 14 and lo & 0x8000:
                    lo -= 0x10000
                return ((((w & 0xFFFF) << 16) + lo) & 0xFFFFFFFF)
    raise SystemExit('no lis/addi pair near 0x%08X' % start)


def resolve(region, dol):
    usa = USA_DOL if region != 'RRSE4Q' else dol
    out = {}
    for k, (addr, b, a) in SITES.items():
        out[k] = addr if usa is dol else find_unique(usa, dol, addr, b, a)
        if _w(dol, out[k]) != ORIG[k]:
            raise SystemExit('%s: site %s at 0x%08X holds 0x%08X, expected 0x%08X' % (region, k, out[k], _w(dol, out[k]), ORIG[k]))
    fn = {k: (addr if usa is dol else find_unique(usa, dol, addr, b, a)) for k, (addr, b, a) in FN.items()}
    if usa is dol:
        si = dict(SI_USA)
        wpad = WPAD_TBL_USA
    else:
        # SIGetType loads the type table with `lis r5; addi r5` near its start
        types = _pair(dol, fn['SIGETTYPE'] + 0x10, 12)
        si = dict(types=types, busy=types - 0x18, shadow=types - 0x14)
        wpad = _pair(dol, out['PROBE'], 8)
    # sanity: the same relations the USA build has
    assert si['busy'] == si['types'] - 0x18 and si['shadow'] == si['busy'] + 4
    # WPADProbe reads the status and extension type at fixed offsets of the block
    probe = [_w(dol, out['PROBE'] + 4 * i) for i in range(40)]
    assert any((w >> 26) == 32 and (w & 0xFFFF) == WPAD_STATUS for w in probe), 'WPAD status offset'
    assert any((w >> 26) == 34 and (w & 0xFFFF) == WPAD_EXT for w in probe), 'WPAD ext offset'
    out.update(fn=fn, si=si, wpad=wpad)
    return out


def compile_hook(name, defs):
    tmp = tempfile.mkdtemp(prefix='robpad')
    D = ['-D%s=%s' % kv for kv in defs.items()] + ['-DHOOK_' + name]
    if os.environ.get('DEBUG_FEED'):
        D.append('-DDEBUG_FEED')
    cflags = ['-O2', '-fno-unroll-loops', '-mbig-endian', '-msoft-float', '-msdata=none', '-ffreestanding',
              '-fno-pic', '-fno-asynchronous-unwind-tables', '-fno-stack-protector', '-nostdlib', '-Wall', '-Werror']
    subprocess.check_call([CC + 'gcc'] + cflags + D + ['-c', HERE + '/pad.c', '-o', tmp + '/g.o'])
    subprocess.check_call([CC + 'gcc', '-mbig-endian', '-c', '-x', 'assembler-with-cpp'] + D +
                          [HERE + '/hooks.S', '-o', tmp + '/h.o'])
    subprocess.check_call([CC + 'ld', '-T', HERE + '/link.ld', '-o', tmp + '/b.elf', tmp + '/h.o', tmp + '/g.o'])
    subprocess.check_call([CC + 'objcopy', '-O', 'binary', tmp + '/b.elf', tmp + '/b.bin'])
    b = open(tmp + '/b.bin', 'rb').read()
    return list(struct.unpack('>%dI' % (len(b) // 4), b))


def build(region, dol):
    a = resolve(region, dol)
    defs = {
        'STATE': '0x%08Xu' % STATE,
        'SI_TYPES': '0x%08Xu' % a['si']['types'],
        'SI_BUSY': '0x%08Xu' % a['si']['busy'],
        'SI_SHADOW': '0x%08Xu' % a['si']['shadow'],
        'FN_SIGETTYPE': '0x%08Xu' % a['fn']['SIGETTYPE'],
        'FN_OSDISABLE': '0x%08Xu' % a['fn']['OSDISABLE'],
        'FN_OSRESTORE': '0x%08Xu' % a['fn']['OSRESTORE'],
        'WPAD_TBL': '0x%08Xu' % a['wpad'],
        'WPAD_STATUS': '0x%X' % WPAD_STATUS,
        'WPAD_EXT': '0x%X' % WPAD_EXT,
    }
    notes = {
        'POLL': 'KPADiRead entry: poll the GameCube ports through the Serial Interface',
        'SAMPLE': 'KPADiRead: GameCube pad / Classic Controller -> Nunchuk sample',
        'POST': 'KPADiRead: C-stick pointer',
        'PROBE': 'WPADProbe: report a Nunchuk for a pad or a Classic Controller',
    }
    ops, cur = [], PAD_BASE
    for name in ('POLL', 'SAMPLE', 'POST', 'PROBE'):
        w = compile_hook(name, defs)
        assert w[-1] == 0x60000000, name           # the nop the hook installer turns into the branch back
        w[-1] = 0
        ops.append(Hook(a[name], ORIG[name], w, cur, note=notes[name]))
        cur += (len(w) * 4 + 15) & ~15
    if cur > PAD_END:
        raise SystemExit('pad code overflows its window: 0x%X > 0x%X' % (cur, PAD_END))
    return Feature('pad', 'GameCube and Classic Controller', region, ops)
