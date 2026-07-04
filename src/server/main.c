#define _POSIX_C_SOURCE 200112L

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BACKLOG 10
#define BUFFER_SIZE 4096

static void usage(const char *program) {
    fprintf(stderr, "usage: %s <port>\n", program);
}

static int create_server_socket(const char *port) {
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *current = NULL;
    int server_fd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int status = getaddrinfo(NULL, port, &hints, &addresses);
    if (status != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        return -1;
    }

    for (current = addresses; current != NULL; current = current->ai_next) {
        server_fd = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (server_fd < 0) {
            continue;
        }

        int enabled = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));

        if (bind(server_fd, current->ai_addr, current->ai_addrlen) == 0
                && listen(server_fd, BACKLOG) == 0) {
            break;
        }

        close(server_fd);
        server_fd = -1;
    }

    freeaddrinfo(addresses);
    return server_fd;
}

static int send_all(int fd, const char *buffer, size_t length) {
    size_t sent = 0;

    while (sent < length) {
        ssize_t bytes = send(fd, buffer + sent, length - sent, 0);
        if (bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        if (bytes == 0) {
            return -1;
        }

        sent += (size_t) bytes;
    }

    return 0;
}

static void handle_client(int client_fd) {
    char buffer[BUFFER_SIZE];

    while (1) {
        ssize_t bytes = recv(client_fd, buffer, sizeof(buffer), 0);
        if (bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("recv");
            break;
        }

        if (bytes == 0) {
            break;
        }

        if (send_all(client_fd, buffer, (size_t) bytes) < 0) {
            perror("send");
            break;
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    int server_fd = create_server_socket(argv[1]);
    if (server_fd < 0) {
        perror("server socket");
        return EXIT_FAILURE;
    }

    printf("listening on port %s\n", argv[1]);

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            break;
        }

        handle_client(client_fd);
        close(client_fd);
    }

    close(server_fd);
    return EXIT_FAILURE;
}
