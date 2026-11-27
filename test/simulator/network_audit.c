// SPDX-License-Identifier: GPL-3.0-or-later
// Fake socket transport: record attempts without addresses, then refuse them.
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __APPLE__
#define connect booksync_connect
#define sendto booksync_sendto
#define sendmsg booksync_sendmsg
#define getaddrinfo booksync_getaddrinfo
#endif

static void attempted(void) {
  const char* path = getenv("BOOKSYNC_NETWORK_AUDIT");
  if (path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd >= 0) {
      (void)write(fd, "request\n", 8);
      close(fd);
    }
  }
  errno = ECONNREFUSED;
}

int connect(int fd, const struct sockaddr* addr, socklen_t len) {
  (void)fd;
  (void)addr;
  (void)len;
  attempted();
  return -1;
}
ssize_t sendto(int fd, const void* buf, size_t len, int flags, const struct sockaddr* addr, socklen_t addrlen) {
  (void)fd;
  (void)buf;
  (void)len;
  (void)flags;
  (void)addr;
  (void)addrlen;
  attempted();
  return -1;
}
ssize_t sendmsg(int fd, const struct msghdr* msg, int flags) {
  (void)fd;
  (void)msg;
  (void)flags;
  attempted();
  return -1;
}

int getaddrinfo(const char* node, const char* service, const struct addrinfo* hints, struct addrinfo** result) {
  (void)node;
  (void)service;
  (void)hints;
  (void)result;
  attempted();
  return EAI_FAIL;
}

#ifdef __APPLE__
#undef connect
#undef sendto
#undef sendmsg
#undef getaddrinfo
// Mach-O dyld interposition pairs; ELF uses the exported function names above.
__attribute__((used, section("__DATA,__interpose,interposing"))) static const void* booksync_interpose[][2] = {
    {(const void*)booksync_connect, (const void*)connect},
    {(const void*)booksync_sendto, (const void*)sendto},
    {(const void*)booksync_sendmsg, (const void*)sendmsg},
    {(const void*)booksync_getaddrinfo, (const void*)getaddrinfo},
};
#endif
