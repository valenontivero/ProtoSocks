#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "shared.h"

// Lee la respuesta de la conexión 
static ssize_t read_response(int sock, char *buf, size_t max_len) {
    size_t total = 0;
    while (total < max_len - 1) {
        char c;
        ssize_t n = recv(sock, &c, 1, 0);
        if (n <= 0) {
            break; // Conexión cerrada o error
        }
        buf[total++] = c;
        
        // Verifica si terminamos con "+OK\n"
        if (total >= 4 && strcmp(buf + total - 4, "+OK\n") == 0) {
            break;
        }
        // Verifica si es un error de una sola línea terminada en '\n'
        if (total >= 5 && strncmp(buf, "-ERR", 4) == 0 && c == '\n') {
            break;
        }
    }
    buf[total] = '\0';
    return total;
}

int main(void) {
    printf("Admin Client Interactive Console version %s\n", protosocks_version());

    // Creamos el socket del admin_cliente 
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Unable to create socket");
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080); // Puerto de monitoreo
    if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(sock);
        return 1;
    }

    // El selector detecta este connect y acepta la conexion del socket
    printf("Connecting to monitor server at 127.0.0.1:8080...\n");
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Connection failed");
        close(sock);
        return 1;
    }


    // Enviamos autenticacion
    const char *auth_cmd = "AUTH admin123\n";
    if (send(sock, auth_cmd, strlen(auth_cmd), 0) < 0) {
        perror("Failed to send AUTH command");
        close(sock);
        return 1;
    }

    char resp[2048] = {0};
    if (read_response(sock, resp, sizeof(resp)) <= 0) {
        printf("No response from server during authentication.\n");
        close(sock);
        return 1;
    }

    if (strncmp(resp, "-ERR", 4) == 0) {
        printf("Auth failed: %s", resp);
        close(sock);
        return 1;
    }

    printf("Auth status: %s", resp);
    printf("-----------------------------------------------------------------\n");
    printf("Valid commands: 'metrics', 'exit'.\n");
    printf("-----------------------------------------------------------------\n");

    char input[256];
    while (1) {
        printf("admin> ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("\n");
            break; // EOF (Ctrl+D)
        }

        // Limpiar el salto de línea al final del comando
        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') {
            input[len - 1] = '\0';
            len--;
        }
        if (len > 0 && input[len - 1] == '\r') {
            input[len - 1] = '\0';
            len--;
        }

        // Si la línea está vacía, continuar
        if (len == 0) {
            continue;
        }

        // Procesar comando local
        if (strcmp(input, "exit") == 0) {
            printf("Desconnecting...\n");
            break;
        }

        if (strcmp(input, "metrics") == 0) {
            // Enviar consulta
            const char *metrics_cmd = "METRICS\n";
            if (send(sock, metrics_cmd, strlen(metrics_cmd), 0) < 0) {
                perror("Failed to send METRICS command");
                break;
            }

            char metrics_resp[2048] = {0};
            if (read_response(sock, metrics_resp, sizeof(metrics_resp)) > 0) {
                printf("%s", metrics_resp);
            } else {
                printf("Failed to recive METRICS command");
                break;
            }
        } else {
            printf("Command not recognized. Valid commands 'metrics', 'exit'\n");
        }
    }

    close(sock);
    return 0;
}
