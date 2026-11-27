// SPDX-License-Identifier: GPL-3.0-or-later
// Fake socket transport: record attempts without addresses, then refuse them.
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __APPLE__
#define connect booksync_connect
#define sendto booksync_sendto
#define sendmsg booksync_sendmsg
#define getaddrinfo booksync_getaddrinfo
#endif

static void append(const char* variable, const char* text, size_t size) {
  const char* path = getenv(variable);
  if (!path) return;
  int file = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
  if (file >= 0) {
    (void)write(file, text, size);
    close(file);
  }
}

static int socket_family(int fd, const struct sockaddr* address, socklen_t length) {
  if (address && length >= sizeof(struct sockaddr)) return address->sa_family;
  struct sockaddr_storage local;
  socklen_t size = sizeof(local);
  if (getsockname(fd, (struct sockaddr*)&local, &size) == 0) return local.ss_family;
  return -1;  // Unknown families remain fatal, rather than escaping the audit.
}

static void attempted(const char* operation, int family) {
  char detail[64];
  int length = snprintf(detail, sizeof(detail), "%s family=%d\n", operation, family);
  if (family == AF_UNIX) {
    // Desktop SDL may contact a local bus. This is host IPC, not an Internet
    // request; retain it separately, still refusing the call in this fake.
    append("BOOKSYNC_LOCAL_IPC_AUDIT", detail, (size_t)length);
  } else {
    append("BOOKSYNC_NETWORK_AUDIT", "request\n", 8);
    append("BOOKSYNC_NETWORK_DETAILS", detail, (size_t)length);
  }
  errno = ECONNREFUSED;
}

int connect(int fd, const struct sockaddr* addr, socklen_t len) {
  attempted("connect", socket_family(fd, addr, len));
  return -1;
}
ssize_t sendto(int fd, const void* buf, size_t len, int flags, const struct sockaddr* addr, socklen_t addrlen) {
  (void)buf;
  (void)len;
  (void)flags;
  attempted("sendto", socket_family(fd, addr, addrlen));
  return -1;
}
ssize_t sendmsg(int fd, const struct msghdr* msg, int flags) {
  (void)flags;
  attempted("sendmsg", socket_family(fd, msg ? msg->msg_name : NULL, msg ? msg->msg_namelen : 0));
  return -1;
}

int getaddrinfo(const char* node, const char* service, const struct addrinfo* hints, struct addrinfo** result) {
  (void)node;
  (void)service;
  (void)hints;
  (void)result;
  attempted("dns", -1);
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
