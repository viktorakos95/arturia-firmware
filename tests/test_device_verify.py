"""Synthetic format checks and optional local-file verifier adversarial tests.

Set KS37_TEST_STOCK, KS37_TEST_CANDIDATE and KS37_TEST_ELF to run complete
file tests. No vendor fixtures are bundled. All mutations remain in memory;
fixture framing does not import the builder, packer or verifier encoder.
"""
import copy
import hashlib
import json
import os
import struct
import unittest
from pathlib import Path
from device import verify

def digest(b):return hashlib.sha256(b).hexdigest()
def records(raw):
 binary=bytes.fromhex(raw.decode('ascii'));out=[];at=0
 while at<len(binary):
  n=int.from_bytes(binary[at:at+2],'big')+2;row=binary[at:at+n];assert len(row)==n
  out.append(row);at+=n
 assert len(out)==176
 return out
def payloads(raw):return [None if len(r)==18 else r[18:1042] for r in records(raw)]
def pack(values,repair_overall=True):
 values=list(values)
 if repair_overall:
  image=b''.join(p if p is not None else bytes([255])*1024 for p in values)
  values[-1]=values[-1][:-2]+((-sum(image[:-2]))&65535).to_bytes(2,'little')
 rows=[]
 for index,p in enumerate(values):
  address=(0x4000+1024*index).to_bytes(3,'big')
  body=bytes.fromhex('6874DA5109')+address+bytes(2)
  if p is not None:
   assert len(p)==1024
   body+=bytes.fromhex('0A0000000400')+p+b'\x0b'+address+b'\x04\x00'
   if index==175:body+=bytes.fromhex('0D02FC0000000E02FC000000')
  body+=bytes(4)
  row=(len(body)+2).to_bytes(2,'big')+body+((-sum(body))&65535).to_bytes(2,'little')
  rows.append(row)
 return b''.join(rows).hex().upper().encode()
def edit_record(raw,index,offset,after,repair=True):
 rows=records(raw);row=bytearray(rows[index]);row[offset:offset+len(after)]=after
 if repair:row[-2:]=((-sum(row[2:-2]))&65535).to_bytes(2,'little')
 rows[index]=row
 return b''.join(rows).hex().upper().encode()
def elf_layout(raw):
 shoff=struct.unpack_from('<I',raw,32)[0];shnum=struct.unpack_from('<H',raw,48)[0];shstr=struct.unpack_from('<H',raw,50)[0]
 sections=[list(struct.unpack_from('<10I',raw,shoff+40*i)) for i in range(shnum)]
 string=sections[shstr];strings=raw[string[4]:string[4]+string[5]]
 named={strings[s[0]:strings.index(0,s[0])].decode():dict(index=i,header=shoff+40*i,fields=s) for i,s in enumerate(sections)}
 sym=named['.symtab']['fields'];strings=sections[sym[6]];names=raw[strings[4]:strings[4]+strings[5]]
 symbols={}
 for at in range(sym[4],sym[4]+sym[5],16):
  n,value,size,info,other,section=struct.unpack_from('<IIIBBH',raw,at)
  name=names[n:names.index(0,n)].decode()
  if name:symbols.setdefault(name,[]).append(dict(offset=at,value=value,size=size,info=info,section=section,name_offset=n))
 return named,symbols

class SyntheticBoundary(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  values=[None]*176
  values[0]=bytes(range(256))*4
  values[-1]=bytes([255])*1024
  cls.raw=pack(values)
 def test_synthetic_record_sums(self):
  rows=verify.read_records(self.raw)
  self.assertEqual(len(rows),176)
  self.assertEqual(verify.sums(rows)['ff_gap_application_residue'],0)
 def test_local_corruption_rejected(self):
  bad=edit_record(self.raw,0,18,b'\x01',repair=False)
  with self.assertRaises(verify.VerificationError):verify.read_records(bad)
 def test_overall_corruption_with_local_repair_rejected(self):
  values=payloads(self.raw);last=bytearray(values[-1]);last[-1]^=1;values[-1]=bytes(last)
  with self.assertRaises(verify.VerificationError):verify.sums(verify.read_records(pack(values,False)))
 def test_syntax_bounds(self):
  for bad in (b'',b'00',self.raw.lower(),self.raw+b'\n',self.raw[:-1],self.raw+b'00'):
   with self.subTest(size=len(bad)),self.assertRaises(verify.VerificationError):verify.read_records(bad)
 def test_stock_pin_rejects_synthetic(self):
  with self.assertRaises(verify.VerificationError):verify.expected(self.raw,b'')
 def test_known_branch_decoding(self):
  self.assertEqual(verify.branch_target(0x08012ec0,bytes.fromhex('00f0e4ff')),('bl',0x08013e8c))
  self.assertEqual(verify.branch_target(0x0801d33a,bytes.fromhex('00f0c5fa')),('bl',0x0801d8c8))
 def test_branch_encode_extremes(self):
  for kind in ('bl','b.w'):
   for displacement in (-2**24,-2,0,2,2**24-2):
    place=0x08100000;target=place+4+displacement
    self.assertEqual(verify.branch_target(place,verify.branch_bytes(place,target,kind)),(kind,target))
  for target in (0x08000001,0x08000004+2**24,0x08000004-2**24-2):
   with self.assertRaises(verify.VerificationError):verify.branch_bytes(0x08000000,target,'bl')
 def test_elf_invalid_headers(self):
  for raw in (b'',bytes(52),b'\x7fELF\x02\x01\x01'+bytes(45)):
   with self.assertRaises(verify.VerificationError):verify.read_elf(raw)
 def test_profile_exact_boundaries(self):
  profile=json.loads((Path(__file__).resolve().parents[1]/'device/profile.json').read_text())
  verify.check_profile(profile)
  for key,value in [('code_start','0x0801f402'),('ram_start','0x20005eb8'),('initial_sp','0x20010000'),('stock_sha256','0'*64),('stack_margin',0)]:
   changed=copy.deepcopy(profile);changed[key]=value
   with self.subTest(key=key),self.assertRaises(verify.VerificationError):verify.check_profile(changed)
  for mutator in (lambda p:p['patches'].pop(),lambda p:p['patches'][0].update(before='00000000'),lambda p:p['externals'].update(stock_runtime_init='0x0801d8cb')):
   changed=copy.deepcopy(profile);mutator(changed)
   with self.assertRaises(verify.VerificationError):verify.check_profile(changed)


class LocalDeviceFiles(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  names=('KS37_TEST_STOCK','KS37_TEST_CANDIDATE','KS37_TEST_ELF')
  if not all(os.environ.get(n) for n in names):
   raise unittest.SkipTest('Set KS37_TEST_STOCK, KS37_TEST_CANDIDATE, KS37_TEST_ELF for local-file checks')
  paths=[Path(os.environ[n]) for n in names]
  cls.stock,cls.candidate,cls.elf=(p.read_bytes() for p in paths)
  cls.sections,cls.symbols=elf_layout(cls.elf)
  cls.text=cls.sections['.text']['fields']
  cls.values=payloads(cls.candidate)
  cls.report=verify.verify_bytes(cls.stock,cls.candidate,cls.elf)
  cls.plan={'patches':cls.report['patches']}
  cls.before_files={p:digest(p.read_bytes()) for p in paths}
 def reject(self,candidate=None,elf=None,metadata=None):
  with self.assertRaises(verify.VerificationError):
   verify.verify_bytes(self.stock,self.candidate if candidate is None else candidate,self.elf if elf is None else elf,build_report=metadata)
 def mutate_flash(self,address,value=None,delta=1):
  p=list(self.values);at=address-0x08004000;index,offset=divmod(at,1024);page=bytearray(p[index]);page[offset]=value if value is not None else page[offset]^delta;p[index]=bytes(page);return pack(p)
 def mutate_symbol(self,name,value=None,kind=None,section=None):
  b=bytearray(self.elf);s=self.symbols[name][0];at=s['offset']
  if value is not None:struct.pack_into('<I',b,at+4,value)
  if kind is not None:b[at+12]=(s['info']&240)|kind
  if section is not None:struct.pack_into('<H',b,at+14,section)
  return bytes(b)
 def test_01_actual_candidate_matches_compiled_elf_and_allowlist(self):
  r=verify.verify_bytes(self.stock,self.candidate,self.elf,build_report=self.report)
  self.assertEqual(r['candidate_sha256'],digest(self.candidate));self.assertEqual(r['local_checksums'],176)
  self.assertEqual(len(r['patches']),len(self.plan['patches']));self.assertEqual(r['ff_gap_application_residue'],0)
 def test_02_roundtrip_official_container_using_test_encoder(self):
  self.assertEqual(pack(payloads(self.stock)),self.stock)
 def test_ascii_syntax_case_whitespace_truncation(self):
  for b in [self.candidate.lower(),self.candidate+b'\n',self.candidate[:-1],b'Z'+self.candidate[1:]]:
   with self.subTest(prefix=b[:3]):self.reject(b)
 def test_trailing_records_or_bytes(self):
  self.reject(self.candidate+b'00');self.reject(self.candidate+records(self.stock)[0].hex().upper().encode())
 def test_local_checksum_error(self):
  row=records(self.candidate)[0];self.reject(edit_record(self.candidate,0,18,bytes([row[18]^1]),False))
 def test_overall_error_with_local_checksum_repaired(self):
  p=list(self.values);v=bytearray(p[-1]);v[-1]^=1;p[-1]=bytes(v);self.reject(pack(p,False))
 def test_framing_address_and_command_after_local_repair(self):
  for off,data in [(7,b'\x01'),(10,b'\x01'),(12,b'\x09'),(1042,b'\x0a'),(-6,b'\x01')]:
   with self.subTest(offset=off):self.reject(edit_record(self.candidate,0,off,data))
 def test_final_command_after_local_repair(self):self.reject(edit_record(self.candidate,175,1048,b'\x0c'))
 def test_unauthorized_stock_change_with_both_checksums_repaired(self):self.reject(self.mutate_flash(0x08004500))
 def test_balanced_corruption_preserves_both_sums(self):
  p=list(self.values);v=bytearray(p[0]);a=next(i for i in range(0x200,0x300) if v[i]<255);b=next(i for i in range(0x200,0x300) if i!=a and v[i]>0)
  v[a]+=1;v[b]-=1;p[0]=bytes(v);candidate=pack(p)
  self.assertEqual(records(candidate)[0][-2:],records(self.candidate)[0][-2:])
  self.assertEqual(payloads(candidate)[-1][-2:],self.values[-1][-2:])
  self.reject(candidate)
 def test_elf_payload_corruption_with_all_checksums_repaired(self):self.reject(self.mutate_flash(0x0801f400+100))
 def test_non_ff_injection_padding_after_exact_text_extent(self):self.reject(self.mutate_flash(0x0801f400+self.text[5],value=0))
 def test_extra_empty_ff_record_expansion(self):
  p=list(self.values);index=next(i for i,x in enumerate(p) if x is None);p[index]=bytes([255])*1024;self.reject(pack(p))
 def test_missing_elf_record_with_derived_new_checksums(self):
  p=list(self.values);p[110]=None;self.reject(pack(p))
 def test_each_hook_rejects_corruption_after_sum_repair(self):
  for patch in self.plan['patches']:
   with self.subTest(hook=patch['target']):self.reject(self.mutate_flash(int(patch['address'],16)))
 def test_nop_padding_is_exact(self):
  for patch in self.plan['patches']:
   if len(bytes.fromhex(patch['after']))>4:
    with self.subTest(hook=patch['target']):self.reject(self.mutate_flash(int(patch['address'],16)+4))
 def test_elf_truncated_section_header(self):
  b=bytearray(self.elf);struct.pack_into('<I',b,32,len(b)-20);self.reject(elf=bytes(b))
 def test_elf_wrong_machine_entry(self):
  b=bytearray(self.elf);struct.pack_into('<H',b,18,3);self.reject(elf=bytes(b))
  b=bytearray(self.elf);struct.pack_into('<I',b,24,0x0801f400);self.reject(elf=bytes(b))
 def test_elf_writable_code_or_wrong_ram_flags(self):
  for section,flags in [('.text',7),('.boot_ram',2)]:
   b=bytearray(self.elf);struct.pack_into('<I',b,self.sections[section]['header']+8,flags)
   with self.subTest(section=section):self.reject(elf=bytes(b))
 def test_elf_ram_extent_and_payload_ownership(self):
  b=bytearray(self.elf);at=self.sections['.boot_ram']['header'];struct.pack_into('<I',b,at+20,0x10000);self.reject(elf=bytes(b))
  self.reject(elf=self.mutate_symbol('__boot_payload_start',value=0x20005ef8))
  self.reject(elf=self.mutate_symbol('runtime_storage',section=self.sections['.text']['index']))
 def test_elf_duplicate_required_symbol(self):
  b=bytearray(self.elf);a=self.symbols['ks37_panel_pixel_hook'][0];other=self.symbols['ks37_panel_display_hook'][0]
  struct.pack_into('<I',b,other['offset'],a['name_offset']);self.reject(elf=bytes(b))
 def test_elf_stock_continuation_changed(self):self.reject(elf=self.mutate_symbol('__gp_spi_send',value=0x0800a671))
 def test_elf_hook_must_be_odd_thumb(self):
  s=self.symbols['ks37_panel_pixel_hook'][0];self.reject(elf=self.mutate_symbol('ks37_panel_pixel_hook',value=s['value']&~1))
 def test_elf_hook_cannot_be_retyped_data_target(self):
  target=self.symbols['__gd_rodata_start'][0]['value']|1
  with self.assertRaises(verify.VerificationError):verify.read_elf(self.mutate_symbol('ks37_panel_pixel_hook',value=target,kind=1))
 def test_elf_hook_requires_function_type_even_at_current_address(self):
  with self.assertRaises(verify.VerificationError):verify.read_elf(self.mutate_symbol('ks37_panel_pixel_hook',kind=1))
 def test_elf_unreviewed_relocation_and_symbol_index(self):
  rel=self.sections['.rel.text']['fields'];at=rel[4];info=struct.unpack_from('<I',self.elf,at+4)[0]
  for new in [(info&~255)|255,0xffffff0a]:
   b=bytearray(self.elf);struct.pack_into('<I',b,at+4,new);self.reject(elf=bytes(b))
 def test_elf_branch_relocation_no_longer_matches_code(self):
  rel=self.sections['.rel.text']['fields'];place=struct.unpack_from('<I',self.elf,rel[4])[0]
  b=bytearray(self.elf);b[self.text[4]+place-self.text[3]]^=1;self.reject(elf=bytes(b))
 def test_elf_relocation_audit_cannot_be_dropped(self):
  b=bytearray(self.elf);struct.pack_into('<I',b,self.sections['.rel.text']['header']+20,0);self.reject(elf=bytes(b))
 def test_build_report_size_and_hashes(self):
  for key,value in [('candidate_sha256','0'*64),('candidate_bytes',0),('elf_sha256','0'*64),('code_sha256','0'*64),('runtime_bytes',0),('ram_end','0x2000c000'),('stack_margin_policy_bytes',0)]:
   m=copy.deepcopy(self.report);m[key]=value
   with self.subTest(key=key):self.reject(metadata=m)
 def test_build_report_cannot_be_empty(self):self.reject(metadata={})
 @classmethod
 def tearDownClass(cls):
  for p,h in cls.before_files.items():assert digest(p.read_bytes())==h,'Read-only test input changed'


if __name__=="__main__":
 unittest.main()
