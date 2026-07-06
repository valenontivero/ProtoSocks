/**
 * socks5nio.c  - controla el flujo de un proxy SOCKSv5 (sockets no bloqueantes)
 */
#include<stdio.h>
#include <stdlib.h>  // malloc
#include <string.h>  // memset
#include <assert.h>  // assert
#include <errno.h>
#include <time.h>
#include <unistd.h>  // close
#include <pthread.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <arpa/inet.h>

#include "hello.h"
#include "request.h"
#include "buffer.h"

#include "selector.h"
#include "stm.h"
#include "socks5nio.h"
#include "netutils.h"

#define N(x) (sizeof(x)/sizeof((x)[0]))


/** maquina de estados general */
enum socks_v5state {
    /**
     * recibe el mensaje `hello` del cliente, y lo procesa
     *
     * Intereses:
     *     - OP_READ sobre client_fd
     *
     * Transiciones:
     *   - HELLO_READ  mientras el mensaje no esté completo
     *   - HELLO_WRITE cuando está completo
     *   - ERROR       ante cualquier error (IO/parseo)
     */
    HELLO_READ,

    /**
     * envía la respuesta del `hello' al cliente.
     *
     * Intereses:
     *     - OP_WRITE sobre client_fd
     *
     * Transiciones:
     *   - HELLO_WRITE  mientras queden bytes por enviar
     *   - REQUEST_READ cuando se enviaron todos los bytes
     *   - ERROR        ante cualquier error (IO/parseo)
     */
    HELLO_WRITE,

    REQUEST_READ,

    CONNECTING,

    REQUEST_WRITE,

    COPY,

    // estados terminales
    DONE,
    ERROR,
};

static struct socks5 *pool = NULL;
static size_t pool_size = 0;
static const size_t max_pool = 50;

typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_ERROR
} log_level;

#define LOG(level, ...) do { \
    if (level == LOG_ERROR) { \
        fprintf(stderr, "[ERROR] " __VA_ARGS__); \
        fprintf(stderr, "\n"); \
    } else { \
        printf("[DEBUG] " __VA_ARGS__); \
        printf("\n"); \
    } \
} while(0)

////////////////////////////////////////////////////////////////////
// Definición de variables para cada estado
////////////////////////////////////////////////////////////////////


/** usado por HELLO_READ, HELLO_WRITE */
struct hello_st {
    /** buffer utilizado para I/O */
    buffer               *rb, *wb;
    struct hello_parser   parser;
    /** el método de autenticación seleccionado */
    uint8_t               method;
} ;

/** usado por REQUEST_READ, REQUEST_WRITE */
struct request_st {
    buffer                  *rb, *wb;
    struct request_parser    parser;
    uint8_t                  reply;
};

/*
 * Representa una conexion socks5 completa.
 * Si bien cada estado tiene su propio struct que le da un alcance
 * acotado, disponemos de la siguiente estructura para hacer una única
 * alocación cuando recibimos la conexión.
 *
 * Se utiliza un contador de referencias (references) para saber cuando debemos
 * liberarlo finalmente, y un pool para reusar alocaciones previas.
 */
struct socks5 {
    /** Informacion del cliente */
    struct sockaddr_storage client_addr;
    socklen_t               client_addr_len;
    int                     client_fd;

    /** Resolucion de direccion de origen */
    struct addrinfo *origin_resolution;
    struct addrinfo *origin_resolution_current;

    /** File descriptor del origen */
    int origin_fd;

    /** Maquina de estados */
    struct state_machine stm;

    /** buffers de lectura y escritura */
    uint8_t raw_buff_a[2048];
    uint8_t raw_buff_b[2048];
    buffer  read_buffer;
    buffer  write_buffer;

    /** estados para el client_fd */
    union {
        struct hello_st hello;
        struct request_st request;
        // struct copy       copy;
    } client;

    /** estados para el origin_fd */
    // union {
    //     struct connecting         conn;
    //     struct copy               copy;
    // } orig;

    /** Para el pool de objetos */
    struct socks5 *next;
    unsigned       references;
};


/** realmente destruye */
static void
socks5_destroy_(struct socks5* s) {
    if(s->origin_resolution != NULL) {
        freeaddrinfo(s->origin_resolution);
        s->origin_resolution = 0;
    }
    free(s);
}

/**
 * destruye un  `struct socks5', tiene en cuenta las referencias
 * y el pool de objetos.
 */
static void
socks5_destroy(struct socks5 *s) {
    if(s == NULL) {
        // nada para hacer
    } else if(s->references == 1) {
        if(s != NULL) {
            if(pool_size < max_pool) {
                s->next = pool;
                pool    = s;
                pool_size++;
            } else {
                socks5_destroy_(s);
            }
        }
    } else {
        s->references -= 1;
    }
}

void
socksv5_pool_destroy(void) {
    struct socks5 *next, *s;
    for(s = pool; s != NULL ; s = next) {
        next = s->next;
        free(s);
    }
}

/** obtiene el struct (socks5 *) desde la llave de selección  */
#define ATTACHMENT(key) ( (struct socks5 *)(key)->data)

static const struct state_definition client_statbl[8];

/* declaración forward de los handlers de selección de una conexión
 * establecida entre un cliente y el proxy.
 */
static void socksv5_read   (struct selector_key *key);
static void socksv5_write  (struct selector_key *key);
static void socksv5_block  (struct selector_key *key);
static void socksv5_close  (struct selector_key *key);
static const struct fd_handler socks5_handler = {
    .handle_read   = socksv5_read,
    .handle_write  = socksv5_write,
    .handle_close  = socksv5_close,
    .handle_block  = socksv5_block,
};


/** Crea un nuevo objecto socks5 */

static struct socks5 *socks5_new(const int client_fd)
{
    LOG(LOG_DEBUG, "Creating new SOCKS5 state");
    struct socks5 *ret;

    if (pool == NULL) {
        LOG(LOG_DEBUG, "No available SOCKS5 state in pool, allocating new one");
        ret = malloc(sizeof(*ret));
    } else {
        LOG(LOG_DEBUG, "Reusing SOCKS5 state from pool");
        ret       = pool;
        pool      = pool->next;
        ret->next = 0;
        pool_size--;
    }

    if (ret == NULL) {
        LOG(LOG_ERROR, "Error allocating memory for new SOCKS5 state");
        return NULL;
    }

    memset(ret, 0x00, sizeof(*ret));

    ret->origin_fd       = -1;
    ret->client_fd       = client_fd;
    ret->client_addr_len = sizeof(ret->client_addr);

    ret->references = 1;

    // Initialize buffers
    buffer_init(&ret->read_buffer, N(ret->raw_buff_a), ret->raw_buff_a);
    buffer_init(&ret->write_buffer, N(ret->raw_buff_b), ret->raw_buff_b);

    ret->stm.initial   = HELLO_READ;
    ret->stm.max_state = ERROR;
    ret->stm.states    = client_statbl;
    stm_init(&ret->stm);

    LOG(LOG_DEBUG, "SOCKS5 state created successfully (fd=%d)", client_fd);
    return ret;
}

/** Intenta aceptar la nueva conexión entrante*/
void
socksv5_passive_accept(struct selector_key *key) {
    struct sockaddr_storage       client_addr;
    socklen_t                     client_addr_len = sizeof(client_addr);
    struct socks5                *state           = NULL;

    const int client = accept(key->fd, (struct sockaddr*) &client_addr,
                                                          &client_addr_len);
    if(client == -1) {
        goto fail;
    }
    // Seteamos el socket en no bloqueante
    if(selector_fd_set_nio(client) == -1) {
        goto fail;
    }
    state = socks5_new(client);
    if(state == NULL) {
        // sin un estado, nos es imposible manejaro.
        // tal vez deberiamos apagar accept() hasta que detectemos
        // que se liberó alguna conexión.
        goto fail;
    }
    memcpy(&state->client_addr, &client_addr, client_addr_len);
    state->client_addr_len = client_addr_len;

    if(SELECTOR_SUCCESS != selector_register(key->s, client, &socks5_handler,
                                              OP_READ, state)) {
        goto fail;
    }
    return ;
fail:
    if(client != -1) {
        close(client);
    }
    socks5_destroy(state);
}

////////////////////////////////////////////////////////////////////////////////
// HELLO
////////////////////////////////////////////////////////////////////////////////

/** callback del parser utilizado en `read_hello' */
static void
on_hello_method(struct hello_parser *p, const uint8_t method) {
    uint8_t *selected  = p->data;

    if(SOCKS_HELLO_NOAUTHENTICATION_REQUIRED == method) {
       *selected = method;
    }
}

/** inicializa las variables de los estados HELLO_.../ */
static void
hello_read_init(const unsigned state, struct selector_key *key) {
    struct hello_st *d = &ATTACHMENT(key)->client.hello;

    d->rb                              = &(ATTACHMENT(key)->read_buffer);
    d->wb                              = &(ATTACHMENT(key)->write_buffer);
    d->method                          = SOCKS_HELLO_NO_ACCEPTABLE_METHODS;
    d->parser.data                     = &d->method;
    d->parser.on_authentication_method = on_hello_method, hello_parser_init(&d->parser);
}

static unsigned
hello_process(const struct hello_st* d);

/** lee todos los bytes del mensaje de tipo `hello' y inicia su proceso */
static unsigned
hello_read(struct selector_key *key) {
    struct hello_st *d = &ATTACHMENT(key)->client.hello;
    unsigned  ret      = HELLO_READ;
        bool  error    = false;
     uint8_t *ptr;
      size_t  count;
     ssize_t  n;

    ptr = buffer_write_ptr(d->rb, &count);

    // Se utiliza para recibir datos desde un socket conectado o no conectado
    n = recv(key->fd, ptr, count, 0);

    if(n > 0) {
        buffer_write_adv(d->rb, n);
        const enum hello_state st = hello_consume(d->rb, &d->parser, &error);
        if(hello_is_done(st, 0)) {
            if(SELECTOR_SUCCESS == selector_set_interest_key(key, OP_WRITE)) {
                ret = hello_process(d);
            } else {
                ret = ERROR;
            }
        }
    } else {
        ret = ERROR;
    }

    return error ? ERROR : ret;
}

/** procesamiento del mensaje `hello' */
static unsigned
hello_process(const struct hello_st* d) {
    unsigned ret = HELLO_WRITE;

    uint8_t m = d->method;
    const uint8_t r = (m == SOCKS_HELLO_NO_ACCEPTABLE_METHODS) ? 0xFF : 0x00;
    if (-1 == hello_marshall(d->wb, r)) {
        ret  = ERROR;
    }
    return ret;
}

static void
hello_read_close(const unsigned state, struct selector_key *key) {
    struct hello_st *d = &ATTACHMENT(key)->client.hello;
    hello_parser_close(&d->parser);
}

static void
hello_write_close(const unsigned state, struct selector_key *key) {
    // Nada que liberar
}

static unsigned
hello_write(struct selector_key *key) {
    struct hello_st *d = &ATTACHMENT(key)->client.hello;
    unsigned  ret      = HELLO_WRITE;
    uint8_t  *ptr;
    size_t    count;
    ssize_t   n;

    ptr = buffer_read_ptr(d->wb, &count);
    n = send(key->fd, ptr, count, MSG_NOSIGNAL);
    if(n > 0) {
        buffer_read_adv(d->wb, n);
        if(!buffer_can_read(d->wb)) {
            if(d->method == SOCKS_HELLO_NO_ACCEPTABLE_METHODS) {
                ret = DONE;
            } else if(SELECTOR_SUCCESS == selector_set_interest_key(key, OP_READ)) {
                ret = REQUEST_READ;
            } else {
                ret = ERROR;
            }
        }
    } else {
        ret = ERROR;
    }

    return ret;
}

////////////////////////////////////////////////////////////////////////////////
// REQUEST
////////////////////////////////////////////////////////////////////////////////

static void
request_read_init(const unsigned state, struct selector_key *key) {
    struct request_st *d = &ATTACHMENT(key)->client.request;

    (void) state;
    d->rb    = &(ATTACHMENT(key)->read_buffer);
    d->wb    = &(ATTACHMENT(key)->write_buffer);
    d->reply = SOCKS_REPLY_GENERAL_FAILURE;
    request_parser_init(&d->parser);
}

static unsigned
request_set_reply(struct selector_key *key, struct request_st *d, const uint8_t reply) {
    d->reply = reply;
    if(request_marshall(d->wb, d->reply) < 0) {
        return ERROR;
    }
    if(SELECTOR_SUCCESS != selector_set_interest(key->s, ATTACHMENT(key)->client_fd, OP_WRITE)) {
        return ERROR;
    }
    return REQUEST_WRITE;
}

static int
request_connect_ipv4(struct selector_key *key, const struct request_parser *p, bool *in_progress) {
    struct socks5 *s = ATTACHMENT(key);
    struct sockaddr_in addr;
    int fd;
    int ret;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(p->port);
    memcpy(&addr.sin_addr.s_addr, p->addr, 4);

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) {
        return -1;
    }
    if(selector_fd_set_nio(fd) < 0) {
        close(fd);
        return -1;
    }

    ret = connect(fd, (struct sockaddr *) &addr, sizeof(addr));
    if(ret < 0 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }

    *in_progress = ret < 0;
    s->origin_fd = fd;
    s->references++;
    if(SELECTOR_SUCCESS != selector_register(key->s, fd, &socks5_handler,
                                             *in_progress ? OP_WRITE : OP_NOOP, s)) {
        s->references--;
        s->origin_fd = -1;
        close(fd);
        return -1;
    }

    return 0;
}

static int
request_marshall_origin_bind(struct socks5 *s, struct request_st *d) {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    uint8_t bind_addr[4] = { 0x00, 0x00, 0x00, 0x00 };
    uint16_t bind_port = 0;

    memset(&addr, 0, sizeof(addr));
    if(getsockname(s->origin_fd, (struct sockaddr *) &addr, &addr_len) == 0
            && addr.sin_family == AF_INET) {
        memcpy(bind_addr, &addr.sin_addr.s_addr, sizeof(bind_addr));
        bind_port = ntohs(addr.sin_port);
    }

    return request_marshall_ipv4(d->wb, d->reply, bind_addr, bind_port);
}

static unsigned
request_process(struct selector_key *key, struct request_st *d) {
    struct socks5 *s = ATTACHMENT(key);
    bool in_progress = false;

    if(d->parser.cmd != SOCKS_REQUEST_CMD_CONNECT) {
        return request_set_reply(key, d, SOCKS_REPLY_COMMAND_NOT_SUPPORTED);
    }
    if(d->parser.atyp != SOCKS_REQUEST_ATYP_IPV4) {
        return request_set_reply(key, d, SOCKS_REPLY_ADDRESS_TYPE_NOT_SUPPORTED);
    }
    if(request_connect_ipv4(key, &d->parser, &in_progress) < 0) {
        return request_set_reply(key, d, SOCKS_REPLY_GENERAL_FAILURE);
    }

    if(in_progress) {
        if(SELECTOR_SUCCESS != selector_set_interest(key->s, s->client_fd, OP_NOOP)) {
            return ERROR;
        }
        return CONNECTING;
    }

    d->reply = SOCKS_REPLY_SUCCEEDED;
    if(request_marshall_origin_bind(s, d) < 0) {
        return ERROR;
    }
    if(SELECTOR_SUCCESS != selector_set_interest(key->s, s->client_fd, OP_WRITE)) {
        return ERROR;
    }
    return REQUEST_WRITE;
}

static unsigned
request_read(struct selector_key *key) {
    struct request_st *d = &ATTACHMENT(key)->client.request;
    unsigned  ret        = REQUEST_READ;
        bool  error      = false;
     uint8_t *ptr;
      size_t  count;
     ssize_t  n;

    ptr = buffer_write_ptr(d->rb, &count);
    n = recv(key->fd, ptr, count, 0);

    if(n > 0) {
        buffer_write_adv(d->rb, n);
        const enum request_state st = request_consume(d->rb, &d->parser, &error);
        if(request_is_done(st, 0)) {
            ret = request_process(key, d);
        }
    } else if(n == 0) {
        ret = ERROR;
    } else if(errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        ret = ERROR;
    }

    return error ? ERROR : ret;
}

static void
request_read_close(const unsigned state, struct selector_key *key) {
    (void) state;
    (void) key;
}

static void
request_write_close(const unsigned state, struct selector_key *key) {
    (void) state;
    (void) key;
}

static unsigned
connecting(struct selector_key *key) {
    struct socks5 *s = ATTACHMENT(key);
    struct request_st *d = &s->client.request;
    int error = 0;
    socklen_t len = sizeof(error);

    if(key->fd != s->origin_fd) {
        return CONNECTING;
    }
    if(getsockopt(s->origin_fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
        selector_set_interest(key->s, s->origin_fd, OP_NOOP);
        return request_set_reply(key, d, SOCKS_REPLY_GENERAL_FAILURE);
    }

    d->reply = SOCKS_REPLY_SUCCEEDED;
    if(request_marshall_origin_bind(s, d) < 0) {
        return ERROR;
    }
    if(SELECTOR_SUCCESS != selector_set_interest(key->s, s->origin_fd, OP_NOOP)) {
        return ERROR;
    }
    if(SELECTOR_SUCCESS != selector_set_interest(key->s, s->client_fd, OP_WRITE)) {
        return ERROR;
    }
    return REQUEST_WRITE;
}

static unsigned
request_write(struct selector_key *key) {
    struct socks5 *s = ATTACHMENT(key);
    struct request_st *d = &s->client.request;
    unsigned  ret        = REQUEST_WRITE;
    uint8_t  *ptr;
    size_t    count;
    ssize_t   n;

    ptr = buffer_read_ptr(d->wb, &count);
    n = send(key->fd, ptr, count, MSG_NOSIGNAL);
    if(n > 0) {
        buffer_read_adv(d->wb, n);
        if(!buffer_can_read(d->wb)) {
            ret = (d->reply == SOCKS_REPLY_SUCCEEDED && s->origin_fd != -1) ? COPY : DONE;
        }
    } else if(n == 0) {
        ret = ERROR;
    } else if(errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        ret = ERROR;
    }

    return ret;
}


////////////////////////////////////////////////////////////////////////////////
// COPY
////////////////////////////////////////////////////////////////////////////////

static bool
copy_buffer_can_write(buffer *b) {
    if(!buffer_can_write(b)) {
        buffer_compact(b);
    }
    return buffer_can_write(b);
}

static bool
copy_update_interests(struct selector_key *key) {
    struct socks5 *s = ATTACHMENT(key);
    fd_interest client_interest = OP_NOOP;
    fd_interest origin_interest = OP_NOOP;

    if(s->origin_fd == -1) {
        return false;
    }

    if(copy_buffer_can_write(&s->read_buffer)) {
        client_interest |= OP_READ;
    }
    if(buffer_can_read(&s->write_buffer)) {
        client_interest |= OP_WRITE;
    }
    if(copy_buffer_can_write(&s->write_buffer)) {
        origin_interest |= OP_READ;
    }
    if(buffer_can_read(&s->read_buffer)) {
        origin_interest |= OP_WRITE;
    }

    return SELECTOR_SUCCESS == selector_set_interest(key->s, s->client_fd, client_interest)
        && SELECTOR_SUCCESS == selector_set_interest(key->s, s->origin_fd, origin_interest);
}

static void
copy_init(const unsigned state, struct selector_key *key) {
    (void) state;
    buffer_reset(&ATTACHMENT(key)->write_buffer);
    copy_update_interests(key);
}

static unsigned
copy_read(struct selector_key *key) {
    struct socks5 *s = ATTACHMENT(key);
    buffer *b;
    uint8_t *ptr;
    size_t count;
    ssize_t n;

    if(key->fd == s->client_fd) {
        b = &s->read_buffer;
    } else if(key->fd == s->origin_fd) {
        b = &s->write_buffer;
    } else {
        return ERROR;
    }

    if(!copy_buffer_can_write(b)) {
        return copy_update_interests(key) ? COPY : ERROR;
    }

    ptr = buffer_write_ptr(b, &count);
    n = recv(key->fd, ptr, count, 0);
    if(n > 0) {
        buffer_write_adv(b, n);
        return copy_update_interests(key) ? COPY : ERROR;
    }
    if(n == 0) {
        return DONE;
    }
    if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return copy_update_interests(key) ? COPY : ERROR;
    }
    return ERROR;
}

static unsigned
copy_write(struct selector_key *key) {
    struct socks5 *s = ATTACHMENT(key);
    buffer *b;
    uint8_t *ptr;
    size_t count;
    ssize_t n;

    if(key->fd == s->client_fd) {
        b = &s->write_buffer;
    } else if(key->fd == s->origin_fd) {
        b = &s->read_buffer;
    } else {
        return ERROR;
    }

    ptr = buffer_read_ptr(b, &count);
    n = send(key->fd, ptr, count, MSG_NOSIGNAL);
    if(n > 0) {
        buffer_read_adv(b, n);
        return copy_update_interests(key) ? COPY : ERROR;
    }
    if(n == 0) {
        return ERROR;
    }
    if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return copy_update_interests(key) ? COPY : ERROR;
    }
    return ERROR;
}

static const struct state_definition client_statbl[] = {
    {
        .state            = HELLO_READ,
        .on_arrival       = hello_read_init,
        .on_departure     = hello_read_close,
        .on_read_ready    = hello_read,
    },
    {
        .state            = HELLO_WRITE,
        .on_arrival       = NULL,
        .on_departure     = hello_write_close,
        .on_write_ready   = hello_write,
    },
    {
        .state            = REQUEST_READ,
        .on_arrival       = request_read_init,
        .on_departure     = request_read_close,
        .on_read_ready    = request_read,
    },
    {
        .state            = CONNECTING,
        .on_arrival       = NULL,
        .on_departure     = NULL,
        .on_write_ready   = connecting,
    },
    {
        .state            = REQUEST_WRITE,
        .on_arrival       = NULL,
        .on_departure     = request_write_close,
        .on_write_ready   = request_write,
    },
    {
        .state            = COPY,
        .on_arrival       = copy_init,
        .on_departure     = NULL,
        .on_read_ready    = copy_read,
        .on_write_ready   = copy_write,
    },
    {
        .state            = DONE,
        .on_arrival       = NULL,
        .on_departure     = NULL,
        .on_read_ready    = NULL,
        .on_write_ready   = NULL,
    },
    {
        .state            = ERROR,
        .on_arrival       = NULL,
        .on_departure     = NULL,
        .on_read_ready    = NULL,
        .on_write_ready   = NULL,
    }
};

///////////////////////////////////////////////////////////////////////////////
// Handlers top level de la conexión pasiva.
// son los que emiten los eventos a la maquina de estados.

static void
socksv5_done(struct selector_key* key);

static void
socksv5_read(struct selector_key *key) {
    struct state_machine *stm   = &ATTACHMENT(key)->stm;
    const enum socks_v5state st = stm_handler_read(stm, key);

    if(ERROR == st || DONE == st) {
        socksv5_done(key);
    }
}

static void
socksv5_write(struct selector_key *key) {
    struct state_machine *stm   = &ATTACHMENT(key)->stm;
    const enum socks_v5state st = stm_handler_write(stm, key);

    if(ERROR == st || DONE == st) {
        socksv5_done(key);
    }
}

static void
socksv5_block(struct selector_key *key) {
    struct state_machine *stm   = &ATTACHMENT(key)->stm;
    const enum socks_v5state st = stm_handler_block(stm, key);

    if(ERROR == st || DONE == st) {
        socksv5_done(key);
    }
}

static void
socksv5_close(struct selector_key *key) {
    socks5_destroy(ATTACHMENT(key));
}

static void
socksv5_done(struct selector_key* key) {
    const int fds[] = {
        ATTACHMENT(key)->client_fd,
        ATTACHMENT(key)->origin_fd,
    };
    for(unsigned i = 0; i < N(fds); i++) {
        if(fds[i] != -1) {
            if(SELECTOR_SUCCESS != selector_unregister_fd(key->s, fds[i])) {
                abort();
            }
            close(fds[i]);
        }
    }
}
