"""Map code and data addresses of one ALIENSP2.EXE build to another
(English 1.03US -> French 1.02F).

Every function of the reference build (functions.tsv) is searched in the
other build as a byte pattern with its relocated operands (LE fixups) and
rel32 branch targets masked out.  A function that is found exactly once is
matched; its fixups, read at the same offsets in both builds, then pair up
the data addresses the two builds use.

usage: xmap.py REF.EXE ref_functions.tsv OTHER.EXE out.tsv [addr ...]
  out.tsv: <ref func> <other func>  for every uniquely matched function
  each extra addr (relocated hex) is translated and printed
"""
import sys, os, re, struct, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from le import LE

RELOC = 0x400000


def masked(le, code_obj):
    """code bytes of the object + a mask (1 = byte depends on addresses)"""
    lo = code_obj.base - le.lo
    n = code_obj.vsize
    mem = bytes(le.mem[lo:lo + n])
    m = bytearray(n)
    for (sl, st, tgt, tobj) in le.fixups:
        i = sl - code_obj.base
        w = 4 if st in (7, 8) else 2
        if 0 <= i and i + w <= n:
            for k in range(w):
                m[i + k] = 1
    return mem, m


def mask_branches(mem, m, start, end):
    """mask rel32 of call/jmp/jcc inside [start, end) (simple linear scan
    is not reliable on x86, so only look at E8/E9/0F8x followed by a target
    that falls inside the code object) - done by the caller per hit"""


def pattern(mem, m, a, b, base, size):
    out = []
    i = a
    while i < b:
        c = mem[i]
        # call/jmp rel32 whose target lands in the code object: mask the rel
        if c in (0xe8, 0xe9) and i + 5 <= b and not any(m[i:i + 5]):
            rel = struct.unpack_from('<i', mem, i + 1)[0]
            t = base + i + 5 + rel
            if base <= t < base + size:
                out.append(re.escape(bytes([c])) + b'....')
                i += 5
                continue
        if c == 0x0f and i + 6 <= b and 0x80 <= mem[i + 1] <= 0x8f and not any(m[i:i + 6]):
            rel = struct.unpack_from('<i', mem, i + 2)[0]
            t = base + i + 6 + rel
            if base <= t < base + size:
                out.append(re.escape(mem[i:i + 2]) + b'....')
                i += 6
                continue
        out.append(b'.' if m[i] else re.escape(bytes([c])))
        i += 1
    return b''.join(out)


def main():
    ref_exe, ftsv, oth_exe, out = sys.argv[1:5]
    extra = [int(x, 16) for x in sys.argv[5:]]
    A, B = LE(ref_exe), LE(oth_exe)
    ca = next(o for o in A.objs if o.exec)
    cb = next(o for o in B.objs if o.exec)
    amem, am = masked(A, ca)
    bmem, bm = masked(B, cb)
    funcs = []
    for line in open(ftsv):
        f = line.split('\t')
        s, e = int(f[0], 16) + RELOC, int(f[1], 16) + RELOC
        if ca.base <= s < ca.base + ca.vsize:
            funcs.append((s, min(e + 1, ca.base + ca.vsize)))
    funcs.sort()
    fmap = {}
    for s, e in funcs:
        a, b = s - ca.base, e - ca.base
        # long functions: the first 160 bytes are enough to be unique
        pat = pattern(amem, am, a, min(b, a + 160), ca.base, ca.vsize)
        hits = [h.start() for h in re.finditer(pat, bmem, re.S)]
        if len(hits) != 1 and b - a > 160:
            pat = pattern(amem, am, a, b, ca.base, ca.vsize)
            hits = [h.start() for h in re.finditer(pat, bmem, re.S)]
        if len(hits) == 1:
            fmap[s] = (cb.base + hits[0], e - s)
    # data/code address pairs from fixups at the same offsets
    afix = {sl: tgt for (sl, st, tgt, tobj) in A.fixups if st == 7}
    bfix = {sl: tgt for (sl, st, tgt, tobj) in B.fixups if st == 7}
    pairs = collections.defaultdict(collections.Counter)
    for s, (t, n) in fmap.items():
        for k in range(n):
            if s + k in afix and t + k in bfix:
                pairs[afix[s + k]][bfix[t + k]] += 1
    amap = {}
    for x, c in pairs.items():
        amap[x] = c.most_common(1)[0][0]
    for s, (t, n) in fmap.items():
        amap.setdefault(s, t)
    with open(out, 'w') as f:
        for s in sorted(fmap):
            f.write('%x\t%x\t%d\n' % (s, fmap[s][0], fmap[s][1]))
    print('functions: %d of %d matched; address pairs: %d' % (len(fmap), len(funcs), len(amap)), file=sys.stderr)
    keys = sorted(amap)
    import bisect
    for x in extra:
        if x in amap:
            print('%x -> %x  (exact)' % (x, amap[x]))
            continue
        # nearest known address below, same object
        i = bisect.bisect_right(keys, x) - 1
        j = i + 1
        cand = []
        if i >= 0: cand.append(keys[i])
        if j < len(keys): cand.append(keys[j])
        best = min(cand, key=lambda k: abs(k - x))
        print('%x -> %x  (via %x%+d; %s)' % (x, amap[best] + (x - best), best, x - best,
                                            'func' if best in fmap else 'data'))


if __name__ == '__main__':
    main()
