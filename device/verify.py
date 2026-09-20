#!/usr/bin/env python3
"""Independent, read-only verifier for the complete generator extension.

Uses only Python's standard library: no builder, packer or image-plan imports.
The expected container is independently assembled only in memory;
this program never writes firmware and never accesses USB/MIDI/the device.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import struct

STOCK_SHA = '464e2ca2fc5318e6026f15cf3c3a61f6417a80171260543a18e87df116319a8a'
START, INJECT, LIMIT = 0x08004000, 0x0801f400, 0x0802fc00
MAGIC = bytes.fromhex('6874da51')
FINAL = bytes.fromhex('0d02fc0000000e02fc000000')
# Independent instruction/data anchors; target values come from the actual ELF.
ANCHORS = (
    (0x08012eb6, '00 23 a3 71', 'b.w', 'ks37_playback_cycle_hook'),
    (0x08017ed0, '10 b5 82 b0', 'b.w', 'ks37_generator_slot_mode_guard12'),
    (0x08005a44, '84 f8 6d 00', 'b.w', 'ks37_generator_mode_ack_guard12'),
    (0x08011ee8, '80 f8 49 10', 'b.w', 'ks37_time_division_request_hook'),
    (0x08012cd2, '84 f8 48 70', 'b.w', 'ks37_time_division_applied_hook'),
    (0x08014034, '08 f0 a6 f9', 'bl', 'ks37_generator_pitch_hook'),
    (0x0800de30, '00 23 01 e0', 'b.w', 'ks37_generator_slot_dirty_guard'),
    (0x08011a1c, '2d e9 f0 43', 'b.w', 'ks37_generator_builder_guard'),
    (0x0801316a, '70 b4 83 b0', 'b.w', 'ks37_generator_record_note_guard'),
    (0x08013266, '82 29 01 d0', 'b.w', 'ks37_generator_record_rest_guard'),
    (0x080132ce, '2d e9 f0 4f', 'b.w', 'ks37_generator_merge_cell_guard'),
    (0x0801342a, 'f0 b4 00 23', 'b.w', 'ks37_generator_reorder_cell_guard'),
    (0x0801348e, '2d e9 f0 43', 'b.w', 'ks37_generator_record_build_guard'),
    (0x08013788, '02 68 92 f8 00 34', 'b.w', 'ks37_generator_shorten_guard'),
    (0x080137c4, '03 68 01 22', 'b.w', 'ks37_generator_clear_guard'),
    (0x080137f0, '30 b4 90 f8 2e 30', 'b.w', 'ks37_generator_record_length_guard'),
    (0x08013890, 'f0 b4 08 29', 'b.w', 'ks37_generator_pattern_guard'),
    (0x08013944, 'f8 b5 04 46', 'b.w', 'ks37_generator_tie_guard'),
    (0x08013b84, 'f8 b5 90 f9 45 30', 'b.w', 'ks37_generator_flush_record_guard'),
    (0x08013c58, '00 23 3f 2b', 'b.w', 'ks37_generator_clear_cells_guard'),
    (0x08014418, '30 b4 8b 7a', 'b.w', 'ks37_generator_config_bytes_guard'),
    (0x08014534, '30 b4 8b 7a', 'b.w', 'ks37_generator_config_words_guard'),
    (0x08004930, '84 f8 5a 30', 'b.w', 'ks37_control_hook'),
    (0x0800c05e, 'b0 f8 44 00', 'b.w', 'ks37_input_usb_flags_hook'),
    (0x0800cdfa, '0e f0 a9 fc', 'bl', 'ks37_input_scanner_hook'),
    (0x0800cf30, '0e f0 0e fc', 'bl', 'ks37_input_scanner_hook'),
    (0x0800d064, '0e f0 74 fb', 'bl', 'ks37_input_scanner_hook'),
    (0x0800d26e, '30 b5 83 b0', 'b.w', 'ks37_panel_pixel_hook'),
    (0x0800e488, '10 b5 04 46', 'b.w', 'ks37_input_usb_deinit_hook'),
    (0x08010310, '2e 78 06 f0 0f 06', 'b.w', 'ks37_input_midi_pre_filter_hook'),
    (0x08012334, 'f0 b5 85 b0', 'b.w', 'ks37_playback_transport_hook'),
    (0x08012ec0, '00 f0 e4 ff', 'bl', 'ks37_playback_step_hook'),
    (0x08013020, '01 60 70 47', 'b.w', 'ks37_generator_current_guard'),
    (0x08013024, '41 60 70 47', 'b.w', 'ks37_generator_pending_guard'),
    (0x08013028, '43 68 03 60', 'b.w', 'ks37_generator_pending_apply_guard'),
    (0x080130cc, '30 b4 05 68', 'b.w', 'ks37_generator_cell_guard'),
    (0x08013102, '03 68 40 29', 'b.w', 'ks37_generator_length_guard'),
    (0x08013118, '03 68 83 f8 02 14', 'b.w', 'ks37_generator_gate_guard'),
    (0x0801312e, '03 68 83 f8 01 14', 'b.w', 'ks37_generator_swing_guard'),
    (0x080150a4, 'fb f7 c8 fa', 'bl', 'ks37_input_usb_ingress_hook'),
    (0x080154d0, '6e 4b 1d 60', 'b.w', 'ks37_input_cold_hook'),
    (0x080157a8, '03 f0 c6 fc', 'bl', 'ks37_input_acquire_hook'),
    (0x080157b4, 'fd f7 0a f9', 'bl', 'ks37_generator_main_hook'),
    (0x080157ca, 'f7 f7 3d fa', 'bl', 'ks37_input_scan_hook'),
    (0x08016968, '38 b5 04 46', 'b.w', 'ks37_generator_mode_guard'),
    (0x08017260, '70 b5 92 b0', 'b.w', 'ks37_generator_ui_guard'),
    (0x08017856, '01 23 43 73', 'b.w', 'ks37_button_chord_down_hook'),
    (0x08017d98, '00 23 43 73', 'b.w', 'ks37_button_chord_up_hook'),
    (0x0801862a, '1a 68 12 f0 20 0f', 'b.w', 'ks37_input_uart_status_hook'),
    (0x08019fa8, '30 bc 70 47', 'b.w', 'ks37_button_constructor_hook'),
    (0x08019fe2, '99 4b 93 f8 b9 50', 'b.w', 'ks37_button_press_pre_hook'),
    (0x0801a500, '0d b0 f0 bd', 'b.w', 'ks37_button_press_post_hook'),
    (0x0801a558, '75 4b 93 f8 b9 30', 'b.w', 'ks37_button_release_pre_hook'),
    (0x0801a5e4, '0b b0 30 bd', 'b.w', 'ks37_button_release_post_hook'),
    (0x0801a7c2, 'ff f7 cd ff', 'bl', 'ks37_button_observer_hook'),
    (0x0801b750, '2d e9 f0 4f', 'b.w', 'ks37_generator_note_guard'),
    (0x0801d33a, '00 f0 c5 fa', 'bl', 'boot_ram_hook'),
    (0x0801d354, 'ac 5e 00 20', 'data', '__boot_ram_end'),
    (0x0801d4e2, '04 eb 08 01 38 46 ed f7 c1 f8', 'b.w', 'ks37_panel_display_hook'),
    (0x0801dab0, '00 f0 5e f8', 'bl', 'boot_ram_target_sbrk'),
    (0x0801db88, 'b0 5e 00 20', 'data', '__boot_ram_end'),
)
EXTERNALS = {
    'stock_runtime_init':0x0801d8c9,
    'ks37_control_pass':0x08004935,'ks37_control_consume':0x08004987,
    '__bda_stock_debounce':0x0801a761,
    'ks37_playback_transport_continue':0x08012339,
    'ks37_playback_stock_step':0x08013e8d,
    '__gd_stock_service':0x080129cd,
    '__gp_writer_continue':0x0800d273,'__gp_spi_send':0x0800a66f,
    '__gp_display_continue':0x0801d4ed,
    'ks37_generator_pitch_native':0x0801c385,
    'ks37_time_division_applied_continue':0x08012ce1,
}


class VerificationError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise VerificationError(message)


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def cut(raw, start, size):
    require(0 <= start <= len(raw) and 0 <= size <= len(raw)-start, 'Truncated binary field')
    return raw[start:start+size]


def cstring(raw, offset):
    require(0 <= offset < len(raw), 'Invalid string offset')
    end = raw.find(b'\0', offset)
    require(end >= 0, 'Unterminated string')
    return raw[offset:end].decode('ascii')


def branch_target(address, raw):
    require(len(raw) == 4, 'Thumb instruction must occupy four bytes')
    a, b = struct.unpack('<HH', raw)
    require(a & 0xf800 == 0xf000 and b & 0xd000 in (0x9000, 0xd000), 'Unsupported Thumb branch')
    kind = 'bl' if b & 0x4000 else 'b.w'
    sign = (a >> 10) & 1
    i1 = 1 ^ ((b >> 13) & 1) ^ sign
    i2 = 1 ^ ((b >> 11) & 1) ^ sign
    displacement = (sign << 24) | (i1 << 23) | (i2 << 22) | ((a & 1023) << 12) | ((b & 2047) << 1)
    if sign:
        displacement -= 1 << 25
    return kind, address + 4 + displacement


def branch_bytes(address, target, kind):
    displacement = target-address-4
    require(kind in ('bl', 'b.w') and displacement % 2 == 0 and -(1 << 24) <= displacement < (1 << 24), 'Unencodable branch')
    value = displacement & ((1 << 25)-1)
    sign, i1, i2 = (value >> 24) & 1, (value >> 23) & 1, (value >> 22) & 1
    a = 0xf000 | sign << 10 | ((value >> 12) & 1023)
    b = (0xd000 if kind == 'bl' else 0x9000) | (1 ^ i1 ^ sign) << 13 | (1 ^ i2 ^ sign) << 11 | ((value >> 1) & 2047)
    raw = struct.pack('<HH', a, b)
    require(branch_target(address, raw) == (kind, target), 'Independent branch encode/decode disagree')
    return raw


def read_elf(raw):
    require(len(raw) >= 52 and raw[:7] == b'\x7fELF\x01\x01\x01', 'Expected ELF32 little-endian v1')
    typ, machine, version, entry, phoff, shoff, flags, ehsize, phsize, phnum, shsize, shnum, shstr = struct.unpack_from('<HHIIIIIHHHHHH', raw, 16)
    require((typ, machine, version, ehsize, shsize) == (2, 40, 1, 52, 40), 'Unexpected ARM ELF metadata')
    require(0 < shnum < 1024 and shstr < shnum, 'Invalid ELF section count')
    sections = []
    for i in range(shnum):
        fields = struct.unpack('<10I', cut(raw, shoff+i*40, 40))
        section = dict(zip(('name_offset', 'type', 'flags', 'address', 'offset', 'size', 'link', 'info', 'align', 'stride'), fields))
        section.update(index=i, data=b'' if section['type'] == 8 else cut(raw, section['offset'], section['size']))
        sections.append(section)
    strings = sections[shstr]['data']
    for section in sections:
        section['name'] = cstring(strings, section['name_offset'])
    tables = [s for s in sections if s['type'] == 2]
    require(len(tables) == 1, 'Expected one static ELF symbol table')
    table = tables[0]
    require(table['stride'] == 16 and table['size'] % 16 == 0 and table['link'] < shnum, 'Invalid ELF symbols')
    strings = sections[table['link']]['data']
    symbols = []
    for offset in range(0, table['size'], 16):
        name, value, size, info, other, section = struct.unpack_from('<IIIBBH', table['data'], offset)
        symbols.append(dict(name=cstring(strings, name), value=value, size=size, type=info & 15, section=section))
    require(not any(s['name'] and not s['section'] for s in symbols), 'ELF contains unresolved symbols')
    allocated = [s for s in sections if s['flags'] & 2 and s['size']]
    require(len(allocated) == 2 and {s['name'] for s in allocated} == {'.text', '.boot_ram'}, 'ELF has unexpected allocated sections')
    text = next(s for s in allocated if s['name'] == '.text')
    ram = next(s for s in allocated if s['name'] == '.boot_ram')
    require(text['type'] == 1 and text['flags'] in (6,22,54) and text['address'] == INJECT and 0 < text['size'] <= LIMIT-INJECT, 'Invalid code placement')
    require(ram['type'] == 8 and ram['flags'] == 3 and ram['address'] == 0x20005eb0, 'RAM must be owned NOLOAD BSS')

    def symbol(name):
        found = [s for s in symbols if s['name'] == name and (name != 'state' or s['type'] == 1)]
        require(len(found) == 1, 'Missing/ambiguous ELF symbol: '+name)
        return found[0]

    names = {name: symbol(name) for name in ('__boot_payload_start', '__boot_payload_end', '__boot_ram_end', '__boot_stack_margin', '__gd_rodata_start', '__gd_rodata_end', 'state', 'guard_low', 'guard_high', 'runtime_storage', 'boot_payload_init', 'boot_payload_get', 'boot_ram_status_get', *(a[3] for a in ANCHORS), *EXTERNALS)}
    payload_start, payload_end, end = (names[n]['value'] for n in ('__boot_payload_start', '__boot_payload_end', '__boot_ram_end'))
    require(payload_start == 0x20005ef0 and payload_end > payload_start and end == ((payload_end+7) & ~7)+16, 'RAM boundary symbols disagree')
    # Runtime size may change; verify actual allocation and both guards.
    require(payload_end-payload_start >= 8, 'Runtime reservation is empty')
    require(end == ram['address']+ram['size'] and end+4096 <= 0x2000c000, 'RAM reservation exceeds profile geometry')
    require(names['__boot_stack_margin']['value'] == 4096 and names['__boot_stack_margin']['section'] == 0xfff1, 'Stack margin is not the reviewed 4096-byte policy')
    for name, value, size in (('state', 0x20005eb0, 44), ('guard_low', 0x20005ee0, 16), ('runtime_storage', payload_start, payload_end-payload_start), ('guard_high', (payload_end+7) & ~7, 16)):
        s = names[name]
        require((s['value'], s['size'], s['section'], s['type']) == (value, size, ram['index'], 1), 'RAM object ownership/layout differs: '+name)
    objects = [s for s in symbols if s['type'] == 1 and s['size'] and s['section'] == ram['index']]
    require({s['name'] for s in objects} == {'state', 'guard_low', 'runtime_storage', 'guard_high'} and len(objects) == 4, 'Unreviewed RAM objects')
    require(entry == INJECT | 1 and names['boot_ram_hook']['value'] == entry, 'Boot entry differs')
    for name, value in EXTERNALS.items():
        require(names[name]['value'] == value and names[name]['section'] == 0xfff1, 'Stock continuation differs: '+name)
    ro_start=names['__gd_rodata_start']['value'];ro_end=names['__gd_rodata_end']['value']
    require(INJECT < ro_start <= ro_end == INJECT+text['size'], 'Read-only constants extent differs')
    mappings = sorted((s['value'], s['name'][1]) for s in symbols if s['section'] == text['index'] and s['name'].startswith(('$t', '$d')))
    def mapped(at, kind, width=4):
        preceding = [(a, k) for a, k in mappings if a <= at]
        return INJECT <= at <= INJECT+text['size']-width and preceding and preceding[-1][1] == kind and all(k == kind for a, k in mappings if at < a < at+width)
    for _, _, kind, name in ANCHORS:
        if kind != 'data':
            s = names[name]
            require(s['type'] == 2 and s['section'] == text['index'] and
                    s['value'] & 1 and s['size'] >= 2 and
                    (s['value'] & ~1)+s['size'] <= ro_start and
                    mapped(s['value'] & ~1, 't', 2),
                    'Hook target is not an owned Thumb function: '+name)
    for s in symbols:
        if s['type'] == 2:
            require((s['value'] & ~1)+s['size'] <= ro_start, 'Function overlaps read-only constants')
            require(s['section'] == text['index'] and s['value'] & 1 and s['size'] >= 2 and INJECT <= (s['value'] & ~1) <= INJECT+text['size']-s['size'] and mapped(s['value'] & ~1, 't', 2), 'Invalid Thumb function: '+s['name'])
    relocation_count = 0
    for section in sections:
        if section['type'] not in (4, 9):
            continue
        require(section['type'] == 9 and section['name'] == '.rel.text' and section['stride'] == 8 and section['size'] % 8 == 0 and section['info'] == text['index'] and section['link'] == table['index'], 'Unsupported ELF relocation section')
        for off in range(0, section['size'], 8):
            place, info = struct.unpack_from('<II', section['data'], off)
            require(info >> 8 < len(symbols), 'Invalid relocation symbol index')
            s, kind = symbols[info >> 8], info & 255
            encoded = cut(text['data'], place-INJECT, 4)
            if kind in (10, 30):
                require(mapped(place, 't') and branch_target(place, encoded) == ('bl' if kind == 10 else 'b.w', s['value'] & ~1), 'ELF branch relocation differs')
                require(s['name'] in EXTERNALS or (s['type'] == 2 and s['section'] == text['index']), 'Unreviewed ELF branch target')
            elif kind == 2:
                target = int.from_bytes(encoded, 'little')
                alias = s['type'] == 3 and s['section'] == ram['index'] and target in {o['value'] for o in objects}
                ro_alias=s['type'] in (1,3) and s['section']==text['index'] and ro_start<=target<ro_end
                external=s['name'] in EXTERNALS and target==EXTERNALS[s['name']] and target&1
                require(mapped(place, 'd') and (target == s['value'] or alias or ro_alias), 'ELF absolute relocation differs')
                require(s['section'] in (text['index'], ram['index']) or s['name'] == '__boot_stack_margin' or external, 'Unreviewed ELF absolute target')
                if s['section'] == text['index'] and not ro_alias:
                    require(s['type'] == 2 and target & 1 and mapped(target & ~1, 't', 2), 'Absolute code pointer must name a mapped Thumb function')
            else:
                raise VerificationError('Unreviewed ELF relocation type: '+str(kind))
            relocation_count += 1
    require(relocation_count > 0, 'ELF must retain relocations for audit')
    return dict(text=text, ram=ram, names=names, relocation_count=relocation_count)


@dataclass(frozen=True)
class Record:
    index: int
    raw: bytes
    payload: bytes | None


def read_records(ascii_bytes):
    require(isinstance(ascii_bytes, bytes) and 0 < len(ascii_bytes) <= 1048576,
            'Expected a nonempty container of at most 1 MiB')
    require(len(ascii_bytes) % 2 == 0 and re.fullmatch(rb'[0-9A-F]+', ascii_bytes), 'Expected uppercase ASCII hex, no whitespace')
    binary = bytes.fromhex(ascii_bytes.decode('ascii'))
    records, offset = [], 0
    for index in range(176):
        length = int.from_bytes(cut(binary, offset, 2), 'big')+2
        require(length in ((1066,) if index == 175 else (18, 1054)), 'Unsupported record length')
        raw = cut(binary, offset, length)
        address = 0x4000+index*1024
        require(raw[2:7] == MAGIC+b'\x09' and raw[7:10] == address.to_bytes(3, 'big') and raw[10:12] == b'\0\0' and raw[-6:-2] == bytes(4), 'Record framing/address differs')
        require((sum(raw[2:-2])+int.from_bytes(raw[-2:], 'little')) & 65535 == 0, 'Record local checksum differs: '+str(index))
        payload = None
        if length != 18:
            require(raw[12:18] == bytes.fromhex('0a0000000400') and raw[1042:1048] == b'\x0b'+address.to_bytes(3, 'big')+b'\x04\x00', 'Payload commands differ')
            require(raw[1048:-6] == (FINAL if index == 175 else b''), 'Trailing commands differ')
            payload = raw[18:1042]
        records.append(Record(index, raw, payload))
        offset += length
    require(offset == len(binary), 'Trailing bytes or extra records')
    return tuple(records)


def image(records):
    return b''.join(r.payload if r.payload is not None else b'\xff'*1024 for r in records)


def sums(records):
    body = image(records)
    stored = int.from_bytes(body[-2:], 'little')
    require((sum(body[:-2])+stored) & 65535 == 0, 'Overall FF-gap application sum differs')
    return dict(local_checksums=176, ff_gap_application_residue=0, stored_le16=body[-2:].hex(' '))


def expected(stock_ascii, elf_raw):
    require(sha(stock_ascii) == STOCK_SHA, 'Vendor SHA differs from pinned 1.1.6.579')
    stock = read_records(stock_ascii)
    require([r.index for r in stock if r.payload is None] == list(range(109, 175)), 'Stock empty-record layout differs')
    sums(stock)
    elf = read_elf(elf_raw)
    expected_image = bytearray(image(stock))
    changes = []
    for address, before_hex, kind, name in ANCHORS:
        before = bytes.fromhex(before_hex)
        require(expected_image[address-START:address-START+len(before)] == before, 'Pinned instruction anchor differs')
        target = elf['names'][name]['value']
        after = struct.pack('<I', target) if kind == 'data' else branch_bytes(address, target & ~1, kind)
        require(len(before) in (4,6,10), 'Unreviewed patch width')
        after += bytes.fromhex('00 bf')*((len(before)-len(after))//2)
        expected_image[address-START:address-START+len(before)] = after
        changes.append(dict(address=hex(address), before=before.hex(' '), after=after.hex(' '), target=name, target_address=hex(target), kind=kind))
    code = elf['text']['data']
    expected_image[INJECT-START:INJECT-START+len(code)] = code
    expected_image[-2:] = ((-sum(expected_image[:-2])) & 65535).to_bytes(2, 'little')
    expanded = list(range(109, 109+(len(code)+1023)//1024))
    # Minimal independent framing of the same expected bytes; only in memory.
    encoded = []
    for r in stock:
        payload = bytes(expected_image[r.index*1024:(r.index+1)*1024])
        if r.payload is None and r.index not in expanded:
            encoded.append(r.raw)
            continue
        address = (0x4000+r.index*1024).to_bytes(3, 'big')
        body = MAGIC+b'\x09'+address+b'\0\0'+bytes.fromhex('0a0000000400')+payload+b'\x0b'+address+b'\x04\x00'+(FINAL if r.index == 175 else b'')+bytes(4)
        encoded.append(struct.pack('>H', len(body)+2)+body+struct.pack('<H', (-sum(body)) & 65535))
    raw = b''.join(encoded).hex().upper().encode('ascii')
    return raw, stock, elf, changes, expanded


def check_profile(profile):
    """Pin metadata to the independently reviewed patch and memory boundary."""
    require(isinstance(profile, dict), 'Profile must be an object')
    fields = dict(schema_version=1, stock_sha256=STOCK_SHA, flash_base=0x08000000,
                  application_start=START, code_start=INJECT, code_limit=LIMIT,
                  ram_start=0x20005eb0, payload_start=0x20005ef0,
                  initial_sp=0x2000c000, stack_margin=4096)
    for key, wanted in fields.items():
        got = profile.get(key)
        if isinstance(wanted, int) and isinstance(got, str):
            try:
                got = int(got, 0)
            except ValueError:
                raise VerificationError('Invalid profile integer: '+key)
        require(type(got) is type(wanted) and got == wanted, 'Profile differs: '+key)
    externals = profile.get('externals')
    require(isinstance(externals, dict) and set(externals) == set(EXTERNALS),
            'Profile external names differ')
    for name, wanted in EXTERNALS.items():
        got = externals[name]
        got = int(got, 0) if isinstance(got, str) else got
        require(type(got) is int and got == wanted, 'Profile external differs: '+name)
    patches = profile.get('patches')
    require(isinstance(patches, list) and len(patches) == len(ANCHORS),
            'Profile patch count differs')
    normalized = []
    for patch in patches:
        require(isinstance(patch, dict), 'Invalid profile patch')
        address = patch.get('address')
        address = int(address, 0) if isinstance(address, str) else address
        kind = patch.get('kind')
        kind = 'data' if kind == 'DATA-LE32' else kind
        normalized.append((address, bytes.fromhex(patch.get('before', '')), kind,
                           patch.get('target')))
    wanted = [(a, bytes.fromhex(b), k, n) for a, b, k, n in ANCHORS]
    require(sorted(normalized) == sorted(wanted), 'Profile patch allowlist differs')


def verify_bytes(stock_ascii, candidate_ascii, elf_raw, profile=None, build_report=None):
    if profile is not None:
        check_profile(profile)
    wanted, stock, elf, changes, expanded = expected(stock_ascii, elf_raw)
    candidate = read_records(candidate_ascii)
    metrics = sums(candidate)
    require([r.index for r in candidate if r.payload is None] == list(range(expanded[-1]+1, 175)),
            'Expanded records differ from exact ELF extent')
    require(candidate_ascii == wanted,
            'Candidate differs from exact patch allowlist, ELF, FF padding or derived sums')
    names = elf['names']
    flash = image(candidate)
    for address, _, kind, name in ANCHORS:
        raw = flash[address-START:address-START+4]
        target = names[name]['value']
        require(int.from_bytes(raw, 'little') == target if kind == 'data' else
                branch_target(address, raw) == (kind, target & ~1),
                'Candidate hook target differs')
    changed = [a.index for a, b in zip(stock, candidate) if a.raw != b.raw]
    result = dict(status='PASS-independent-generator-device',
                  candidate_sha256=sha(candidate_ascii), candidate_bytes=len(candidate_ascii),
                  stock_sha256=STOCK_SHA, elf_sha256=sha(elf_raw),
                  code_sha256=sha(elf['text']['data']), code_bytes=elf['text']['size'],
                  runtime_bytes=names['runtime_storage']['size'],
                  ram_start=hex(elf['ram']['address']), ram_end=hex(names['__boot_ram_end']['value']),
                  ram_bytes=elf['ram']['size'], payload_start=hex(names['__boot_payload_start']['value']),
                  payload_end=hex(names['__boot_payload_end']['value']),
                  stack_margin_policy_bytes=4096,
                  ram_objects={n: dict(address=hex(names[n]['value']), bytes=names[n]['size'])
                               for n in ('state', 'guard_low', 'runtime_storage', 'guard_high')},
                  patches=changes, expanded_records=expanded, changed_records=changed,
                  retained_relocations=elf['relocation_count'], **metrics,
                  limitations=[
                      'File verification does not establish device acceptance, boot or runtime correctness.',
                      'The stack margin is a configured policy, not a maximum-stack measurement.',
                      'Matching hooks and ELF bytes does not validate modified code behavior or peripheral ownership.'])
    if profile is not None:
        baseline = profile.get('baseline', {})
        require(isinstance(baseline, dict), 'Invalid profile baseline')
        result['matches_observed_baseline'] = result['candidate_sha256'] == baseline.get('candidate_sha256')
    if build_report is not None:
        require(isinstance(build_report, dict), 'Build report must be an object')
        matched = []
        for key in ('candidate_sha256', 'candidate_bytes', 'stock_sha256', 'elf_sha256',
                    'code_sha256', 'code_bytes', 'runtime_bytes', 'ram_start', 'ram_end',
                    'ram_bytes', 'stack_margin_policy_bytes', 'retained_relocations'):
            if key in build_report:
                require(build_report[key] == result[key], 'Build report differs: '+key)
                matched.append(key)
        if 'source_sha256' in build_report:
            require(build_report['source_sha256'] == STOCK_SHA, 'Build report stock hash differs')
            matched.append('source_sha256')
        require(matched, 'Build report contains no supported verification fields')
        result['build_report_fields_checked'] = matched
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stock', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--profile', type=Path, default=Path(__file__).with_name('profile.json'))
    parser.add_argument('--report', type=Path, help='Optional build metadata consistency check')
    args = parser.parse_args()
    try:
        profile = json.loads(args.profile.read_text())
        report = json.loads(args.report.read_text()) if args.report else None
        result = verify_bytes(args.stock.read_bytes(), args.candidate.read_bytes(),
                              args.elf.read_bytes(), profile, report)
        result['mode'] = 'read-only-actual-candidate-file'
    except (OSError, ValueError, KeyError, TypeError, UnicodeError, struct.error) as error:
        parser.exit(1, f'Device verification failed: {error}\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
