#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "args_client.h"
#include "shared.h"

// Lee la respuesta de la conexión
static ssize_t read_response(int sock, char *buf, size_t max_len)
{
	size_t total = 0;
	while (total < max_len - 1)
	{
		char c;
		ssize_t n = recv(sock, &c, 1, 0);
		if (n <= 0)
		{
			break; // Conexión cerrada o error
		}
		buf[total++] = c;

		// Verifica si terminamos con "+OK\n"
		if (total >= 4 && strcmp(buf + total - 4, "+OK\n") == 0)
		{
			break;
		}
		// Verifica si es un error de una sola línea terminada en '\n'
		if (total >= 5 && strncmp(buf, "-ERR", 4) == 0 && c == '\n')
		{
			break;
		}
	}
	buf[total] = '\0';
	return total;
}

int main(int argc, char **argv)
{
	printf("Admin Client Interactive Console version %s\n", protosocks_version());

	struct client_args args;
	parse_client_args(argc, argv, &args);

	// Creamos el socket del admin_cliente
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0)
	{
		perror("Unable to create socket");
		return 1;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(args.mng_port); // Puerto de monitoreo
	if (inet_pton(AF_INET, args.mng_addr, &addr.sin_addr) <= 0)
	{
		perror("Invalid address");
		close(sock);
		return 1;
	}

	// El selector detecta este connect y acepta la conexion del socket
	printf("Connecting to monitor server at %s:%u...\n", args.mng_addr, args.mng_port);
	if (connect(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0)
	{
		perror("Connection failed");
		close(sock);
		return 1;
	}

	// Enviamos autenticacion
	char auth_cmd[512];
	snprintf(auth_cmd, sizeof(auth_cmd), "AUTH %s\n", args.token);
	if (send(sock, auth_cmd, strlen(auth_cmd), 0) < 0)
	{
		perror("Failed to send AUTH command");
		close(sock);
		return 1;
	}

	char resp[2048] = {0};
	if (read_response(sock, resp, sizeof(resp)) <= 0)
	{
		printf("No response from server during authentication.\n");
		close(sock);
		return 1;
	}

	if (strncmp(resp, "-ERR", 4) == 0)
	{
		printf("Auth failed: %s", resp);
		close(sock);
		return 1;
	}

	printf("Auth status: %s", resp);
	printf("-----------------------------------------------------------------\n");
	printf("Valid commands: 'metrics', 'add-user <user> <pass>', 'del-user <user>', 'list-users', 'exit'.\n");
	printf("-----------------------------------------------------------------\n");

	char input[256];
	while (1)
	{
		printf("admin> ");
		fflush(stdout);

		if (fgets(input, sizeof(input), stdin) == NULL)
		{
			printf("\n");
			break; // EOF (Ctrl+D)
		}

		// Limpiar el salto de línea al final del comando
		size_t len = strlen(input);
		if (len > 0 && input[len - 1] == '\n')
		{
			input[len - 1] = '\0';
			len--;
		}
		if (len > 0 && input[len - 1] == '\r')
		{
			input[len - 1] = '\0';
			len--;
		}

		// Si la línea está vacía, continuar
		if (len == 0)
		{
			continue;
		}

		// Procesar comando local
		if (strcmp(input, "exit") == 0)
		{
			printf("Desconnecting...\n");
			break;
		}
		else if (strcmp(input, "metrics") == 0)
		{
			// Enviar consulta
			const char *metrics_cmd = "METRICS\n";
			if (send(sock, metrics_cmd, strlen(metrics_cmd), 0) < 0)
			{
				perror("Failed to send METRICS command");
				break;
			}

			char metrics_resp[2048] = {0};
			if (read_response(sock, metrics_resp, sizeof(metrics_resp)) > 0)
			{
				printf("%s", metrics_resp);
			}
			else
			{
				printf("Failed to receive METRICS command");
				break;
			}
		}
		else if (strncmp(input, "add-user ", 9) == 0)
		{
			char add_cmd[512];
			snprintf(add_cmd, sizeof(add_cmd), "ADD-USER %s\n", input + 9);

			if (send(sock, add_cmd, strlen(add_cmd), 0) < 0)
			{
				perror("Failed to send ADD-USER command");
				break;
			}

			char add_resp[2048] = {0};
			if (read_response(sock, add_resp, sizeof(add_resp)) > 0)
			{
				printf("%s", add_resp);
			}
			else
			{
				printf("Failed to receive ADD-USER response\n");
				break;
			}
		}
		else if (strncmp(input, "del-user ", 9) == 0)
		{
			char del_cmd[512];
			snprintf(del_cmd, sizeof(del_cmd), "DEL-USER %s\n", input + 9);

			if (send(sock, del_cmd, strlen(del_cmd), 0) < 0)
			{
				perror("Failed to send DEL-USER command");
				break;
			}

			char del_resp[2048] = {0};
			if (read_response(sock, del_resp, sizeof(del_resp)) > 0)
			{
				printf("%s", del_resp);
			}
			else
			{
				printf("Failed to receive DEL-USER response\n");
				break;
			}
		}
		else if (strcmp(input, "list-users") == 0)
		{
			char *list_cmd = "LIST-USERS\n";

			if (send(sock, list_cmd, strlen(list_cmd), 0) < 0)
			{
				perror("Failed to send LIST-USERS command");
				break;
			}

			char list_resp[2048] = {0};
			if (read_response(sock, list_resp, sizeof(list_resp)) > 0)
			{
				printf("%s", list_resp);
			}
			else
			{
				printf("Failed to receive LIST-USERS response\n");
				break;
			}
		}
		else
		{
			printf("Command not recognized. Valid commands 'metrics', 'add-user <user> <pass>', 'del-user <user>', "
				   "'list-users', 'exit'\n");
		}
	}

	close(sock);
	return 0;
}
