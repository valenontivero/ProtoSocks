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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <signal.h>

#include <unistd.h>
#include <sys/types.h>   // socket
#include <sys/socket.h>  // socket
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

//#include "socks5.h"
#include "selector.h"
#include "socks5nio.h"
#include "monitor.h"
#include "args.h"
#include "user_store.h"

static bool done = false;

static void
sigterm_handler(const int signal) {
    printf("signal %d, cleaning up and exiting\n",signal);
    done = true;
}

int
main(const int argc, const char **argv) {
    struct socks5args args;
    parse_args(argc, (char **)argv, &args);
    monitor_set_token(args.mng_token);

    for(size_t i = 0; i < MAX_USERS; i++) {
        if(args.users[i].name != NULL) {
            user_store_add(args.users[i].name, strlen(args.users[i].name), args.users[i].pass, strlen(args.users[i].pass));
    }
}

    // no tenemos nada que leer de stdin
    close(0);

    const char       *err_msg = NULL;
    selector_status   ss      = SELECTOR_SUCCESS;
    fd_selector selector      = NULL;
    // por si falla antes de crear el socket de monitoreo
    int monitor_server        = -1;

    // Ponemos la estructura en cero, para evitar levantar basura.
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    // Completamos la direccion del servidor: esta parte define donde escucha el servidor
    addr.sin_family      = AF_INET;  // IPv4
    if(inet_pton(AF_INET, args.socks_addr, &addr.sin_addr) <= 0) {
        err_msg = "invalid SOCKS bind address";
        goto finally;
    }
    addr.sin_port        = htons(args.socks_port); // Puerto

    // Creamos el socket pasivo
    const int server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(server < 0) {
        err_msg = "unable to create socket";
        goto finally;
    }

    fprintf(stdout, "Listening on TCP port %d\n", args.socks_port);

    // man 7 ip. no importa reportar nada si falla.
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int));

    // Bindeamos el servidor: asociamos el socket a IP + puerto
    if(bind(server, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
        err_msg = "unable to bind socket";
        goto finally;
    }

    // Configuramos el tamaño de la cola de conexiones
    // Pueden haber 20 conexiones encoladas esperando ser aceptadas
    if (listen(server, 20) < 0) {
        err_msg = "unable to listen";
        goto finally;
    }

    // Creamos el socket pasivo de monitoreo que escucha en el puerto 8080
    monitor_server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(monitor_server < 0) {
        err_msg = "unable to create monitor socket";
        goto finally;
    }

    struct sockaddr_in monitor_addr;
    memset(&monitor_addr, 0, sizeof(monitor_addr));
    monitor_addr.sin_family      = AF_INET;
    if(inet_pton(AF_INET, args.mng_addr, &monitor_addr.sin_addr) <= 0) {
        err_msg = "invalid monitor bind address";
        goto finally;
    }
    monitor_addr.sin_port        = htons(args.mng_port); // Puerto de monitoreo

    fprintf(stdout, "Listening on TCP port %d (monitoring)\n", args.mng_port);

    setsockopt(monitor_server, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int));
    if(bind(monitor_server, (struct sockaddr*) &monitor_addr, sizeof(monitor_addr)) < 0) {
        err_msg = "unable to bind monitor socket";
        goto finally;
    }

    if (listen(monitor_server, 20) < 0) {
        err_msg = "unable to listen monitor";
        goto finally;
    }

    // Registrar sigterm es útil para terminar el programa normalmente.
    // esto ayuda mucho en herramientas como valgrind.
    signal(SIGTERM, sigterm_handler);
    signal(SIGINT,  sigterm_handler);

    // Poner el socket pasivo del proxy en modo no bloqueante 
    if(selector_fd_set_nio(server) == -1) {
        err_msg = "getting server socket flags";
        goto finally;
    }
    // Poner el socket pasivo de monitoreo en no bloqueante
    if(selector_fd_set_nio(monitor_server) == -1) {
        err_msg = "getting monitor socket flags";
        goto finally;
    }

    // Inicializar el selector
    const struct selector_init conf = {
        .signal = SIGALRM,
        .select_timeout = {
            .tv_sec  = 10,
            .tv_nsec = 0,
        },
    };
    if(0 != selector_init(&conf)) {
        err_msg = "initializing selector";
        goto finally;
    }

    // Crear el selector concreto
    selector = selector_new(1024);
    if(selector == NULL) {
        err_msg = "unable to create selector";
        goto finally;
    }

    // El selector notificó actividad de lectura en el socket pasivo,
    // lo que indica una nueva conexión entrante. Se debe invocar accept()
    // para aceptarla y obtener el nuevo socket del cliente.    
    const struct fd_handler socksv5 = {
        .handle_read       = socksv5_passive_accept,
        .handle_write      = NULL,
        .handle_close      = NULL, // nada que liberar
    };

    // Configurar el selector para que ejectute socksv5 cuando haya OP_READ
    // Registar el socket en el selector, queremos que vigile este fd (server, el socket pasivo). 
    // Si tiene algo para leer (OP_READ), queremos que ejecute las funciones que están en socksv5 (el fd_handler).
    ss = selector_register(selector, server, &socksv5, OP_READ, NULL);

    if(ss != SELECTOR_SUCCESS) {
        err_msg = "registering fd";
        goto finally;
    }

    const struct fd_handler monitor_socks = {
        .handle_read       = monitor_passive_accept,
        .handle_write      = NULL,
        .handle_close      = NULL,
    };
    ss = selector_register(selector, monitor_server, &monitor_socks, OP_READ, NULL);

    if(ss != SELECTOR_SUCCESS) {
        err_msg = "registering monitor fd";
        goto finally;
    }
    for(;!done;) {
        err_msg = NULL;
        // Loop principal
        ss = selector_select(selector);
        if(ss != SELECTOR_SUCCESS) {
            err_msg = "serving";
            goto finally;
        }
    }
    if(err_msg == NULL) {
        err_msg = "closing";
    }

    int ret = 0;

finally:
    if(ss != SELECTOR_SUCCESS) {
        fprintf(stderr, "%s: %s\n", (err_msg == NULL) ? "": err_msg,
                                  ss == SELECTOR_IO
                                      ? strerror(errno)
                                      : selector_error(ss));
        ret = 2;
    } else if(err_msg) {
        perror(err_msg);
        ret = 1;
    }
    if(selector != NULL) {
        selector_destroy(selector);
    }
    selector_close();

    socksv5_pool_destroy();

    if(server >= 0) {
        close(server);
    }
    if(monitor_server >= 0) {
        close(monitor_server);
    }
    return ret;
}
