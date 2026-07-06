#ifndef REQUEST_H
#define REQUEST_H

#include <stdbool.h>
#include <stdint.h>

#include "buffer.h"

#define SOCKS_REQUEST_VERSION 0x05

#define SOCKS_REQUEST_CMD_CONNECT 0x01

#define SOCKS_REQUEST_ATYP_IPV4 0x01
#define SOCKS_REQUEST_ATYP_DOMAIN 0x03
#define SOCKS_REQUEST_ATYP_IPV6 0x04

#define SOCKS_REPLY_SUCCEEDED 0x00
#define SOCKS_REPLY_GENERAL_FAILURE 0x01
#define SOCKS_REPLY_COMMAND_NOT_SUPPORTED 0x07
#define SOCKS_REPLY_ADDRESS_TYPE_NOT_SUPPORTED 0x08

#define SOCKS_REQUEST_MAX_DOMAIN 255
#define SOCKS_REQUEST_MAX_ADDR 16

enum request_state {
    REQUEST_VERSION,
    REQUEST_CMD,
    REQUEST_RSV,
    REQUEST_ATYP,
    REQUEST_DST_ADDR,
    REQUEST_DST_PORT,
    REQUEST_DONE,
    REQUEST_ERROR,
};

struct request_parser {
    enum request_state state;
    uint8_t cmd;
    uint8_t atyp;
    uint8_t addr[SOCKS_REQUEST_MAX_ADDR];
    uint8_t domain[SOCKS_REQUEST_MAX_DOMAIN + 1];
    uint8_t domain_len;
    uint8_t addr_pos;
    uint8_t port_pos;
    uint16_t port;
};

void request_parser_init(struct request_parser *p);

enum request_state request_consume(buffer *b, struct request_parser *p, bool *errored);

bool request_is_done(const enum request_state state, bool *errored);

uint8_t request_reply_for(const struct request_parser *p);

int request_marshall(buffer *b, const uint8_t reply);

int request_marshall_ipv4(buffer *b, const uint8_t reply,
                          const uint8_t addr[4], const uint16_t port);

#endif
