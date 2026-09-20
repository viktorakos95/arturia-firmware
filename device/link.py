"""ARM ELF and Thumb checks for the source-built device integration."""
import struct


def thumb_branch(source: int, target: int, kind: str = "bl") -> bytes:
    """Encode Thumb BL / unconditional B.W from even instruction addresses."""
    if kind not in ("bl", "b.w") or type(source) is not int or type(target) is not int:
        raise ValueError("Expected bl/b.w with integer instruction addresses")
    if source & 1 or target & 1 or not 0 <= source <= 0xfffffffb or not 0 <= target <= 0xffffffff:
        raise ValueError("Expected even uint32 instruction addresses")
    delta = target - source - 4
    if not -(1 << 24) <= delta < (1 << 24):
        raise ValueError("Thumb branch displacement out of range")
    imm = delta & 0x1ffffff
    sign, i1, i2 = (imm >> 24) & 1, (imm >> 23) & 1, (imm >> 22) & 1
    j1, j2 = 1 ^ i1 ^ sign, 1 ^ i2 ^ sign
    first = 0xf000 | sign << 10 | (imm >> 12 & 0x3ff)
    second = (0xd000 if kind == "bl" else 0x9000) | j1 << 13 | j2 << 11 | (imm >> 1 & 0x7ff)
    return struct.pack("<HH", first, second)


def decode_thumb_branch(source: int, code: bytes) -> tuple[str, int]:
    if type(source) is not int or source & 1 or not 0 <= source <= 0xfffffffb or len(code) != 4:
        raise ValueError("Expected a four-byte instruction at an even uint32 address")
    first, second = struct.unpack("<HH", code)
    tag = second & 0xd000
    if first & 0xf800 != 0xf000 or tag not in (0x9000, 0xd000):
        raise ValueError("Not BL or unconditional B.W")
    sign, j1, j2 = first >> 10 & 1, second >> 13 & 1, second >> 11 & 1
    imm = (sign << 24 | (1 ^ j1 ^ sign) << 23 | (1 ^ j2 ^ sign) << 22 |
           (first & 0x3ff) << 12 | (second & 0x7ff) << 1)
    if sign:
        imm -= 1 << 25
    target = source + 4 + imm
    if not 0 <= target <= 0xffffffff:
        raise ValueError("Branch target outside uint32 range")
    return ("bl" if tag == 0xd000 else "b.w", target)


def elf32(raw: bytes) -> tuple[list[dict], list[dict]]:
    if len(raw) < 52 or raw[:7] != b"\x7fELF\x01\x01\x01":
        raise ValueError("Expected little-endian ELF32")
    if struct.unpack_from("<HHI", raw, 16) != (2, 40, 1):
        raise ValueError("Expected an executable ARM ELF")
    if struct.unpack_from("<H", raw, 40)[0] != 52:
        raise ValueError("Unexpected ELF header size")
    shoff = struct.unpack_from("<I", raw, 32)[0]
    entsize, count, names_index = struct.unpack_from("<HHH", raw, 46)
    if entsize != 40 or not count or names_index >= count or shoff < 52 or shoff + count * 40 > len(raw):
        raise ValueError("Invalid ELF section table")
    headers = [struct.unpack_from("<10I", raw, shoff + i * 40) for i in range(count)]

    def section_data(header):
        offset, size = header[4:6]
        if header[1] == 8:
            return b""
        if offset + size > len(raw):
            raise ValueError("ELF section outside file")
        return raw[offset:offset + size]

    if headers[names_index][1] != 3:
        raise ValueError("Invalid ELF section name table")
    names = section_data(headers[names_index])

    def cstring(table, offset):
        if offset >= len(table):
            raise ValueError("ELF string offset outside table")
        end = table.find(b"\0", offset)
        if end < 0:
            raise ValueError("Unterminated ELF string")
        try:
            return table[offset:end].decode("utf-8")
        except UnicodeDecodeError as exc:
            raise ValueError("Invalid ELF string encoding") from exc

    sections = []
    for index, header in enumerate(headers):
        name, typ, flags, address, offset, size, link, info, align, stride = header
        if link >= count:
            raise ValueError("ELF section link outside table")
        sections.append(dict(index=index, name=cstring(names, name), type=typ, flags=flags,
                             address=address, size=size, link=link, info=info, stride=stride,
                             data=section_data(header)))
    tables = [s for s in sections if s["type"] == 2]
    if len(tables) != 1:
        raise ValueError("Expected exactly one ELF symbol table")
    table = tables[0]
    if table["stride"] != 16 or table["size"] % 16 or sections[table["link"]]["type"] != 3:
        raise ValueError("Invalid ELF symbol table")
    strings, symbols = sections[table["link"]]["data"], []
    for offset in range(0, table["size"], 16):
        name, value, size, info, other, index = struct.unpack_from("<IIIBBH", table["data"], offset)
        if index >= count and index != 0xfff1:
            raise ValueError("Unsupported ELF symbol section index")
        symbols.append(dict(name=cstring(strings, name), value=value, size=size,
                            type=info & 15, binding=info >> 4, section=index))
    return sections, symbols


def verify_link(raw, profile):
    INJECT = int(profile["code_start"], 16)
    LIMIT = int(profile["code_limit"], 16)
    EXTERNALS = {key: int(value, 16) for key, value in profile["externals"].items()}
    sections,symbols=elf32(raw)
    if any(s['name'] and s['section']==0 for s in symbols):raise ValueError('Unresolved runtime symbol')
    names={s['name']:s for s in symbols if s['name']}
    allocated={s['name']:s for s in sections if s['flags']&2 and s['size']}
    if set(allocated)!={'.text','.boot_ram'}:raise ValueError('Unexpected allocated section')
    code=allocated['.text'];ram=allocated['.boot_ram']
    if code['flags'] not in (6,22,54) or code['address']!=INJECT or INJECT+code['size']>LIMIT:raise ValueError('Invalid code extent')
    if ram['flags']!=3 or ram['type']!=8 or ram['address']!=0x20005eb0:raise ValueError('Invalid RAM ownership')
    if names['boot_ram_hook']['value']!=INJECT|1:raise ValueError('Boot entry moved')
    for name,value in EXTERNALS.items():
        if names[name]['value']!=value or names[name]['section']!=0xfff1:raise ValueError('External target differs')
    start=names['__boot_payload_start']['value'];end=names['__boot_payload_end']['value'];reserved=names['__boot_ram_end']['value']
    if start!=0x20005ef0 or end-start!=names['runtime_storage']['size'] or names['runtime_storage']['value']!=start:
        raise ValueError('Runtime storage differs from owned payload')
    if reserved!=((end+7)&~7)+16 or reserved!=ram['address']+ram['size']:raise ValueError('High guard extent differs')
    if names['__boot_stack_margin']['value']!=4096 or names['__boot_stack_margin']['section']!=0xfff1:
        raise ValueError('Declared stack margin differs from policy4096')
    if reserved+4096>0x2000c000:raise ValueError('Initial SP geometry exceeded')
    mappings=sorted((s['value'],s['name'][1]) for s in symbols if s['section']==code['index'] and s['name'].startswith(('$t','$d')))
    ro_start=names['__gd_rodata_start']['value'];ro_end=names['__gd_rodata_end']['value']
    if not INJECT<ro_start<=ro_end==INJECT+code['size']:raise ValueError('Read-only constant extent differs')
    def mapped(address,size,kind):
        before=[(a,k) for a,k in mappings if a<=address]
        return bool(before) and INJECT<=address<=INJECT+code['size']-size and before[-1][1]==kind and all(k==kind for a,k in mappings if address<a<address+size)
    for s in symbols:
        if s['type']==2:
            if s['section']!=code['index'] or not s['value']&1 or s['size']<2 or not mapped(s['value']&~1,2,'t') or (s['value']&~1)+s['size']>INJECT+code['size']:
                raise ValueError('Invalid function metadata: '+s['name'])
            if (s['value']&~1)+s['size']>ro_start:raise ValueError('Function overlaps constants')
    rels=[]
    for sec in sections:
        if sec['type'] not in (4,9):continue
        if sec['type']!=9 or sec['name']!='.rel.text' or sec['stride']!=8 or sec['info']!=code['index'] or sections[sec['link']]['type']!=2:
            raise ValueError('Unreviewed relocation section')
        for off in range(0,sec['size'],8):
            place,info=struct.unpack_from('<II',sec['data'],off);typ=info&255;s=symbols[info>>8]
            encoded=code['data'][place-INJECT:place-INJECT+4]
            if typ in (10,30):
                if not mapped(place,4,'t'):raise ValueError('Branch relocation in data')
                kind,target=decode_thumb_branch(place,encoded)
                if target!=s['value']&~1 or kind!=('bl' if typ==10 else 'b.w'):raise ValueError('Branch target mismatch')
                if s['name'] not in EXTERNALS and not(s['type']==2 and s['section']==code['index']):raise ValueError('Unknown code target')
            elif typ==2:
                if not mapped(place,4,'d'):raise ValueError('Absolute relocation outside literal')
                target=int.from_bytes(encoded,'little');kind='absolute-literal'
                # Clang names local static objects via SECTION+addend. Resolve
                # only exact starts of sized owned objects, never arbitrary RAM.
                object_alias=s['type']==3 and s['section']==ram['index'] and any(
                    o['type']==1 and o['size']>0 and o['section']==ram['index'] and
                    o['value']==target and ram['address']<=target<=ram['address']+ram['size']-o['size']
                    for o in symbols)
                rodata_alias=s['type'] in (1,3) and s['section']==code['index'] and ro_start<=target<ro_end
                if target!=s['value'] and not object_alias and not rodata_alias:raise ValueError('Absolute addend requires explicit review')
                internal=s['section']==ram['index']
                code_function=s['section']==code['index'] and s['type']==2 and s['value']&1 and mapped(s['value']&~1,2,'t')
                boundary=s['name'] in ('__boot_payload_start','__boot_payload_end','__boot_ram_end','__boot_stack_margin')
                external=s['name'] in EXTERNALS and target==EXTERNALS[s['name']] and target&1
                if not internal and not boundary and not code_function and not external and not rodata_alias:raise ValueError('Unreviewed absolute target '+s['name'])
            else:raise ValueError('Unreviewed relocation type '+str(typ))
            rels.append({'site':hex(place),'type':typ,'symbol':s['name'],'value':hex(target),'kind':kind})
    return code,ram,names,rels
