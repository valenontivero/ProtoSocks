#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>

#include "monitor.h"
#include "buffer.h"
#include "socks5nio.h"
#include "metrics.h"
#include "user_store.h"

#define LINE_BUFFER_SIZE 1024
#define MONITOR_BUFF_SIZE 2048

// hardcodeado para primera version
const char *monitor_token = "admin123";

// Estructura de cada cliente admin
struct monitor_client {
    int fd;
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len;

    buffer read_buffer;
    buffer write_buffer;
    uint8_t raw_buff_read[MONITOR_BUFF_SIZE];
    uint8_t raw_buff_write[MONITOR_BUFF_SIZE];

    bool authenticated;
    bool close_after_write;

    char line_buffer[LINE_BUFFER_SIZE];
    size_t line_len;
};

static void monitor_read(struct selector_key *key);
static void monitor_write(struct selector_key *key);
static void monitor_close(struct selector_key *key);

static const struct fd_handler monitor_handler = {
    .handle_read = monitor_read,
    .handle_write = monitor_write,
    .handle_close = monitor_close,
};

static void monitor_write_str(struct monitor_client *mc, const char *str) {
    size_t len = strlen(str);
    for (size_t i = 0; i < len; i++) {
        if (buffer_can_write(&mc->write_buffer)) {
            buffer_write(&mc->write_buffer, str[i]);
        }
    }
}

static void monitor_update_interests(struct selector_key *key, struct monitor_client *mc) {
    fd_interest interest = OP_READ;
    if (buffer_can_read(&mc->write_buffer)) {
        interest |= OP_WRITE;
    }
    selector_set_interest(key->s, key->fd, interest);
}

static void monitor_process_command(struct monitor_client *mc) {
    // Ignorar \r al final si existe
    if (mc->line_len > 0 && mc->line_buffer[mc->line_len - 1] == '\r') {
        mc->line_buffer[mc->line_len - 1] = '\0';
    }

    if (strncmp(mc->line_buffer, "AUTH ", 5) == 0) {
        const char *token = mc->line_buffer + 5;
        if (strcmp(token, monitor_token) == 0) {
            mc->authenticated = true;
            monitor_write_str(mc, "+OK\n");
        } else {
            monitor_write_str(mc, "-ERR Invalid token\n");
            mc->close_after_write = true;
        }
    } else if (strcmp(mc->line_buffer, "METRICS") == 0) {
        if (!mc->authenticated) {
            monitor_write_str(mc, "-ERR Not authenticated\n");
        } else {
            char resp[512];
            snprintf(resp, sizeof(resp),
                     "historic_connections:%lu\n"
                     "concurrent_connections:%u\n"
                     "bytes_transferred:%lu\n"
                     "+OK\n",
                     (unsigned long)get_metrics_historic_connections(),
                     (unsigned)get_metrics_concurrent_connections(),
                     (unsigned long)get_metrics_bytes_transferred());
            monitor_write_str(mc, resp);
        }
    } else if (strncmp(mc->line_buffer, "ADD-USER ", 9) == 0) {
        if (!mc->authenticated) {
            monitor_write_str(mc, "-ERR Not authenticated\n");
            return;
        } 
        char *args = mc->line_buffer + 9;
        char *username = strtok(args, " ");
        char *password = strtok(NULL, " ");
        char *extra = strtok(NULL, " "); //para verificar que no haya mas argumentos

        if (username == NULL || password == NULL || extra != NULL) {
            monitor_write_str(mc, "-ERR Invalid arguments\n");
            return;
        }

        if (user_store_add((uint8_t *) username, strlen(username), (uint8_t *) password, strlen(password))) {
            monitor_write_str(mc, "+OK\n");
        } else {
            monitor_write_str(mc, "-ERR Could not add user\n");
        }
        
    } else if (strncmp(mc->line_buffer, "DEL-USER ", 9) == 0) {
        if (!mc->authenticated) {
            monitor_write_str(mc, "-ERR Not authenticated\n");
            return;
        }
        char *args = mc->line_buffer + 9;
        char *username = strtok(args, " ");
        char *extra = strtok(NULL, " ");

        if (username == NULL || extra != NULL) {
            monitor_write_str(mc, "-ERR Invalid arguments\n");
            return;
        }
        if (user_store_remove((uint8_t *) username, strlen(username))) {
            monitor_write_str(mc, "+OK\n");
        } else {
            monitor_write_str(mc, "-ERR Could not remove user\n");
        }
    } else if (strcmp(mc->line_buffer, "LIST-USERS") == 0) {
        if (!mc->authenticated) {
            monitor_write_str(mc, "-ERR Not authenticated\n");
            return;
        }
        //falta
    } else {
        if (!mc->authenticated) {
            monitor_write_str(mc, "-ERR Not authenticated\n");
        } else {
            monitor_write_str(mc, "-ERR Unknown command\n");
        }
    }
}

// acepta la conexion del cliente admin
void monitor_passive_accept(struct selector_key *key) {
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    struct monitor_client *mc = NULL;

    const int client = accept(key->fd, (struct sockaddr *)&client_addr, &client_addr_len);
    if (client == -1) {
        goto fail;
    }

    if (selector_fd_set_nio(client) == -1) {
        goto fail;
    }

    mc = malloc(sizeof(*mc));
    if (mc == NULL) {
        goto fail;
    }

    memset(mc, 0, sizeof(*mc));
    mc->fd = client;
    memcpy(&mc->client_addr, &client_addr, client_addr_len);
    mc->client_addr_len = client_addr_len;

    buffer_init(&mc->read_buffer, MONITOR_BUFF_SIZE, mc->raw_buff_read);
    buffer_init(&mc->write_buffer, MONITOR_BUFF_SIZE, mc->raw_buff_write);

    if (SELECTOR_SUCCESS != selector_register(key->s, client, &monitor_handler, OP_READ, mc)) {
        goto fail;
    }
    return;

fail:
    if (client != -1) {
        close(client);
    }
    if (mc != NULL) {
        free(mc);
    }
}

//se parsea la entrada enviada por el cliente admin
static void monitor_read(struct selector_key *key) {
    struct monitor_client *mc = key->data;
    size_t count;
    uint8_t *ptr = buffer_write_ptr(&mc->read_buffer, &count);
    
    if (count == 0) {
        // Buffer de lectura lleno, forzar error o cerrar
        selector_unregister_fd(key->s, key->fd);
        return;
    }

    ssize_t n = recv(key->fd, ptr, count, 0);
    if (n > 0) {
        buffer_write_adv(&mc->read_buffer, n);

        while (buffer_can_read(&mc->read_buffer)) {
            uint8_t c = buffer_read(&mc->read_buffer);
            if (c == '\n') {
                mc->line_buffer[mc->line_len] = '\0';
                monitor_process_command(mc);
                mc->line_len = 0;
            } else if (mc->line_len < LINE_BUFFER_SIZE - 1) {
                mc->line_buffer[mc->line_len++] = c;
            } else {
                // Comando demasiado largo, error de protocolo
                monitor_write_str(mc, "-ERR Command too long\n");
                mc->close_after_write = true;
                mc->line_len = 0;
                break;
            }
        }
        monitor_update_interests(key, mc);
    } else if (n == 0) {
        // Cliente cerro la conexión
        selector_unregister_fd(key->s, key->fd);
    } else {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            selector_unregister_fd(key->s, key->fd);
        }
    }
}

static void monitor_write(struct selector_key *key) {
    struct monitor_client *mc = key->data;
    size_t count;
    uint8_t *ptr = buffer_read_ptr(&mc->write_buffer, &count);

    if (count > 0) {
        ssize_t n = send(key->fd, ptr, count, MSG_NOSIGNAL);
        if (n > 0) {
            buffer_read_adv(&mc->write_buffer, n);
            if (!buffer_can_read(&mc->write_buffer) && mc->close_after_write) {
                selector_unregister_fd(key->s, key->fd);
                return;
            }
            monitor_update_interests(key, mc);
        } else {
            selector_unregister_fd(key->s, key->fd);
        }
    }
}

static void monitor_close(struct selector_key *key) {
    struct monitor_client *mc = key->data;
    if (mc != NULL) {
        close(mc->fd);
        free(mc);
    }
}
