"""Analyze dumped D3D9 vertex-shader bytecode (psyvr_vs_*.bin).

For each unique shader: version, const registers read (c#), relative addressing (a0-indexed
bone palettes), and which const matrix feeds the position output (oPos) chain.
Token format: version dword; opcode tokens bit31=0; operand tokens bit31=1; comment opcode
0xFFFE with dword length in bits 16..30; end = 0x0000FFFF.
Source operand: bits0-10 reg#, bits 11-12 + 28-30 regtype, bit13 (vs) = relative addressing.
Dest operand: same reg fields.
Register types: 0=TEMP 1=INPUT 2=CONST 3=ADDR(a0 for vs) 4=RASTOUT(oPos!) 5=ATTROUT 6=TEXCRDOUT/o#
"""
import glob, os, struct, hashlib
from collections import defaultdict

OPNAMES = {1:'mov',2:'add',3:'sub',4:'mad',5:'mul',6:'rcp',7:'rsq',8:'dp3',9:'dp4',10:'min',11:'max',
           12:'slt',13:'sge',14:'exp',15:'log',16:'lit',17:'dst',18:'lrp',19:'frc',20:'m4x4',21:'m4x3',
           22:'m3x4',23:'m3x3',24:'m3x2',25:'call',26:'callnz',27:'loop',28:'ret',29:'endloop',
           30:'label',31:'dcl',32:'pow',33:'crs',34:'sgn',35:'abs',36:'nrm',37:'sincos',38:'rep',
           39:'endrep',40:'if',41:'ifc',42:'else',43:'endif',44:'break',45:'breakc',46:'mova',
           47:'defb',48:'defi',64:'texcoord',81:'def'}

def regtype(tok):
    return ((tok >> 28) & 0x7) | ((tok >> 8) & 0x18)

def analyze(path):
    data = open(path,'rb').read()
    toks = struct.unpack('<%dI' % (len(data)//4), data)
    version = toks[0]
    i = 1
    consts_read = set()
    rel_consts = set()          # const regs read with a0-relative addressing
    defd = set()                # consts set via def (immediate) - not interesting
    ops_reading_c = []          # (opname, dest_regtype, dest_reg, [const regs read])
    while i < len(toks):
        t = toks[i]
        if t == 0x0000FFFF: break
        if (t & 0xFFFF) == 0xFFFE:  # comment
            i += 1 + ((t >> 16) & 0x7FFF); continue
        opcode = t & 0xFFFF
        opname = OPNAMES.get(opcode, 'op%d' % opcode)
        i += 1
        operands = []
        while i < len(toks) and (toks[i] & 0x80000000):
            operands.append(toks[i]); i += 1
        if opname == 'def':
            if operands: defd.add(operands[0] & 0x7FF)
            continue
        if opname == 'dcl':
            continue
        creads = []
        for k, o in enumerate(operands):
            rt = regtype(o)
            r = o & 0x7FF
            if k > 0 and rt == 2:  # source const
                if r in defd: continue
                consts_read.add(r)
                creads.append(r)
                if o & 0x2000:
                    rel_consts.add(r)
        if operands and creads:
            d = operands[0]
            ops_reading_c.append((opname, regtype(d), d & 0x7FF, creads))
    return version, consts_read, rel_consts, ops_reading_c

files = sorted(glob.glob(os.path.join(os.environ.get('TEMP', '/tmp'), 'psyvr_vs_*.bin')))
by_hash = {}
for f in files:
    h = hashlib.md5(open(f,'rb').read()).hexdigest()
    by_hash.setdefault(h, []).append(f)

print(f"{len(files)} dumps, {len(by_hash)} unique shaders\n")

groups = defaultdict(list)
for h, fl in by_hash.items():
    version, consts, rel, ops = analyze(fl[0])
    uses96 = any(c >= 96 for c in consts)
    uses64 = any(64 <= c < 96 for c in consts)
    uses6 = any(6 <= c <= 9 for c in consts)
    key = (uses6, uses64, uses96)
    groups[key].append((fl[0], version, sorted(consts), sorted(rel), ops, len(fl)))

for key in sorted(groups):
    uses6, uses64, uses96 = key
    tag = f"c6:{'Y' if uses6 else 'n'} c64+:{'Y' if uses64 else 'n'} c96+:{'Y' if uses96 else 'n'}"
    g = groups[key]
    print(f"=== {tag}  ({len(g)} unique shaders, {sum(x[5] for x in g)} dumps) ===")
    # show one representative in detail
    f, version, consts, rel, ops, n = g[0]
    print(f"  rep: {os.path.basename(f)} ver=0x{version:08X} consts={consts} rel={rel}")
    # ops that write to oPos-family (regtype 4 = RASTOUT) or feed it
    for opname, drt, dr, creads in ops:
        if drt == 4 or any(c >= 90 or (6 <= c <= 9) or (16 <= c <= 19) for c in creads):
            dst = 'oPos' if drt == 4 else ('r%d' % dr if drt == 0 else 'rt%d.%d' % (drt, dr))
            print(f"    {opname} -> {dst}   reads c{creads}")
    print()
