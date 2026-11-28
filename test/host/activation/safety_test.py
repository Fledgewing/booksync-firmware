#!/usr/bin/env python3
"""Compile actual boot guard and strict key loader, without mutation APIs."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[3]
temporary_root=ROOT/'build/activation-tmp'
temporary_root.mkdir(parents=True,exist_ok=True)
class Safety(unittest.TestCase):
    def test_pre_arduino_initialization_never_formats(self):
        source = (ROOT/'src/booksync/system/NvsGuard.cpp').read_text()
        with tempfile.TemporaryDirectory(dir=temporary_root) as directory:
            p = Path(directory)
            (p/'nvs_flash.h').write_text('''#pragma once
using esp_err_t=int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_NVS_NO_FREE_PAGES=1, ESP_ERR_NVS_NEW_VERSION_FOUND=2;
esp_err_t nvs_flash_init();
''')
            (p/'esp_rom_sys.h').write_text('#define esp_rom_printf(...) ((void)0)\n')
            (p/'test.cpp').write_text('''#include <cassert>
#include "nvs_flash.h"
int result=0, calls=0, formats=0;
unsigned char partition[3]={1,2,3};
extern "C" int __real_nvs_flash_init(){ ++calls; return result; }
extern "C" int __wrap_nvs_flash_init();
void preArduinoBoot() {
 int err=__wrap_nvs_flash_init();
 if(err==ESP_ERR_NVS_NO_FREE_PAGES || err==ESP_ERR_NVS_NEW_VERSION_FOUND) {
  ++formats; for(auto& byte:partition) byte=0;
 }
}
int main(){
 for(int error: {0,1,2,3,-1}) {
  result=error;
  int returned=__wrap_nvs_flash_init();
  assert(returned != ESP_ERR_NVS_NO_FREE_PAGES && returned != ESP_ERR_NVS_NEW_VERSION_FOUND);
  assert((returned==0)==(error==0));
  preArduinoBoot(); preArduinoBoot();
  assert(formats==0 && partition[0]==1 && partition[1]==2 && partition[2]==3);
 }
 assert(calls==15);
}
'''.replace('#include <cassert>', '#include <cassert>\n#include <initializer_list>'))
            subprocess.run(['c++','-std=c++17','-I'+str(p),str(p/'test.cpp'),str(ROOT/'src/booksync/system/NvsGuard.cpp'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)
    def test_pinned_compilation_removes_generation_and_rejects_source_drift(self):
        import sys
        sys.path.insert(0, str(ROOT/'tailnet'))
        from retained_keys import strict_identity_source
        source=(ROOT/'tailnet/third_party/microlink/components/microlink/src/microlink.c').read_text()
        result=strict_identity_source(source)
        self.assertNotIn('load_or_generate_keys',result)
        self.assertNotIn('generate_keypair',result)
        self.assertIn('booksync_load_retained_keys(ml)',result)
        self.assertIn('#include "RetainedKeys.h"',result)
        with self.assertRaises(ValueError): strict_identity_source(source.replace('generating ephemeral keys','changed'))
    def test_strict_key_load_cuts_every_read(self):
        header = ROOT/'tailnet/shim/RetainedKeys.h'
        self.assertTrue(header.exists())
        with tempfile.TemporaryDirectory(dir=temporary_root) as directory:
            p=Path(directory)
            (p/'test.cpp').write_text('''#include <cassert>
#include <cstring>
#include <cstdint>
#include <cstddef>
using esp_err_t=int;
using nvs_handle_t=int;
constexpr int ESP_OK=0, ESP_FAIL=-1, NVS_READONLY=0;
static int cut=-1, reads=0, closes=0, opens=0, shortRead=-1;
struct microlink_t {
 uint8_t machine_private_key[32], machine_public_key[32], wg_private_key[32], wg_public_key[32], disco_private_key[32], disco_public_key[32];
};
int nvs_open(const char* space,int mode,int* handle) { assert(strcmp(space,"microlink")==0); assert(mode==0); ++opens; *handle=1; return cut==0 ? -1 : 0; }
int nvs_get_blob(int,const char* key,void* data,size_t* bytes) {
 static const char* names[]={"machine_pri","machine_pub","wg_private","wg_public","disco_pri","disco_pub"};
 assert(strcmp(key,names[reads])==0);
 ++reads; if(cut==reads) return -1;
 assert(*bytes==32); memset(data,reads,32); if(shortRead==reads) *bytes=31;
 return 0;
}
void nvs_close(int) { ++closes; }
#include "RetainedKeys.h"
int main(){
 for(int failure=-1; failure<=6; ++failure) {
  reads=closes=opens=0; cut=failure; microlink_t keys{};
  auto result=booksync_load_retained_keys(&keys);
  assert((result==0)==(failure==-1));
  assert(opens==1 && closes==(failure==0 ? 0 : 1));
  if(failure!=-1) for(auto byte: reinterpret_cast<uint8_t(&)[192]>(keys)) assert(byte==0);
 }
 cut=-1;
 for(int failure=1; failure<=6; ++failure) {
  reads=0; shortRead=failure; microlink_t keys{};
  assert(booksync_load_retained_keys(&keys)!=0);
  for(auto byte: reinterpret_cast<uint8_t(&)[192]>(keys)) assert(byte==0);
 }
}
''')
            subprocess.run(['c++','-std=c++17','-I'+str(header.parent),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)
if __name__=='__main__': unittest.main()
