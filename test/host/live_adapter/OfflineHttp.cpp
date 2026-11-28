// Offline Library contract fixture: the production factory/adapter remain linked,
// but this test intentionally has no socket, TLS, filesystem or worker runtime.
#include "booksync/server/bookorbit/LiveAdapter.h"
namespace booksync::server {
HttpPort& deviceHttpPort() {
  class Offline final : public HttpPort {
    int open(const HttpLogin&, const char*, std::string_view, std::string_view, size_t) override { return 0; }
    int read(char*, size_t) override { return -1; }
    int finish() override { return 0; }
    void close() override {}
  };
  static Offline port;
  return port;
}
}  // namespace booksync::server
