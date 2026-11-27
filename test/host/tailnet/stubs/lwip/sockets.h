#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdint.h>
typedef struct { uint32_t addr; } ip4_addr_t;
static inline int ip4addr_aton(const char *s, ip4_addr_t *out) { return inet_pton(AF_INET,s,&out->addr); }
#define lwip_ntohl ntohl
#define lwip_ntohs ntohs
