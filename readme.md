# ProtoSocks — Servidor Proxy SOCKS v5

Implementación de un servidor proxy para el protocolo SOCKS v5 ([RFC 1928](https://datatracker.ietf.org/doc/html/rfc1928)) desarrollado en C11 como Trabajo Práctico Especial para la materia **Protocolos de Comunicación** — ITBA, 1er cuatrimestre 2026.

## Índice

---
1. [Materiales del proyecto](#materiales-del-proyecto)
2. [Requisitos previos](#requisitos-previos)
3. [Compilación](#compilación)
4. [Artefactos generados](#artefactos-generados)
5. [Ejecución](#ejecución)
	- [Servidor SOCKS5](#servidor-socks5)
	- [Cliente de monitoreo](#cliente-de-monitoreo)
6. [Protocolo de monitoreo](#protocolo-de-monitoreo)
7. [Estructura del proyecto](#estructura-del-proyecto)
8. [Limitaciones](#limitaciones)


## Materiales del proyecto

---

| Material                                       | Ubicación                              |
|------------------------------------------------|----------------------------------------|
| Informe                                        | `doc/`                                 |
| Consigna                                       | `doc/Consigna TPE 2026C1 - Socks5.pdf` |
| Código fuente del servidor                     | `src/server/`                          |
| Código fuente del cliente de monitoreo         | `src/client/`                          |
| Código compartido (selector, buffers, parsers) | `src/shared/`                          |
| Parsers del protocolo SOCKS5                   | `src/socks5/`                          |
| Headers                                        | `include/`                             |
| Archivos de construcción                       | `Makefile`, `Makefile.inc`             |
| Binarios generados                             | `bin/`                                 |


## Requisitos previos

---
- **Sistema operativo:** Linux
- **Compilador:** GCC con soporte para C11
- **Herramientas:** GNU Make
- **Dependencias externas:** ninguna (solo libc y pthreads)
- Opcionales:
	- clang-format (para make format)
	- clang-tidy (para make tidy)


## Compilación

---
Desde la raíz del proyecto:
```bash
make            # Compila servidor y cliente
make server     # Compila solo el servidor
make client     # Compila solo el cliente de monitoreo
make clean      # Elimina binarios y objetos
```


## Artefactos generados

---
Luego de ejecutar `make`, los binarios se encuentran en el directorio `bin/`:

| Artefacto | Ubicación    | Descripción                                    |
|-----------|--------------|------------------------------------------------|
| `server`  | `bin/server` | Servidor proxy SOCKS v5                        |
| `client`  | `bin/client` | Cliente interactivo de monitoreo/configuración |


## Ejecución

---
### Servidor SOCKS5

```bash
./bin/server [OPCIONES]
```

**Opciones:**

| Opción             | Descripción                                | Default     |
|--------------------|--------------------------------------------|-------------|
| `-h`               | Muestra la ayuda y termina                 | —           |
| `-l <dirección>`   | Dirección donde escucha el proxy SOCKS     | `0.0.0.0`   |
| `-p <puerto>`      | Puerto para conexiones SOCKS               | `1080`      |
| `-L <dirección>`   | Dirección del servicio de monitoreo        | `127.0.0.1` |
| `-P <puerto>`      | Puerto del servicio de monitoreo           | `8080`      |
| `-t <token>`       | Token de autenticación para monitoreo      | `admin123`  |
| `-u <user>:<pass>` | Agrega un usuario SOCKS (hasta 10 vía CLI) | —           |
| `-N`               | Deshabilita disectores de protocolo        | habilitados |
| `-v`               | Muestra la versión y termina               | —           |

**Ejemplo:**

```bash
# Proxy en puerto 1080, monitoreo en puerto 9090, con un usuario
./bin/server -p 1080 -P 9090 -t miToken123 -u juan:password123
```

**Señales:**

- `SIGTERM` / `SIGINT`: inicia un apagado controlado (*graceful shutdown*). El servidor deja de aceptar nuevas conexiones y espera a que las activas finalicen.
- Una segunda señal fuerza la terminación inmediata.


### Cliente de monitoreo

---
```bash
./bin/client [OPCIONES]
```

**Opciones:**

| Opción           | Descripción                         | Default     |
|------------------|-------------------------------------|-------------|
| `-L <dirección>` | Dirección del servicio de monitoreo | `127.0.0.1` |
| `-P <puerto>`    | Puerto del servicio de monitoreo    | `8080`      |
| `-t <token>`     | Token de autenticación              | `admin123`  |
| `-h`             | Muestra la ayuda y termina          | —           |

**Ejemplo:**
```bash
./bin/client -P 9090 -t miToken123
```

**Comandos disponibles en la consola interactiva:**

| Comando                  | Descripción                                                                                 |
|--------------------------|---------------------------------------------------------------------------------------------|
| `help`                   | Muestra los comandos disponibles                                                            |
| `metrics`                | Muestra las métricas del servidor (conexiones históricas, concurrentes, bytes transferidos) |
| `access-log`             | Muestra el registro de accesos recientes                                                    |
| `add-user <user> <pass>` | Agrega o actualiza un usuario de autenticación SOCKS                                        |
| `del-user <user>`        | Elimina un usuario de autenticación SOCKS                                                   |
| `list-users`             | Lista los usuarios configurados                                                             |
| `exit`                   | Cierra el cliente                                                                           |


## Protocolo de monitoreo

---
El servidor expone un protocolo de monitoreo basado en texto sobre TCP en un puerto independiente del proxy SOCKS. La descripción completa del protocolo se encuentra en el informe (`doc/`).

**Resumen:**

- **Transporte:** TCP
- **Formato:** texto, orientado a líneas (delimitador `\n`)
- **Autenticación:** por token — el cliente debe enviar `AUTH <token>\n` como primer comando
- **Respuestas:** prefijo `+OK\n` indica éxito, prefijo `-ERR <mensaje>\n` indica error

**Comandos del protocolo:**

| Comando                  | Descripción                            |
|--------------------------|----------------------------------------|
| `AUTH <token>`           | Autenticación del cliente de monitoreo |
| `METRICS`                | Consulta de métricas del servidor      |
| `ACCESS-LOG`             | Consulta del registro de accesos       |
| `ADD-USER <user> <pass>` | Alta/modificación de usuario SOCKS     |
| `DEL-USER <user>`        | Baja de usuario SOCKS                  |
| `LIST-USERS`             | Listado de usuarios SOCKS              |


## Estructura del proyecto

---
```
ProtoSocks/
├── Makefile                     # Sistema de construcción principal
├── Makefile.inc                 # Variables de compilación
├── readme.md                    # Este archivo
├── bin/                         # Binarios generados
│   ├── server                   # Servidor proxy SOCKS v5
│   └── client                   # Cliente de monitoreo
├── doc/                         # Documentación e informe
├── include/
│   ├── client/                  # Headers del cliente de monitoreo
│   │   └── args_client.h
│   ├── server/                  # Headers del servidor
│   │   ├── args.h               # Parsing de argumentos del servidor
│   │   ├── metrics.h            # Métricas del sistema
│   │   ├── monitor.h            # Protocolo de monitoreo
│   │   ├── socks5nio.h          # Lógica NIO del proxy SOCKS5
│   │   ├── user_store.h         # Almacén de usuarios
│   │   └── access_log.h         # Registro de accesos
│   ├── shared/                  # Headers compartidos
│   │   ├── selector.h           # Multiplexor de I/O (pselect)
│   │   ├── buffer.h             # Buffer circular
│   │   ├── stm.h                # Máquina de estados
│   │   ├── parser.h             # Parser genérico
│   │   ├── parser_utils.h       # Utilidades de parsing
│   │   ├── netutils.h           # Utilidades de red
│   │   └── shared.h             # Constantes compartidas
│   └── socks5/                  # Headers del protocolo SOCKS5
│       ├── hello.h              # Negociación de métodos (HELLO)
│       ├── auth.h               # Autenticación usuario/contraseña (RFC 1929)
│       └── request.h            # Procesamiento de requests SOCKS5
└── src/
    ├── client/                  # Código fuente del cliente
    │   ├── main.c               # Punto de entrada del cliente
    │   └── args_client.c        # Parsing de argumentos del cliente
    ├── server/                  # Código fuente del servidor
    │   ├── main.c               # Punto de entrada, setup de sockets y selector
    │   ├── socks5nio.c          # Máquina de estados NIO del proxy SOCKS5
    │   ├── monitor.c            # Implementación del protocolo de monitoreo
    │   ├── metrics.c            # Recolección de métricas
    │   ├── user_store.c         # Gestión de usuarios
    │   ├── access_log.c         # Registro de accesos
    │   └── args.c               # Parsing de argumentos del servidor
    ├── shared/                  # Código compartido
    │   ├── selector.c           # Multiplexor de I/O basado en pselect(2)
    │   ├── buffer.c             # Implementación del buffer circular
    │   ├── stm.c                # Máquina de estados genérica
    │   ├── parser.c             # Parser genérico
    │   ├── parser_utils.c       # Utilidades de parsing
    │   ├── netutils.c           # Utilidades de red
    │   └── shared.c             # Implementaciones compartidas
    └── socks5/                  # Parsers del protocolo SOCKS5
        ├── hello.c              # Parser de negociación de métodos
        ├── auth.c               # Parser de autenticación RFC 1929
        └── request.c            # Parser de requests SOCKS5
```


## Limitaciones

---
- **Máximo ~510 conexiones concurrentes:** El selector utiliza `pselect(2)` con `fd_set`, limitado a `FD_SETSIZE` (1024 file descriptors). Como cada conexión SOCKS5 requiere 2 file descriptors (cliente + origen), el máximo teórico es ~510 conexiones simultáneas.
- **Solo IPv4 para los sockets pasivos del servidor:** Los sockets de escucha (SOCKS y monitoreo) se configuran con `AF_INET`. Las conexiones salientes sí soportan IPv4 e IPv6.
- **Resolución DNS en threads separados:** La resolución de nombres se delega a threads usando `getaddrinfo(3)` + `pthread_create`. No hay pool de threads ni límite en la cantidad de resoluciones concurrentes.
- **Métricas volátiles:** Las estadísticas se pierden al reiniciar el servidor.
- **Usuarios iniciales vía CLI limitados a 10:** Se pueden agregar más en runtime mediante el protocolo de monitoreo.