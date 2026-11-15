#!/usr/bin/env python3
"""Build a MOCK data directory (synthetic art, real table values from spec/10 appendix) so the game
logic can be smoke-tested without the original game files. Usage: tests/mkdata.py OUTDIR"""
import struct, sys, os, re

here = os.path.dirname(os.path.abspath(__file__))
spec = next(p for p in (os.path.join(here, '..', 'spec'), os.path.join(here, '..', '..', 'spec')) if os.path.isdir(p))   # repo root, or the engine folder beside spec/

def ne_file(resources, shift=9):
    """resources: list of (type_id_with_bit15, id, bytes). Returns file bytes."""
    by_type = {}
    for t, i, b in resources:
        by_type.setdefault(t, []).append((i, b))
    ne_off = 0x80
    hdr_size = 0x40
    rt_off = hdr_size                       # relative to NE header start
    n_entries = sum(len(v) for v in by_type.values())
    table_size = 2 + sum(8 + 12 * len(v) for v in by_type.values()) + 2
    data_start = ne_off + rt_off + table_size
    data_start = (data_start + (1 << shift) - 1) >> shift << shift
    pos = data_start
    table = struct.pack('<H', shift)
    blobs = []
    for t, items in by_type.items():
        table += struct.pack("<HHI", t | 0x8000, len(items), 0)
        for i, b in items:
            padded = (len(b) + (1 << shift) - 1) >> shift << shift
            table += struct.pack('<HHHHI', pos >> shift, padded >> shift, 0x1030, 0x8000 | i, 0)
            blobs.append((pos, b, padded))
            pos += padded
    table += struct.pack('<H', 0)
    out = bytearray(pos)
    out[0:2] = b'MZ'
    struct.pack_into('<I', out, 0x3C, ne_off)
    out[ne_off:ne_off + 2] = b'NE'
    struct.pack_into('<H', out, ne_off + 0x24, rt_off)
    struct.pack_into('<H', out, ne_off + 0x26, rt_off + len(table))
    out[ne_off + rt_off: ne_off + rt_off + len(table)] = table
    for p, b, padded in blobs:
        out[p:p + len(b)] = b
    return bytes(out)

def nametable(names):
    out = b''
    for ordn, (tn, idn) in names:
        e = struct.pack('<HHH', 0, 0xFF00 | ordn, 1) + tn.encode() + b'\0' + idn.encode() + b'\0'
        e = struct.pack('<H', len(e)) + e[2:]
        out += e
    return out + struct.pack('<H', 0)

def rle_cel(w, h, color_fn):
    rows = b''
    for y in range(h):
        px = bytes(color_fn(x, y) for x in range(w))
        x = 0
        while x < w:
            n = min(254, w - x)
            rows += bytes([n]) + px[x:x + n]
            x += n
    return struct.pack('<HHH', 4, w, h) + rows

def aseq(cels, seqs, fps=10):
    """cels: list of cel bytes; seqs: list of list of (a,b,c,d)."""
    head = struct.pack('<HHHHH', len(seqs), len(cels), fps, 1, 1)
    offs = []
    p = 0
    for c in cels:
        offs.append(p)
        p += len(c)
    head += b''.join(struct.pack('<I', o) for o in offs)
    head += b'\0\0\0\0' * len(seqs)
    for s in seqs:
        head += struct.pack('<H', len(s)) + b''.join(struct.pack('<hhhh', *e) for e in s)
    return head + b''.join(cels)

def still(w, h, fn):
    return aseq([rle_cel(w, h, fn)], [[(0, 0, 0, 0), (-1, -1, -1, -1)]])

def menu_res(popups):
    """popups: [(title, [(flags, id, text)])] -> a Win16 MENU resource as menu.c reads it"""
    b = struct.pack('<HH', 0, 0)
    for pi, (title, items) in enumerate(popups):
        b += struct.pack('<H', 0x10 | (0x80 if pi == len(popups) - 1 else 0)) + title.encode() + b'\0'
        for ii, (fl, mid, txt) in enumerate(items):
            b += struct.pack('<HH', fl | (0x80 if ii == len(items) - 1 else 0), mid) + txt.encode() + b'\0'
    return b

def parse_appendix():
    txt = open(os.path.join(spec, '10-appendix-tables.md')).read()
    hot = []
    for m in re.finditer(r'^area\s+(\d+) slot (\d)\s*: \(([^)]*)\)', txt, re.M):
        hot.append((int(m.group(1)), int(m.group(2)), [int(x) for x in m.group(3).split(',')]))
    anim = {}
    for m in re.finditer(r'^state\s+(\d+): n=(\d+) cels=\[([^\]]*)\] ticks=(\d+)/(\d+)', txt, re.M):
        anim[int(m.group(1))] = (int(m.group(2)), [int(x) for x in m.group(3).split(',') if x.strip()], int(m.group(4)), int(m.group(5)))
    objs = []
    for m in re.finditer(r'^obj (\d) [^:]*: \(([^)]*)\)', txt, re.M):
        objs.append([int(x) for x in m.group(2).split(',')])
    return hot, anim, objs

def bina():
    hot, anim, objs = parse_appendix()
    b = bytearray(1914)
    for a, s, f in hot:
        for k, v in enumerate(f):
            struct.pack_into('<h', b, (a * 4 + s) * 14 + k * 2, v)
    for st, (n, cels, t, tt) in anim.items():
        o = 0x380 + st * 26
        struct.pack_into('<h', b, o, n)
        for k, c in enumerate(cels[:9]):
            struct.pack_into('<h', b, o + 4 + k * 2, c)
        struct.pack_into('<hh', b, o + 22, t, tt)
    for i, f in enumerate(objs):
        for k, v in enumerate(f):
            struct.pack_into('<h', b, 0x70E + i * 18 + k * 2, v)
    return bytes(b)

def main(outdir):
    os.makedirs(outdir, exist_ok=True)
    # --- SSO1
    sizes = {}
    for c in range(213): sizes[c] = (24, 40)
    for c in range(64, 74): sizes[c] = (8, 13)
    sizes[74] = sizes[75] = sizes[76] = sizes[77] = (50, 20)
    for c in range(137, 175): sizes[c] = (13, 223)
    for c in range(134, 137): sizes[c] = (125, 130)
    for c in range(117, 122): sizes[c] = (120, 28)
    sizes[58] = (7, 252); sizes[59] = (7, 251); sizes[122] = (40, 100); sizes[123] = (30, 230); sizes[195] = (50, 60)
    for c in range(78, 98): sizes[c] = (41, 29)
    cels = []
    for c in range(213):
        w, h = sizes[c]
        base = 10 + (c * 7) % 230
        cels.append(rle_cel(w, h, lambda x, y, base=base: base + ((x // 4 + y // 4) & 3)))
    a200 = aseq(cels, [[(0, 0, 0, 0), (-1, -1, -1, -1)]])
    a210 = aseq([rle_cel(24, 30, lambda x, y: 100 + (x & 3)) for _ in range(20)], [[(0, 0, 0, 0), (-1, -1, -1, -1)]])
    res1 = [(0x7F01, 200, a200), (0x7F01, 210, a210),
            (0x7F01, 1000, still(512, 384, lambda x, y: 4 if (9 <= x < 503 and 35 <= y < 284) is False else 0)),
            (0x7F01, 1007, aseq([rle_cel(66, 54, lambda x, y: 200), rle_cel(66, 54, lambda x, y: 210)], [[(0, 0, 0, 0), (-1, -1, -1, -1)]])),
            (0x7F01, 1022, still(74, 115, lambda x, y: 50)),
            (0x7F02, 5000, bina()),
            (0x7F01, 1011, aseq([rle_cel(307, 115, lambda x, y: 77 if (x // 8 + y // 8) % 2 else 66)],
                [[(102, -75 + 5 * i, 0, 0) for i in range(21)] + [(0, 0, -4, 0), (102, 30, 0, 0), (0, 0, -1, 0)], [(102, 30 - 5 * i, 0, 0) for i in range(22)] + [(0, 0, -1, 0)]], fps=20)),
            (0x7F05, 128, b''.join(struct.pack('>HHH', i * 257, (255 - i) * 257, (i * 7 % 256) * 257) for i in range(256)))]
    for i, aid in enumerate((1001, 1003, 1004, 1005, 1006)):
        res1.append((0x7F01, aid, still(492, 247, lambda x, y, i=i: 20 + i * 30 + ((x // 16 + y // 16) & 1))))
    res1.append((0x7F01, 16000, still(512, 384, lambda x, y: 30 + ((x // 32 + y // 32) & 1))))
    for aid, (w, h) in ((16001, (73, 48)), (16002, (73, 48)), (16003, (73, 48)), (16009, (85, 48))):
        res1.append((0x7F01, aid, aseq([rle_cel(w, h, lambda x, y, b=aid % 50: 120 + b + (y // 8 & 3)) for _ in range(3)], [[(0, 0, 0, 0), (-1, -1, -1, -1)]])))
    for aid in (16005, 16006):
        res1.append((0x7F01, aid, still(31, 24, lambda x, y: 90)))
    menus = {500: [('&File', [(0, 500, '&Rank'), (0, 502, 'E&xit')])],
             600: [('&Options', [(0, 600, 'New Player &Hints'), (0, 602, '&Sound Effects'), (0, 603, '&Music'), (0, 604, '&Voice'), (0, 0, ''), (0, 606, '&Calculator')])],
             1000: [('&File', [(0, 1000, 'Return to Sign &In'), (0, 1002, '&Rank'), (0, 1004, 'E&xit')])],
             1100: [('&Options', [(0, 1100, 'New Player &Hints'), (0, 1102, '&Sound Effects'), (0, 1103, '&Music'), (0, 1104, '&Voice'), (0, 1106, '&Customize...')])],
             1200: [('&Help', [(0, 1200, '&How to Play')])],
             1300: [('&File', [(0, 1300, 'Return to Sign &In'), (0, 1302, 'E&xit')])],
             1400: [('&Options', [(0, 1400, '&Sound Effects'), (0, 1401, '&Music')])],
             1500: [('&Help', [(0, 1500, '&Help')])]}
    for mid, pp in menus.items(): res1.append((0x4, mid, menu_res(pp)))
    for tid, nm in ((1724, 'INTRO'), (1728, 'LEVEL1'), (1729, 'LEVEL2'), (1745, 'ROOM1'), (1746, 'ROOM2'), (1717, 'ENTER'), (1735, 'MIDNITE'), (1757, 'VICTORY')):
        res1.append((0x7F06, tid, (' ' + nm + '.MID\r\n').encode()))
    nt = nametable([(1, ('ASEQ', 'A')), (2, ('BINA', 'B')), (3, ('ESEQ', 'E')), (4, ('INTS', 'I')), (5, ('RRGB', 'R')), (6, ('TEXT', 'T'))])
    res1.append((0x800F, 1, nt))
    open(os.path.join(outdir, 'SSO1.DAT'), 'wb').write(ne_file(res1))
    for fn in ('SSOWINCD.EXE', 'SSO2.DAT', 'SSO3.DAT', 'SND.DAT', 'SPEECH.DAT', 'SSO.FON'):      # stand-ins: the game folder is complete (13 section 3.1, 14 section 5.1)
        open(os.path.join(outdir, fn), 'wb').write(ne_file([(0x7F01, 1, b'x')]))

if __name__ == '__main__':
    main(sys.argv[1])
