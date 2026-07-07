#include "auth.h"

#include <string.h>

void auth_parser_init(struct auth_parser *p) {
    memset(p, 0, sizeof(*p));
    p->state = AUTH_VERSION;
}

enum auth_state auth_consume(buffer *b, struct auth_parser *p, bool *errored) {
    while(buffer_can_read(b) && p->state != AUTH_DONE && p->state != AUTH_ERROR) {
        const uint8_t c = buffer_read(b);

        switch(p->state) {
            case AUTH_VERSION:
                p->state = (c == SOCKS_AUTH_VERSION) ? AUTH_ULEN : AUTH_ERROR;
                break;

            case AUTH_ULEN:
                p->username_len = c;
                p->pos = 0;
                p->state = (c == 0) ? AUTH_ERROR : AUTH_UNAME;
                break;

            case AUTH_UNAME:
                p->username[p->pos++] = c;
                if(p->pos == p->username_len) {
                    p->username[p->username_len] = 0;
                    p->pos = 0;
                    p->state = AUTH_PLEN;
                }
                break;

            case AUTH_PLEN:
                p->password_len = c;
                p->pos = 0;
                p->state = (c == 0) ? AUTH_ERROR : AUTH_PASSWD;
                break;

            case AUTH_PASSWD:
                p->password[p->pos++] = c;
                if(p->pos == p->password_len) {
                    p->password[p->password_len] = 0;
                    p->state = AUTH_DONE;
                }
                break;

            case AUTH_DONE:
            case AUTH_ERROR:
                break;
        }
    }

    if(p->state == AUTH_ERROR && errored != NULL) {
        *errored = true;
    }
    return p->state;
}

bool auth_is_done(const enum auth_state state, bool *errored) {
    if(state == AUTH_ERROR) {
        if(errored != NULL) {
            *errored = true;
        }
        return false;
    }
    return state == AUTH_DONE;
}

int auth_marshall(buffer *b, const uint8_t status) {
    if(status != SOCKS_AUTH_STATUS_SUCCESS && status != SOCKS_AUTH_STATUS_FAILURE) {
        return -1;
    }
    if(!buffer_can_write(b)) {
        return -1;
    }
    buffer_write(b, SOCKS_AUTH_VERSION);
    if(!buffer_can_write(b)) {
        return -1;
    }
    buffer_write(b, status);
    return 0;
}

