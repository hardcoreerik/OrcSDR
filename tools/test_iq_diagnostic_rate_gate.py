"""Compile the production diagnostic routing slice, including enclosing conditions."""
from pathlib import Path
import subprocess, tempfile, sys, re
root=Path(__file__).resolve().parents[1]
s=(root/'apps/orcsdr-tab5/ui/main.cpp').read_text()
a=s.index('// Raw diagnostic IQ');b=s.index('mark(dsp_stats::Stage::record);',a)
# Include the containing scope from the preceding recorder branch boundary.
def enclosing_contexts(prefix):
 # Strip comments and literals before tracking the scopes containing the append.
 clean=re.sub(r"""//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'""", "", prefix, flags=re.S)
 stack=[];context=''
 for ch in clean:
  if ch=='{':stack.append(context[-400:]);context=''
  elif ch=='}':
   if stack:stack.pop()
   context=''
  elif ch==';':context=''
  else:context+=ch
 return stack
assert any('custom_rate' in c for c in enclosing_contexts('void f(){if (!block.custom_rate) {'))
assert not any('custom_rate' in c for c in enclosing_contexts(s[:a])), 'Enclosing scope excludes custom rates'
fragment=s[a:b]
assert 'custom_rate' not in fragment, 'Custom rates must not be excluded'
assert 'block.sample_rate_sps == g_iq_rec_sample_rate_sps' in fragment
code=r"""
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cassert>
enum class IqCaptureKind { diagnostic, other };
std::atomic<IqCaptureKind> g_iq_rec_kind{IqCaptureKind::diagnostic};
uint32_t g_iq_rec_sample_rate_sps;int appends=0;
struct Block {const uint8_t* data=nullptr;size_t bytes=8;uint32_t sample_rate_sps;bool custom_rate;} block;
void iq_rec_append(const uint8_t*,size_t){++appends;}
void route(){
"""+fragment+r"""
}
int main(){
 for(auto rate:{960000u,2400000u,3200000u}){
  g_iq_rec_sample_rate_sps=rate;block.sample_rate_sps=rate;
  for(bool custom:{false,true}){block.custom_rate=custom;appends=0;route();if(appends!=1)return 1;}
  block.sample_rate_sps=rate+1;appends=0;route();if(appends!=0)return 2;
 }
 g_iq_rec_kind=IqCaptureKind::other;block.sample_rate_sps=g_iq_rec_sample_rate_sps;appends=0;route();return appends?3:0;
}
"""
code='#include <initializer_list>\n'+code
with tempfile.TemporaryDirectory(prefix='diagnostic-rate-') as folder:
 out=Path(folder);(out/'test.cpp').write_text(code)
 (out/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.16)\nproject(rate_gate LANGUAGES CXX)\nadd_executable(rate_gate test.cpp)\n')
 subprocess.run(['cmake','-S',str(out),'-B',str(out/'build')],check=True,stdout=subprocess.DEVNULL)
 subprocess.run(['cmake','--build',str(out/'build'),'--config','Debug'],check=True,stdout=subprocess.DEVNULL)
 exe=out/'build/Debug/rate_gate.exe' if sys.platform=='win32' else out/'build/rate_gate'
 subprocess.run([str(exe)],check=True)
print('PASS production diagnostic routing: matching custom/default rates accepted, mismatched rates rejected')
