#ifndef CLIENT_ARGS_H
#define CLIENT_ARGS_H

struct client_args {
    char *mng_addr;
    unsigned short mng_port;
    char *token;
};

void parse_client_args(int argc, char **argv, struct client_args *args);

#endif