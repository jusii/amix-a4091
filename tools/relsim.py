#!/usr/bin/env python3
# Faithful reimplementation of amiga/boot/rel.c ET_REL path (big-endian m68k ELF).
# Reproduces findsections/bindsections/allocbss/specialsyms/relocsections/symvaddr
# and reports exactly which COMPLAIN (-> D245 Alert) would fire, if any.
import sys, struct

SHT_PROGBITS=1; SHT_SYMTAB=2; SHT_NOBITS=8; SHT_RELA=4
SHF_WRITE=1; SHF_ALLOC=2; SHF_EXECINSTR=4
SHN_UNDEF=0; SHN_ABS=0xfff1; SHN_COMMON=0xfff2
R_68K_32=1

def u32(b,o): return struct.unpack_from('>I',b,o)[0]
def u16(b,o): return struct.unpack_from('>H',b,o)[0]

def analyze(path):
    d=open(path,'rb').read()
    assert d[:4]==b'\x7fELF'
    e_type=u16(d,16); e_shoff=u32(d,32); e_shentsize=u16(d,46)
    e_shnum=u16(d,48); e_shstrndx=u16(d,50)
    # section headers
    S=[]
    for i in range(e_shnum):
        o=e_shoff+i*e_shentsize
        S.append(dict(name=u32(d,o),type=u32(d,o+4),flags=u32(d,o+8),
                      addr=u32(d,o+12),offset=u32(d,o+16),size=u32(d,o+20),
                      link=u32(d,o+24),info=u32(d,o+28),entsize=u32(d,o+36)))
    shstr=d[S[e_shstrndx]['offset']:]
    def snm(i):
        n=S[i]['name']; end=shstr.find(b'\0',n); return shstr[n:end].decode('latin1')
    # ---- findsections (by FLAGS, exactly like rel.c) ----
    thdr=rhdr=dhdr=bhdr=symhdr=None
    for s in S:
        if s['type']==SHT_PROGBITS:
            if s['size']>0 and (s['flags']&SHF_ALLOC):
                if s['flags']&SHF_EXECINSTR: thdr=s
                elif s['flags']&SHF_WRITE:   dhdr=s
                else:                        rhdr=s
        elif s['type']==SHT_NOBITS:
            if (s['flags']&SHF_WRITE) and s['size']>0: bhdr=s
        elif s['type']==SHT_SYMTAB:
            symhdr=s
    # bound = sh_addr set by bindsections; mark which section dicts get bound
    bound=set()
    vaddr=0x07800000; end=vaddr
    for s in (thdr,rhdr,dhdr,bhdr):
        if s is not None:
            s['addr']=end; end+=s['size']; bound.add(id(s))
    boundidx={i for i,s in enumerate(S) if id(s) in bound}
    bhdr_idx = S.index(bhdr) if bhdr is not None else None
    # symtab (mutable shndx copy so we can apply allocbss + specialsyms)
    syo=symhdr['offset']; syn=symhdr['size']//symhdr['entsize']
    raw=bytearray(d)  # mutable copy not needed; track shndx overrides in a dict
    shndx_override={}
    def sym(idx):
        o=syo+idx*16
        sh=shndx_override.get(idx, u16(d,o+14))
        return dict(name=u32(d,o),value=u32(d,o+4),size=u32(d,o+8),
                    info=d[o+12],shndx=sh)
    strtab=d[S[symhdr['link']]['offset']:]
    def symname(idx):
        n=sym(idx)['name']; end=strtab.find(b'\0',n); return strtab[n:end].decode('latin1')
    # ---- allocbss(): SHN_COMMON -> bss (bound) ----
    for idx in range(syn):
        if sym(idx)['shndx']==SHN_COMMON and bhdr_idx is not None:
            shndx_override[idx]=bhdr_idx
    # ---- specialsyms(): etext/edata/end -> SHN_ABS ----
    for idx in range(syn):
        nm=symname(idx)
        if nm in ('etext','edata','end'):
            shndx_override[idx]=SHN_ABS
    # ---- enumerate relocs, apply symvaddr() test ----
    complaints=[]
    typehist={}
    for si,s in enumerate(S):
        if s['type']!=SHT_RELA: continue
        nrel=s['size']//s['entsize']
        base=s['offset']
        for k in range(nrel):
            o=base+k*12
            r_info=u32(d,o+4)
            reltype=r_info&0xff; symidx=r_info>>8
            typehist[reltype]=typehist.get(reltype,0)+1
            sy=sym(symidx); shndx=sy['shndx']
            # symvaddr() logic:
            if 0<shndx<e_shnum:
                if shndx not in boundidx:
                    complaints.append(('unbound_section',snm(si),symidx,symname(symidx),shndx,snm(shndx) if shndx<e_shnum else '?',reltype))
            elif shndx==SHN_ABS:
                pass
            elif shndx==SHN_COMMON:
                # allocbss() reassigns COMMON -> bhdr (bound) BEFORE relocsections, so OK if bhdr exists
                if bhdr is None:
                    complaints.append(('common_no_bss',snm(si),symidx,symname(symidx),shndx,'COMMON',reltype))
            else:  # SHN_UNDEF (0) or out of range
                complaints.append(('undef_or_oor',snm(si),symidx,symname(symidx),shndx,'UND' if shndx==0 else '?',reltype))
            if reltype!=R_68K_32:
                complaints.append(('bad_reltype',snm(si),symidx,symname(symidx),shndx,'',reltype))
    print(f"== {path} ==  e_type={e_type:#x} shnum={e_shnum}")
    print(f"   bound sections (idx): {sorted(boundidx)}  text={snm(S.index(thdr)) if thdr else None}")
    print(f"   reloc type histogram: {typehist}")
    print(f"   COMPLAINTS (would fire D245): {len(complaints)}")
    seen=set()
    for c in complaints:
        key=(c[0],c[3],c[4])
        if key in seen: continue
        seen.add(key)
        print(f"     [{c[0]}] reloc-sect={c[1]} sym#{c[2]} '{c[3]}' shndx={c[4]}({c[5]}) type={c[6]}")
        if len(seen)>=40: print("     ... (truncated)"); break
    return complaints

if __name__=='__main__':
    for p in sys.argv[1:]:
        analyze(p); print()
