"""Bounded Unity managed-reference decoding using each file's exact ref schemas.

The stock metadata dump skips ReferencedObjectData payloads and loses alignment.
This reader resolves their actual class/ns/assembly schema; only emitted local
CharacterData blobs are read. No guessed binary field offsets or game writes.
"""
from pathlib import Path
import struct,json
from endfield_profiles import read,write,ROOT
def tree(nodes):
 root=None;stack=[]
 for item in nodes:
  node=dict(item);node['children']=[];level=node['m_Level']
  while len(stack)>level:stack.pop()
  if stack:stack[-1]['children'].append(node)
  else:root=node
  stack.append(node)
 return root
FORMATS={'int':'i','SInt32':'i','UInt32':'I','unsigned int':'I','float':'f','double':'d','SInt64':'q','UInt64':'Q','long long':'q','unsigned long long':'Q','short':'h','SInt16':'h','unsigned short':'H','UInt16':'H','UInt8':'B','SInt8':'b','char':'b','bool':'B','Boolean':'B'}
class Decoder:
 def __init__(self,data,refs):
  self.data=data;self.at=0;self.types={(r['m_KlassName'],r['m_NameSpace'],r['m_AsmName']):tree(r['m_Type']['m_Nodes']) for r in refs};self.objects=[];self.payload_depth=0
 def align(self):self.at=(self.at+3)//4*4
 def scalar(self,fmt):
  size=struct.calcsize('<'+fmt)
  if self.at+size>len(self.data):raise ValueError('read outside source object')
  value=struct.unpack_from('<'+fmt,self.data,self.at)[0];self.at+=size;return value
 def parse(self,node,context=None,depth=0):
  if depth>100:raise ValueError('reference schema recursion')
  kind=node['m_Type'];children=node['children'];start=self.at
  # Registry metadata on referenced type schemas describes the shared host
  # registry. Its bytes occur once on the owning MonoBehaviour, not per object.
  if kind=='ManagedReferencesRegistry' and self.payload_depth:return {}
  if kind in FORMATS:result=self.scalar(FORMATS[kind])
  elif kind=='string':
   size=self.scalar('i')
   if not(0<=size<=len(self.data)-self.at):raise ValueError(f'invalid string size {size} at {self.at-4} ({node["m_Name"]})')
   result=self.data[self.at:self.at+size].decode('utf-8',errors='strict');self.at+=size;self.align()
  elif kind=='ReferencedObjectData':
   description=(context or {}).get('type',{});key=(description.get('class',''),description.get('ns',''),description.get('asm',''))
   if not key[0]:result={}
   elif key not in self.types:raise ValueError('missing native managed schema '+str(key))
   else:
    self.payload_depth+=1
    try:result=self.parse(self.types[key],None,depth+1)
    finally:self.payload_depth-=1
  elif kind=='Array':
   size=self.scalar('i')
   if size<0 or size>65536:raise ValueError(f'invalid array {size} at {start}')
   element=children[1];result=[self.parse(element,None,depth+1) for _ in range(size)]
  elif len(children)==1 and children[0]['m_Type']=='Array':result=self.parse(children[0],None,depth+1)
  else:
   result={}
   for child in children:result[child['m_Name']]=self.parse(child,result,depth+1)
   if kind=='ReferencedObject':self.objects.append({'offset':start,'end':self.at,**result})
  if node['m_MetaFlag']&0x4000:self.align()
  return result
def main():
 folder=ROOT/'research/weapon-coverage/config-trees'
 for file in folder.glob('asset-data_chr_*.raw'):
  prefix=file.with_suffix('');refs=read(prefix.with_suffix('.reftypes.json'))['m_RefTypes'];nodes=read(prefix.with_suffix('.nodes.json'));data=file.read_bytes();decoder=Decoder(data,refs);result=decoder.parse(tree(nodes))
  if decoder.at!=len(data):raise ValueError(f'{file.name}: consumed {decoder.at}/{len(data)}')
  write(prefix.with_suffix('.decoded.json'),{'schema':1,'raw_bytes':len(data),'bytes_consumed':decoder.at,'data':result,'managed_objects':decoder.objects,'coordinate_fields':'native file type schema; no inferred offsets','runtime_verified':False})
  print(file.name,'bytes',decoder.at,'managed objects',len(decoder.objects))
if __name__=='__main__':main()
