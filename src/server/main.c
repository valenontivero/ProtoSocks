/**
 * main.c - servidor proxy socks concurrente
 *
 * Interpreta los argumentos de línea de comandos, y monta un socket
 * pasivo.
 *
 * Todas las conexiones entrantes se manejarán en éste hilo.
 *
 * Se descargará en otro hilos las operaciones bloqueantes (resolución de
 * DNS utilizando getaddrinfo), pero toda esa complejidad está oculta en
 * el selector.
 */
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h> // socket
#include <sys/types.h>	// socket
#include <unistd.h>

// #include "socks5.h"
#include "args.h"
#include "metrics.h"
#include "monitor.h"
#include "selector.h"
#include "socks5nio.h"
#include "user_store.h"

#define SOMAXCONN_PATH "/proc/sys/net/core/somaxconn"

// Pagina 428 de The Linux Programming Interface
volatile sig_atomic_t shutdown_requested = false;
volatile sig_atomic_t signal_count = 0;

static int get_somaxconn(void);
static void sigterm_handler(int signal);

int main(const int argc, const char **argv)
{
	struct socks5args args;
	parse_args(argc, (char **) argv, &args);
	monitor_set_token(args.mng_token);

	for (size_t i = 0; i < MAX_USERS; i++)
	{
		if (args.users[i].name != NULL)
		{
			user_store_add((const uint8_t *) args.users[i].name, strlen(args.users[i].name),
						   (const uint8_t *) args.users[i].pass, strlen(args.users[i].pass));
		}
	}

	// no tenemos nada que leer de stdin
	close(0);

	const char *err_msg = NULL;
	selector_status ss = SELECTOR_SUCCESS;
	fd_selector selector = NULL;
	// por si falla antes de crear el socket de monitoreo
	int monitor_server = -1;

	// Ponemos la estructura en cero, para evitar levantar basura.
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));

	// Completamos la direccion del servidor: esta parte define donde escucha el servidor
	addr.sin_family = AF_INET; // IPv4
	if (inet_pton(AF_INET, args.socks_addr, &addr.sin_addr) <= 0)
	{
		err_msg = "invalid SOCKS bind address";
		goto finally;
	}
	addr.sin_port = htons(args.socks_port); // Puerto

	// Creamos el socket pasivo
	const int server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (server < 0)
	{
		err_msg = "unable to create socket";
		goto finally;
	}

	fprintf(stdout, "Listening on TCP port %d\n", args.socks_port);

	// man 7 ip. no importa reportar nada si falla.
	setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &(int) {1}, sizeof(int));

	// Bindeamos el servidor: asociamos el socket a IP + puerto
	if (bind(server, (struct sockaddr *) &addr, sizeof(addr)) < 0)
	{
		err_msg = "unable to bind socket";
		goto finally;
	}

	// Configuramos el tamaño de la cola de conexiones.
	// Leemos el backlog maximo del kernel desde /proc/sys/net/core/somaxconn
	// (configurable en runtime desde Linux 2.4.25, pag. 1157 de TLPI).
	const int backlog = get_somaxconn();
	fprintf(stdout, "Using listen backlog: %d (from %s)\n", backlog, SOMAXCONN_PATH);
	if (listen(server, backlog) < 0)
	{
		err_msg = "unable to listen";
		goto finally;
	}

	// Creamos el socket pasivo de monitoreo que escucha en el puerto 8080
	monitor_server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (monitor_server < 0)
	{
		err_msg = "unable to create monitor socket";
		goto finally;
	}

	struct sockaddr_in monitor_addr;
	memset(&monitor_addr, 0, sizeof(monitor_addr));
	monitor_addr.sin_family = AF_INET;
	if (inet_pton(AF_INET, args.mng_addr, &monitor_addr.sin_addr) <= 0)
	{
		err_msg = "invalid monitor bind address";
		goto finally;
	}
	monitor_addr.sin_port = htons(args.mng_port); // Puerto de monitoreo

	fprintf(stdout, "Listening on TCP port %d (monitoring)\n", args.mng_port);

	setsockopt(monitor_server, SOL_SOCKET, SO_REUSEADDR, &(int) {1}, sizeof(int));
	if (bind(monitor_server, (struct sockaddr *) &monitor_addr, sizeof(monitor_addr)) < 0)
	{
		err_msg = "unable to bind monitor socket";
		goto finally;
	}

	if (listen(monitor_server, backlog) < 0)
	{
		err_msg = "unable to listen monitor";
		goto finally;
	}

	// Registrar sigterm es útil para terminar el programa normalmente.
	// esto ayuda mucho en herramientas como valgrind.
	signal(SIGTERM, sigterm_handler);
	signal(SIGINT, sigterm_handler);

	// Poner el socket pasivo del proxy en modo no bloqueante
	if (selector_fd_set_nio(server) == -1)
	{
		err_msg = "getting server socket flags";
		goto finally;
	}
	// Poner el socket pasivo de monitoreo en no bloqueante
	if (selector_fd_set_nio(monitor_server) == -1)
	{
		err_msg = "getting monitor socket flags";
		goto finally;
	}

	// Inicializar el selector
	const struct selector_init conf = {
		.signal = SIGALRM,
		.select_timeout =
			{
				.tv_sec = 10,
				.tv_nsec = 0,
			},
	};
	if (0 != selector_init(&conf))
	{
		err_msg = "initializing selector";
		goto finally;
	}

	// Crear el selector concreto
	selector = selector_new(1024);
	if (selector == NULL)
	{
		err_msg = "unable to create selector";
		goto finally;
	}

	// El selector notificó actividad de lectura en el socket pasivo,
	// lo que indica una nueva conexión entrante. Se debe invocar accept()
	// para aceptarla y obtener el nuevo socket del cliente.
	const struct fd_handler socksv5 = {
		.handle_read = socksv5_passive_accept,
		.handle_write = NULL,
		.handle_close = NULL, // nada que liberar
	};

	// Configurar el selector para que ejectute socksv5 cuando haya OP_READ
	// Registar el socket en el selector, queremos que vigile este fd (server, el socket pasivo).
	// Si tiene algo para leer (OP_READ), queremos que ejecute las funciones que están en socksv5 (el fd_handler).
	ss = selector_register(selector, server, &socksv5, OP_READ, NULL);

	if (ss != SELECTOR_SUCCESS)
	{
		err_msg = "registering fd";
		goto finally;
	}

	const struct fd_handler monitor_socks = {
		.handle_read = monitor_passive_accept,
		.handle_write = NULL,
		.handle_close = NULL,
	};
	ss = selector_register(selector, monitor_server, &monitor_socks, OP_READ, NULL);

	if (ss != SELECTOR_SUCCESS)
	{
		err_msg = "registering monitor fd";
		goto finally;
	}

	bool listening = true;

	// Loop principal
	for (;;)
	{
		err_msg = NULL;

		if (shutdown_requested && listening)
		{
			listening = false;
			selector_unregister_fd(selector, server);
			selector_unregister_fd(selector, monitor_server);
		}

		if (!listening && get_metrics_concurrent_connections() == 0)
		{
			break;
		}

		ss = selector_select(selector);
		if (ss != SELECTOR_SUCCESS)
		{
			err_msg = "serving";
			goto finally;
		}
	}
	if (err_msg == NULL)
	{
		err_msg = "closing";
	}

	int ret = 0;

finally:
	if (ss != SELECTOR_SUCCESS)
	{
		fprintf(stderr, "%s: %s\n", (err_msg == NULL) ? "" : err_msg,
				ss == SELECTOR_IO ? strerror(errno) : selector_error(ss));
		ret = 2;
	}
	else if (err_msg)
	{
		perror(err_msg);
		ret = 1;
	}
	if (selector != NULL)
	{
		selector_destroy(selector);
	}
	selector_close();

	socksv5_pool_destroy();

	if (server >= 0)
	{
		close(server);
	}
	if (monitor_server >= 0)
	{
		close(monitor_server);
	}
	return ret;
}

/**
 * Lee el backlog máximo del kernel desde /proc/sys/net/core/somaxconn.
 * Disponible y configurable en runtime desde Linux 2.4.25.
 * Si no se puede leer, retorna SOMAXCONN definida en sys/socket.h
 * Leer página 1157 que explica porque se pone así
 */
static int get_somaxconn(void)
{
	FILE *fp = fopen(SOMAXCONN_PATH, "r");
	if (fp == NULL)
	{
		return SOMAXCONN;
	}

	int value;
	if (fscanf(fp, "%d", &value) != 1 || value <= 0)
	{
		value = SOMAXCONN;
	}
	fclose(fp);
	return value;
}

static void sigterm_handler(const int signal)
{
	printf("signal %d, cleaning up and exiting\n", signal);
	shutdown_requested = true;
	signal_count++;

	// Forzar salida si recibimos mas de una señal
	if (signal_count > 1)
	{
		exit(1);
	}
}