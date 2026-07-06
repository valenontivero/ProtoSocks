/*
Sacado del RFC 1928:
 Once the method-dependent subnegotiation has completed, the client
   sends the request details.  If the negotiated method includes
   encapsulation for purposes of integrity checking and/or
   confidentiality, these requests MUST be encapsulated in the method-
   dependent encapsulation.

   The SOCKS request is formed as follows:

        +----+-----+-------+------+----------+----------+
        |VER | CMD |  RSV  | ATYP | DST.ADDR | DST.PORT |
        +----+-----+-------+------+----------+----------+
        | 1  |  1  | X'00' |  1   | Variable |    2     |
        +----+-----+-------+------+----------+----------+

     Where:

          o  VER    protocol version: X'05'
          o  CMD
             o  CONNECT X'01'
             o  BIND X'02'
             o  UDP ASSOCIATE X'03'
          o  RSV    RESERVED
          o  ATYP   address type of following address
             o  IP V4 address: X'01'
             o  DOMAINNAME: X'03'
             o  IP V6 address: X'04'
          o  DST.ADDR       desired destination address
          o  DST.PORT desired destination port in network octet
             order

   The SOCKS server will typically evaluate the request based on source
   and destination addresses, and return one or more reply messages, as
   appropriate for the request type.
   */
#include "request.h"

#include <stddef.h>
#include <string.h>

static uint8_t address_length(const struct request_parser *p) {
    switch(p->atyp) {
        case SOCKS_REQUEST_ATYP_IPV4:
            return 4;
        case SOCKS_REQUEST_ATYP_IPV6:
            return 16;
        case SOCKS_REQUEST_ATYP_DOMAIN:
            return p->domain_len;
        default:
            return 0;
    }
}

void request_parser_init(struct request_parser *p) {
    memset(p, 0, sizeof(*p));
    p->state = REQUEST_VERSION;
}

enum request_state request_consume(buffer *b, struct request_parser *p, bool *errored) {
    while(buffer_can_read(b) && p->state != REQUEST_DONE && p->state != REQUEST_ERROR) {
        const uint8_t c = buffer_read(b);

        switch(p->state) {
            case REQUEST_VERSION:
                p->state = (c == SOCKS_REQUEST_VERSION) ? REQUEST_CMD : REQUEST_ERROR;
                break;

            case REQUEST_CMD:
                p->cmd = c;
                p->state = REQUEST_RSV;
                break;

            case REQUEST_RSV:
                p->state = (c == 0x00) ? REQUEST_ATYP : REQUEST_ERROR;
                break;

            case REQUEST_ATYP:
                p->atyp = c;
                if(c == SOCKS_REQUEST_ATYP_IPV4 || c == SOCKS_REQUEST_ATYP_IPV6) {
                    p->state = REQUEST_DST_ADDR;
                } else if(c == SOCKS_REQUEST_ATYP_DOMAIN) {
                    p->state = REQUEST_DST_ADDR;
                    p->domain_len = 0;
                } else {
                    p->state = REQUEST_DONE;
                }
                break;

            case REQUEST_DST_ADDR:
                if(p->atyp == SOCKS_REQUEST_ATYP_DOMAIN && p->domain_len == 0) {
                    p->domain_len = c;
                    if(p->domain_len == 0) {
                        p->state = REQUEST_ERROR;
                    }
                    break;
                }

                if(p->atyp == SOCKS_REQUEST_ATYP_DOMAIN) {
                    p->domain[p->addr_pos++] = c;
                    if(p->addr_pos == p->domain_len) {
                        p->domain[p->domain_len] = 0;
                        p->state = REQUEST_DST_PORT;
                    }
                } else {
                    p->addr[p->addr_pos++] = c;
                    if(p->addr_pos == address_length(p)) {
                        p->state = REQUEST_DST_PORT;
                    }
                }
                break;

            case REQUEST_DST_PORT:
                p->port = (uint16_t)((p->port << 8) | c);
                p->port_pos++;
                if(p->port_pos == 2) {
                    p->state = REQUEST_DONE;
                }
                break;

            case REQUEST_DONE:
            case REQUEST_ERROR:
                break;
        }
    }

    if(p->state == REQUEST_ERROR && errored != NULL) {
        *errored = true;
    }
    return p->state;
}

bool request_is_done(const enum request_state state, bool *errored) {
    if(state == REQUEST_ERROR) {
        if(errored != NULL) {
            *errored = true;
        }
        return false;
    }
    return state == REQUEST_DONE;
}

uint8_t request_reply_for(const struct request_parser *p) {
    if(p->cmd != SOCKS_REQUEST_CMD_CONNECT) {
        return SOCKS_REPLY_COMMAND_NOT_SUPPORTED;
    }
    if(p->atyp != SOCKS_REQUEST_ATYP_IPV4
            && p->atyp != SOCKS_REQUEST_ATYP_DOMAIN
            && p->atyp != SOCKS_REQUEST_ATYP_IPV6) {
        return SOCKS_REPLY_ADDRESS_TYPE_NOT_SUPPORTED;
    }
    return SOCKS_REPLY_GENERAL_FAILURE;
}

static int request_marshall_bytes(buffer *b, const uint8_t *response, const size_t length) {
    for(size_t i = 0; i < length; i++) {
        if(!buffer_can_write(b)) {
            return -1;
        }
        buffer_write(b, response[i]);
    }
    return 0;
}

int request_marshall_ipv4(buffer *b, const uint8_t reply,
                          const uint8_t addr[4], const uint16_t port) {
    const uint8_t response[] = {
        SOCKS_REQUEST_VERSION,
        reply,
        0x00,
        SOCKS_REQUEST_ATYP_IPV4,
        addr[0], addr[1], addr[2], addr[3],
        (uint8_t) ((port >> 8) & 0xFF),
        (uint8_t) (port & 0xFF),
    };

    if(reply > SOCKS_REPLY_ADDRESS_TYPE_NOT_SUPPORTED) {
        return -1;
    }

    return request_marshall_bytes(b, response, sizeof(response));
}

int request_marshall_ipv6(buffer *b, const uint8_t reply,
                          const uint8_t addr[16], const uint16_t port) {
    uint8_t response[4 + 16 + 2];

    if(reply > SOCKS_REPLY_ADDRESS_TYPE_NOT_SUPPORTED) {
        return -1;
    }

    response[0] = SOCKS_REQUEST_VERSION;
    response[1] = reply;
    response[2] = 0x00;
    response[3] = SOCKS_REQUEST_ATYP_IPV6;
    memcpy(response + 4, addr, 16);
    response[20] = (uint8_t) ((port >> 8) & 0xFF);
    response[21] = (uint8_t) (port & 0xFF);

    return request_marshall_bytes(b, response, sizeof(response));
}

int request_marshall(buffer *b, const uint8_t reply) {
    static const uint8_t zero_addr[] = { 0x00, 0x00, 0x00, 0x00 };
    return request_marshall_ipv4(b, reply, zero_addr, 0);
}
