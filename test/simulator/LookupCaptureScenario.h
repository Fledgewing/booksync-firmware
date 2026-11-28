// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <HalStorage.h>
#include <Logging.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "booksync/services/LookupHistorySource.h"
inline void runLookupCaptureScenario() {
  if (!std::getenv("BOOKSYNC_LOOKUP_CAPTURE_SCENARIO")) return;
  using namespace booksync;
  static LookupHistorySource source;
  static LookupEntry entry;
  bool ok = true;
  auto write = [&](const char* folder, const char* suffix, const std::string& bytes) {
    Storage.mkdir(folder);
    char path[160]; std::snprintf(path,sizeof(path),"%s/dictionary_history.txt%s",folder,suffix);
    auto file = Storage.open(path,O_WRONLY|O_CREAT|O_TRUNC);
    if (!file) return false;
    const bool written = file.write(bytes.data(),bytes.size())==bytes.size() && file.sync();
    const bool closed = file.close(); return written && closed;
  };
  auto drain = [&](unsigned& words) {
    words=0;
    for (unsigned steps=0;steps<1000;++steps) {
      const auto result=source.tick(entry,7);
      if(result==LookupStep::Record) ++words;
      else if(result!=LookupStep::Pending && result!=LookupStep::EmptyLine) return result;
    }
    return LookupStep::IoError;
  };
  ok = Storage.mkdir("/.booksync-word-fixture") && ok;
  ok = write("/.booksync-word-fixture/known","","river|D\rcloud|T\r\nwind|X\n") && ok;
  ok = source.begin("/.booksync-word-fixture/known")==LookupStep::Pending && ok;
  unsigned words=0; ok = drain(words)==LookupStep::End && words==3 && source.complete() && ok;
  std::string unicode; unicode.reserve(390); for(unsigned i=0;i<96;++i) unicode+="水";
  ok = write("/.booksync-word-fixture/unicode","",unicode+"|D\n") && ok;
  ok = source.begin("/.booksync-word-fixture/unicode")==LookupStep::Pending && ok;
  ok = source.tick(entry,65535)==LookupStep::Pending && source.lookupConsumedBytes()<=256 && ok;
  ok = drain(words)==LookupStep::End && words==1 && std::string(entry.word.data())==unicode && ok;
  ok = write("/.booksync-word-fixture/torn","","river|D\nunfinished|") && ok;
  ok = source.begin("/.booksync-word-fixture/torn")==LookupStep::Pending && ok;
  ok = drain(words)==LookupStep::End && words==1 && !source.complete() && ok;
  ok = write("/.booksync-word-fixture/backup",".bak","cloud|D\n") && ok;
  ok = write("/.booksync-word-fixture/backup",".tmp","staging|D\n") && ok;
  ok = source.begin("/.booksync-word-fixture/backup")==LookupStep::Pending && source.fromBackup() && ok;
  ok = drain(words)==LookupStep::End && words==1 && std::strcmp(entry.word.data(),"cloud")==0 && ok;
  ok = write("/.booksync-word-fixture/changed","","river|D\n") && ok;
  ok = source.begin("/.booksync-word-fixture/changed")==LookupStep::Pending && ok;
  ok = write("/.booksync-word-fixture/changed","","river|D\ncloud|D\n") && ok;
  ok = source.tick(entry)==LookupStep::Changed && ok;
  ok = write("/.booksync-word-fixture/large","",std::string(97,'a')+"|D\n") && ok;
  ok = source.begin("/.booksync-word-fixture/large")==LookupStep::Pending && ok;
  ok = drain(words)==LookupStep::Oversize && words==0 && ok;
  ok = write("/.booksync-word-fixture/malformed","","river|Q\n") && ok;
  ok = source.begin("/.booksync-word-fixture/malformed")==LookupStep::Pending && ok;
  ok = drain(words)==LookupStep::Malformed && words==0 && ok;
  ok = source.begin("/.booksync-word-fixture/missing")==LookupStep::Missing && ok;
  LOG_INF("BSW","BookSync lookup capture %s: cases=8 scratch=%u requested-alloc=0 native-writes=0",ok?"PASS":"FAIL",unsigned(sizeof(source)));
  std::fflush(nullptr); std::_Exit(ok?0:1);
}
