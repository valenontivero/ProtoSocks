#ifndef SOCKS5NIO_H
#define SOCKS5NIO_H
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>

struct selector_key;

void socksv5_passive_accept(struct selector_key *key);

void socksv5_pool_destroy(void);

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#endif