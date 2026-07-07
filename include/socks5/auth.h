#ifndef AUTH_H
#define AUTH_H

#include <stdbool.h>
#include <stdint.h>

#include "buffer.h"

#define SOCKS_AUTH_VERSION 0x01
#define SOCKS_AUTH_STATUS_SUCCESS 0x00
#define SOCKS_AUTH_STATUS_FAILURE 0x01
#define SOCKS_AUTH_MAX_FIELD 255

enum auth_state {
    AUTH_VERSION,
    AUTH_ULEN,
    AUTH_UNAME,
    AUTH_PLEN,
    AUTH_PASSWD,
    AUTH_DONE,
    AUTH_ERROR,
};

struct auth_parser {
    enum auth_state state;
    uint8_t username[SOCKS_AUTH_MAX_FIELD + 1];
    uint8_t password[SOCKS_AUTH_MAX_FIELD + 1];
    uint8_t username_len;
    uint8_t password_len;
    uint8_t pos;
};

void auth_parser_init(struct auth_parser *p);

enum auth_state auth_consume(buffer *b, struct auth_parser *p, bool *errored);

bool auth_is_done(const enum auth_state state, bool *errored);

int auth_marshall(buffer *b, const uint8_t status);

#endif

