#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile production persistence/key functions with F03 synthetic NVS, no radio."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
SHIM = ROOT / 'tailnet/shim/Tailnet.cpp'
ML = ROOT / 'tailnet/third_party/microlink/components/microlink/src/microlink.c'


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class Identity(unittest.TestCase):
    def test_namespace_keys_and_types_against_f03(self):
        entries = json.loads((ROOT / 'test/fixtures/legacy/nvs/entries.json').read_text())['entries']
        source = SHIM.read_text()
        keys = dict(re.findall(r'const char\* (NVS_\w+) = "([^"]+)";', source))
        self.assertEqual(keys.pop('NVS_NS'), 'tailnet')
        expected = {e['key']: e['type'] for e in entries if e['namespace'] == 'tailnet'}
        self.assertEqual(set(keys.values()), set(expected))
        self.assertEqual({k for k,v in expected.items() if v == 'u8'}, {'enabled','joined'})
        self.assertEqual({k for k,v in expected.items() if v == 'str'}, {'login_url'})
        # Persistence helpers are byte-identical to the integration source.
        provenance = json.loads((ROOT / 'tailnet/provenance.json').read_text())
        for signature, digest in provenance['persistence_functions'].items():
            self.assertEqual(hashlib.sha256(function(source, signature).encode()).hexdigest(), digest)
        mlkeys = dict(re.findall(r'#define (NVS_\w+)\s+"([^"]+)"', ML.read_text()))
        self.assertEqual(mlkeys.pop('NVS_NAMESPACE'), 'microlink')
        self.assertEqual(set(mlkeys.values()), {e['key'] for e in entries if e['namespace'] == 'microlink'})

    def test_two_boots_reuse_identity_without_writes_or_random(self):
        source = ML.read_text()
        entries = json.loads((ROOT / 'test/fixtures/legacy/nvs/entries.json').read_text())['entries']
        blobs = {e['key']: e['hex'] for e in entries if e['namespace'] == 'microlink'}
        definitions = '\n'.join(re.findall(r'^#define NVS_.*$', source, re.M))
        fields = ['machine_private_key','machine_public_key','wg_private_key','wg_public_key','disco_private_key','disco_public_key']
        code = '''#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
using esp_err_t = int;
using nvs_handle_t = int;
constexpr int ESP_OK = 0, NVS_READWRITE = 1, ESP_MAC_WIFI_STA = 0;
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
static int randoms = 0, writes = 0, opens = 0, closes = 0;
static std::map<std::string, std::vector<uint8_t>> data;
struct microlink_t {'''
        code += ''.join('uint8_t '+field+'[32];' for field in fields) + '};\n'
        code += '''int nvs_open(const char* ns, int, int* h) { assert(strcmp(ns,"microlink")==0); *h=1; ++opens; return 0; }
int nvs_get_blob(int, const char* key, void* dst, size_t* len) {
  auto it=data.find(key); if(it==data.end()) return 1;
  assert(*len>=it->second.size()); *len=it->second.size(); memcpy(dst,it->second.data(),*len); return 0;
}
int nvs_set_blob(int, const char*, const void*, size_t) { ++writes; return 0; }
int nvs_commit(int) { ++writes; return 0; }
void nvs_close(int) { ++closes; }
void esp_fill_random(void*, size_t) { ++randoms; }
void x25519_base(uint8_t*, const uint8_t*, int) {}
void esp_read_mac(uint8_t* out, int) { const uint8_t mac[6]={2,0,0,0,0,1}; memcpy(out,mac,6); }
#define CONFIG_ML_DEVICE_NAME ""
'''
        code += definitions+'\n'+function(source,'static void generate_keypair(')+'\n'
        code += function(source,'static esp_err_t load_or_generate_keys(')+'\n'
        code += function(source,'const char *microlink_default_device_name(')+'\n'
        code += 'int main() {\n'
        for key, value in blobs.items():
            code += 'data["'+key+'"]={'+','.join(str(b) for b in bytes.fromhex(value))+'};\n'
        code += '''microlink_t first{}, second{};
assert(load_or_generate_keys(&first)==0);
assert(load_or_generate_keys(&second)==0);
assert(memcmp(&first,&second,sizeof(first))==0);
assert(randoms==0 && writes==0 && opens==2 && closes==2);
'''
        for field, key in zip(fields, blobs):
            code += 'assert(memcmp(first.'+field+', data["'+key+'"].data(), 32)==0);\n'
        # Compare bytes, do not emit the synthetic device name into logs.
        code += 'const char expected[]={101,115,112,51,50,45,48,48,48,48,48,49,0};\n'
        code += 'assert(strcmp(microlink_default_device_name(),expected)==0);\n}\n'
        with tempfile.TemporaryDirectory(prefix='tailnet-identity-') as temp:
            path = Path(temp)/'identity.cpp'
            path.write_text(code)
            executable = Path(temp)/'identity'
            subprocess.run(['c++','-std=c++17',str(path),'-o',str(executable)], check=True)
            subprocess.run([str(executable)],check=True)

    def test_absent_optional_nvs_is_not_rewritten_by_shim(self):
        source = SHIM.read_text()
        optional = json.loads((ROOT/'test/fixtures/legacy/nvs/optional-keys.json').read_text())['absent']
        for entry in optional:
            self.assertNotIn('"'+entry['key']+'"', source)
        self.assertIn('cfg.device_name = nullptr;', source)
        self.assertIn('#define CONFIG_ML_DEVICE_NAME ""', (ROOT/'tailnet/ml_config.h').read_text())


if __name__ == '__main__':
    unittest.main()
