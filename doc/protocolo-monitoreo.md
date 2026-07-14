# ProtoSocks Monitor Protocol (PSMP)

## Resumen

Este documento especifica el **ProtoSocks Monitor Protocol (PSMP)**, un protocolo de texto que permite a un administrador conectarse al servidor proxy SOCKS5 ProtoSocks para consultar métricas de operación, revisar el registro de accesos y gestionar los usuarios habilitados para usar el proxy.

---

## Tabla de Contenidos

1. [Introducción](#1-introducción)
2. [Transporte](#2-transporte)
3. [Formato de Mensajes](#3-formato-de-mensajes)
4. [Autenticación](#4-autenticación)
5. [Comandos](#5-comandos)
   - 5.1 [AUTH](#51-auth)
   - 5.2 [METRICS](#52-metrics)
   - 5.3 [ACCESS-LOG](#53-access-log)
   - 5.4 [LIST-USERS](#54-list-users)
   - 5.5 [ADD-USER](#55-add-user)
   - 5.6 [DEL-USER](#56-del-user)
6. [Códigos de Respuesta](#6-códigos-de-respuesta)
7. [Límites y Restricciones](#7-límites-y-restricciones)
8. [Cliente de Referencia](#8-cliente-de-referencia)

---

## 1. Introducción

El servidor proxy SOCKS5 ProtoSocks expone un puerto de administración separado del puerto SOCKS5 principal. A través de este puerto se sirve el **ProtoSocks Monitor Protocol (PSMP)**, un protocolo de texto, que permite a un operador administrar el servidor en tiempo real sin reiniciarlo.

Las capacidades provistas por PSMP son:

- Consultar métricas de operación del proxy (conexiones históricas, concurrentes, bytes transferidos).
- Consultar el registro de accesos reciente (últimas conexiones SOCKS5 realizadas).
- Listar, agregar y eliminar usuarios autorizados a usar el proxy SOCKS5.

---

## 2. Transporte

- **Protocolo de red:** TCP (IPv4)
- **Puerto por defecto del servidor:** `8080` (configurable con `-P <puerto>`)
- **Dirección por defecto del servidor:** `127.0.0.1` (configurable con `-L <addr>`)
- **Modelo de I/O en el servidor:** No bloqueante, multiplexado con `select(2)`.
- **Modelo de I/O en el cliente:** Bloqueante (dado que el cliente es un proceso dedicado e interactivo).
- **Encoding:** ASCII. Los nombres de usuario y contraseñas son secuencias de bytes ASCII imprimibles.

---

## 3. Formato de Mensajes

### 3.1 Comandos (Cliente → Servidor)

Los comandos son líneas de texto ASCII terminadas en `\n`:

```
<COMANDO> [ARGUMENTO1 [ARGUMENTO2]]\n
```

- Los tokens de comando están en **MAYÚSCULAS**.
- Los argumentos se separan por un único espacio.
- El `\r` antes del `\n` es aceptado y descartado (compatibilidad con terminales Windows).
- La longitud máxima de una línea es de **1023 bytes** (sin contar el `\n`). Las líneas más largas provocan un error de protocolo y el cierre de la conexión.

### 3.2 Respuestas (Servidor → Cliente)

Toda respuesta del servidor es una secuencia de líneas terminadas en `\n`. Existen dos formatos:

**Respuesta de éxito simple:**
```
+OK\n
```

**Respuesta de éxito con datos (respuesta multi-línea):**
```
<clave>:<valor>\n
[<clave>:<valor>\n ...]
[<item>\n ...]
+OK\n
```

La línea `+OK\n` al final actúa como **terminador de respuesta**. El cliente debe leer hasta encontrar `+OK\n` para considerar la respuesta completa.

**Respuesta de error (siempre de una sola línea):**
```
-ERR <descripción>\n
```

Las respuestas de error no van seguidas de `+OK\n`; la línea `-ERR ...` es completa por sí sola.

---

## 4. Autenticación

### 4.1 Mecanismo

PSMP utiliza autenticación por **token compartido** (*shared secret*). El token es una cadena de texto arbitraria configurada tanto en el servidor como en el cliente al momento de iniciar los procesos.

**Configuración del servidor:**
```
./bin/server -t <token>          # token de monitoreo
```
El token por defecto (si no se especifica) es `admin123`.

**Configuración del cliente:**
```
./bin/client -t <token>
```

### 4.2 Flujo de autenticación

1. El cliente establece la conexión TCP con el servidor.
2. El cliente envía **inmediatamente** el comando `AUTH` con el token.
3. El servidor valida el token:
   - Si es correcto, responde `+OK\n` y la sesión queda **autenticada**.
   - Si es incorrecto, responde `-ERR Invalid token\n` y **cierra la conexión**.
4. Todos los comandos posteriores a `AUTH` requieren que la sesión esté autenticada; de lo contrario, el servidor responde `-ERR Not authenticated\n`.

```
Cliente                              Servidor
  |                                      |
  |---TCP connect ---------------------->|
  |                                      |
  |---AUTH admin123\n ------------------>|
  |                                      |
  |<--+OK\n -----------------------------|
  |                                      |
  |   (sesión autenticada)               |
```

### 4.3 Razón del diseño

Se eligió token compartido por su simplicidad de implementación y de configuración. El token viaja en texto plano, lo cual es aceptable dado que:
- El puerto de monitoreo escucha sólo en `127.0.0.1` por defecto.
- El canal de administración no está expuesto a Internet.

---

## 5. Comandos

### 5.1 AUTH

**Propósito:** Autenticar la sesión de administración.

**Sintaxis:**
```
AUTH <token>\n
```

| Campo | Descripción |
|---|---|
| `token` | Cadena ASCII del secreto compartido configurado en el servidor. |

**Respuestas:**

| Condición | Respuesta |
|---|---|
| Token válido | `+OK\n` |
| Token inválido | `-ERR Invalid token\n` (y cierre de conexión) |

**Ejemplo:**
```
→ AUTH supersecret\n
← +OK\n
```

---

### 5.2 METRICS

**Propósito:** Consultar los contadores de operación del proxy SOCKS5.

**Sintaxis:**
```
METRICS\n
```

**Respuesta exitosa:**
```
historic_connections:<N>\n
concurrent_connections:<M>\n
bytes_transferred:<B>\n
+OK\n
```

| Campo | Tipo | Descripción |
|---|---|---|
| `historic_connections` | uint64 | Total de conexiones SOCKS5 aceptadas desde el inicio del servidor. |
| `concurrent_connections` | uint32 | Conexiones SOCKS5 activas en este momento. |
| `bytes_transferred` | uint64 | Bytes totales retransmitidos por el proxy (suma de ambas direcciones). |

**Respuestas de error:**

| Condición | Respuesta |
|---|---|
| No autenticado | `-ERR Not authenticated\n` |

**Ejemplo:**
```
→ METRICS\n
← historic_connections:42\n
← concurrent_connections:3\n
← bytes_transferred:1048576\n
← +OK\n
```

---

### 5.3 ACCESS-LOG

**Propósito:** Consultar el registro circular de los últimos intentos de conexión SOCKS5.

**Sintaxis:**
```
ACCESS-LOG\n
```

**Respuesta exitosa:**
```
access_log:<N>\n
<timestamp> user=<usuario> dst=<destino> port=<puerto> status=<estado>\n
...
+OK\n
```

| Campo | Descripción |
|---|---|
| `N` | Cantidad de entradas devueltas (máximo 32). |
| `timestamp` | Fecha y hora local en formato `YYYY-MM-DD HH:MM:SS`. |
| `user` | Nombre de usuario SOCKS5 que realizó la conexión (o `-` si no aplica). |
| `dst` | Hostname o dirección IP de destino solicitada. |
| `port` | Puerto de destino (decimal sin signo). |
| `status` | Estado de la conexión: `ok` si fue exitosa, `failed` si fue rechazada. |

El log es circular con capacidad para **32 entradas**. Cuando se llena, las entradas más antiguas son sobrescritas.

**Respuestas de error:**

| Condición | Respuesta |
|---|---|
| No autenticado | `-ERR Not authenticated\n` |

**Ejemplo:**
```
→ ACCESS-LOG\n
← access_log:2\n
← 2026-07-13 22:15:01 user=ana dst=example.com port=443 status=ok\n
← 2026-07-13 22:17:44 user=juan dst=10.0.0.1 port=80 status=failed\n
← +OK\n
```

---

### 5.4 LIST-USERS

**Propósito:** Listar los usuarios SOCKS5 actualmente configurados en el servidor.

**Sintaxis:**
```
LIST-USERS\n
```

**Respuesta exitosa:**
```
users:<N>\n
<username1>\n
<username2>\n
...
+OK\n
```

| Campo | Descripción |
|---|---|
| `N` | Cantidad de usuarios activos. |
| `<usernameK>` | Nombre de usuario (sin contraseña). |

**Respuestas de error:**

| Condición | Respuesta |
|---|---|
| No autenticado | `-ERR Not authenticated\n` |

**Ejemplo:**
```
→ LIST-USERS\n
← users:3\n
← admin\n
← ana\n
← juan\n
← +OK\n
```

---

### 5.5 ADD-USER

**Propósito:** Agregar un nuevo usuario SOCKS5 al servidor.

**Sintaxis:**
```
ADD-USER <username> <password>\n
```

| Campo | Restricción |
|---|---|
| `username` | Sin espacios. Longitud entre 1 y 255 bytes (`USER_STORE_MAX_FIELD`). |
| `password` | Sin espacios. Longitud entre 1 y 255 bytes (`USER_STORE_MAX_FIELD`). |

Se aceptan exactamente dos argumentos. Si se proporcionan más o menos argumentos, se devuelve un error.

El usuario no puede ya existir en el store. Si existe, la operación falla (no actualiza la contraseña).

**Respuestas:**

| Condición | Respuesta |
|---|---|
| Usuario agregado exitosamente | `+OK\n` |
| No autenticado | `-ERR Not authenticated\n` |
| Argumentos inválidos (cantidad incorrecta, campos vacíos o demasiado largos) | `-ERR Invalid arguments\n` |
| El usuario ya existe o el store está lleno (máx. 10 usuarios) | `-ERR Could not add user\n` |

**Ejemplo:**
```
→ ADD-USER juan secreto123\n
← +OK\n
```

---

### 5.6 DEL-USER

**Propósito:** Eliminar un usuario SOCKS5 existente del servidor.

**Sintaxis:**
```
DEL-USER <username>\n
```

| Campo | Restricción |
|---|---|
| `username` | Sin espacios. El usuario debe existir en el store. |

Se acepta exactamente un argumento.

**Respuestas:**

| Condición | Respuesta |
|---|---|
| Usuario eliminado exitosamente | `+OK\n` |
| No autenticado | `-ERR Not authenticated\n` |
| Argumentos inválidos | `-ERR Invalid arguments\n` |
| El usuario no existe | `-ERR Could not remove user\n` |

**Ejemplo:**
```
→ DEL-USER juan\n
← +OK\n
```

---

## 6. Códigos de Respuesta

### 6.1 Respuestas de éxito

| Respuesta | Significado |
|---|---|
| `+OK` | La operación fue exitosa. Termina toda respuesta exitosa. |

### 6.2 Respuestas de error

| Respuesta | Descripción |
|---|---|
| `-ERR Not authenticated` | El comando requiere autenticación previa con `AUTH`. |
| `-ERR Invalid token` | El token provisto en `AUTH` no coincide con el configurado. |
| `-ERR Invalid arguments` | El comando tiene una cantidad incorrecta de argumentos o los argumentos son inválidos. |
| `-ERR Unknown command` | El comando enviado no es reconocido por el servidor. |
| `-ERR Could not add user` | No se pudo agregar el usuario (ya existe o el store está lleno). |
| `-ERR Could not remove user` | No se pudo eliminar el usuario (no existe). |
| `-ERR Command too long` | La línea enviada excede el límite de 1023 bytes. Termina la conexión. |

---

## 7. Límites y Restricciones

| Parámetro | Valor |
|---|---|
| Longitud máxima de línea de comando | 1023 bytes |
| Capacidad del buffer de escritura por cliente | 16384 bytes (16 KB) |
| Entradas máximas en el log de acceso | 32 |
| Usuarios máximos en el store | 10 (`USER_STORE_MAX_USERS`) |
| Longitud máxima de username / password | 255 bytes (`USER_STORE_MAX_FIELD`) |
| Longitud máxima de destino en log | 255 bytes |
| Longitud máxima de username en log | 64 bytes |

---

## 8. Cliente de Referencia

El paquete ProtoSocks incluye una aplicación cliente de referencia (`bin/client`) que implementa PSMP con I/O bloqueante.

### 8.1 Uso

```
bin/client [OPCIONES]

Opciones:
  -L <addr>   Dirección del servidor de monitoreo  (default: 127.0.0.1)
  -P <port>   Puerto del servidor de monitoreo     (default: 8080)
  -t <token>  Token de autenticación               (default: admin123)
```

**Ejemplo:**
```bash
./bin/client -L 127.0.0.1 -P 8080 -t mysecrettoken
```

### 8.2 Interfaz interactiva

Al conectarse, el cliente se autentica automáticamente y presenta el prompt `admin>`:

```
Admin Client Interactive Console version 0.1.0
Auth status: +OK
-----------------------------------------------------------------
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣀⡠⢤⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⡴⠟⠃⠀⠀⠙⣄⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣠⠋⠀⠀⠀⠀⠀⠀⠘⣆⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢠⠾⢛⠒⠀⠀⠀⠀⠀⠀⠀⢸⡆⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣿⣶⣄⡈⠓⢄⠠⡀⠀⠀⠀⣄⣷⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⣿⣷⠀⠈⠱⡄⠑⣌⠆⠀⠀⡜⢻⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸⣿⡿⠳⡆⠐⢿⣆⠈⢿⠀⠀⡇⠘⡆⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢿⣿⣷⡇⠀⠀⠈⢆⠈⠆⢸⠀⠀⢣⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠘⣿⣿⣿⣧⠀⠀⠈⢂⠀⡇⠀⠀⢨⠓⣄⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣸⣿⣿⣿⣦⣤⠖⡏⡸⠀⣀⡴⠋⠀⠈⠢⡀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢠⣾⠁⣹⣿⣿⣿⣷⣾⠽⠖⠊⢹⣀⠄⠀⠀⠀⠈⢣⡀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡟⣇⣰⢫⢻⢉⠉⠀⣿⡆⠀⠀⡸⡏⠀⠀⠀⠀⠀⠀⢇
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢨⡇⡇⠈⢸⢸⢸⠀⠀⡇⡇⠀⠀⠁⠻⡄⡠⠂⠀⠀⠀⠘
⢤⣄⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢠⠛⠓⡇⠀⠸⡆⢸⠀⢠⣿⠀⠀⠀⠀⣰⣿⣵⡆⠀⠀⠀⠀
⠈⢻⣷⣦⣀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣠⡿⣦⣀⡇⠀⢧⡇⠀⠀⢺⡟⠀⠀⠀⢰⠉⣰⠟⠊⣠⠂⠀⡸
⠀⠀⢻⣿⣿⣷⣦⣀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣠⢧⡙⠺⠿⡇⠀⠘⠇⠀⠀⢸⣧⠀⠀⢠⠃⣾⣌⠉⠩⠭⠍⣉⡇
⠀⠀⠀⠻⣿⣿⣿⣿⣿⣦⣀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣠⣞⣋⠀⠈⠀⡳⣧⠀⠀⠀⠀⠀⢸⡏⠀⠀⡞⢰⠉⠉⠉⠉⠉⠓⢻⠃
⠀⠀⠀⠀⠹⣿⣿⣿⣿⣿⣿⣷⡄⠀⠀⢀⣀⠠⠤⣤⣤⠤⠞⠓⢠⠈⡆⠀⢣⣸⣾⠆⠀⠀⠀⠀⠀⢀⣀⡼⠁⡿⠈⣉⣉⣒⡒⠢⡼⠀
⠀⠀⠀⠀⠀⠘⣿⣿⣿⣿⣿⣿⣿⣎⣽⣶⣤⡶⢋⣤⠃⣠⡦⢀⡼⢦⣾⡤⠚⣟⣁⣀⣀⣀⣀⠀⣀⣈⣀⣠⣾⣅⠀⠑⠂⠤⠌⣩⡇⠀
⠀⠀⠀⠀⠀⠀⠘⢿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡁⣺⢁⣞⣉⡴⠟⡀⠀⠀⠀⠁⠸⡅⠀⠈⢷⠈⠏⠙⠀⢹⡛⠀⢉⠀⠀⠀⣀⣀⣼⡇⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠈⠻⣿⣿⣿⣿⣿⣿⣿⣿⣽⣿⡟⢡⠖⣡⡴⠂⣀⣀⣀⣰⣁⣀⣀⣸⠀⠀⠀⠀⠈⠁⠀⠀⠈⠀⣠⠜⠋⣠⠁⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠙⢿⣿⣿⣿⡟⢿⣿⣿⣷⡟⢋⣥⣖⣉⠀⠈⢁⡀⠤⠚⠿⣷⡦⢀⣠⣀⠢⣄⣀⡠⠔⠋⠁⠀⣼⠃⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠻⣿⣿⡄⠈⠻⣿⣿⢿⣛⣩⠤⠒⠉⠁⠀⠀⠀⠀⠀⠉⠒⢤⡀⠉⠁⠀⠀⠀⠀⠀⢀⡿⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠙⢿⣤⣤⠴⠟⠋⠉⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠑⠤⠀⠀⠀⠀⠀⢩⠇⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
Valid commands:
  help                    Show this command list.
  metrics                 Show proxy counters: connections and transferred bytes.
  access-log              Show recent SOCKS connection attempts.
  add-user <user> <pass>  Add or update a SOCKS authentication user.
  del-user <user>         Remove a SOCKS authentication user.
  list-users              List configured SOCKS authentication users.
  exit                    Close the monitor client.
-----------------------------------------------------------------
admin>
```

### 8.3 Mapeo de comandos locales a mensajes PSMP

| Comando del operador | Mensaje PSMP enviado |
|---|---|
| `metrics` | `METRICS\n` |
| `access-log` | `ACCESS-LOG\n` |
| `list-users` | `LIST-USERS\n` |
| `add-user <u> <p>` | `ADD-USER <u> <p>\n` |
| `del-user <u>` | `DEL-USER <u>\n` |
| `help` | *(procesado localmente, no envía mensaje)* |
| `exit` | *(cierra el socket TCP, no envía mensaje)* |

El cliente lee la respuesta del servidor hasta encontrar `+OK\n` (éxito) o una línea que comience con `-ERR` (error), y la imprime en la salida estándar.

---
