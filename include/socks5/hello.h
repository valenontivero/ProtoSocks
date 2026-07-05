// Se encarga de entender el primer mensaje que manda el cliente SOCKS5
// Es el parser del saludo inicial de SOCKS5: consume bytes del cliente, 
// valida que formen un HELLO correcto, 
// detecta qué métodos de autenticación ofrece y permite armar la respuesta del servidor.
//
// Por el estandar del protocolo socks5 el mensaje es de la forma:
// +----+----------+----------+
// |VER | NMETHODS | METHODS  |
// +----+----------+----------+
// | 1  |    1     | 1 to 255 |
// +----+----------+----------+
//
// Ejemplo: 05 01 00
// 05 = versión SOCKS5
// 01 = te ofrezco 1 método de autenticación
// 00 = método "sin autenticación"


#ifndef HELLO_H
#define HELLO_H

#include <stdint.h>
#include <stdbool.h>
#include "buffer.h"
#include "parser.h"

// SOCKS5 Version
#define SOCKS_HELLO_VERSION 0x05

// Métodos de autenticación definidos por el estándar SOCKSv5
#define SOCKS_HELLO_NOAUTHENTICATION_REQUIRED 0x00
#define SOCKS_HELLO_NO_ACCEPTABLE_METHODS 0xFF

// Parser states
enum hello_state {
    HELLO_VERSION, // Valida que el primer byte recibido sea 0x05
    HELLO_NMETHODS, // Lee cuantos metodos de autenticacion ofrece el cliente
    HELLO_METHODS, // Lee uno a uno los metodos
    HELLO_DONE, // Indica que terminamos de procesar el saludo con exito
    HELLO_ERROR, // Si la version no es 0x05 o hay un error de lectura
};


struct hello_parser {
    enum hello_state state;
    
    // Internal state storage
    struct parser *parser;
    uint8_t nmethods_remaining;       // Cantidad de métodos que nos quedan por leer (NMETHODS)

    // Callback para cuando se lee un método de autenticación del cliente
    void (*on_authentication_method)(struct hello_parser *p, const uint8_t method);   
    void *data;              // Puntero genérico para guardar el método seleccionado (u otros datos)

};

// Inicializa el parser
void hello_parser_init(struct hello_parser *p);

// Libera los recursos del parser
void hello_parser_close(struct hello_parser *p);

// Consume bytes de un buffer y avanza el estado del parser. Devuelve el estado actual.
enum hello_state hello_consume(buffer *b, struct hello_parser *p, bool *errored);

// Indica si el parser ha terminado con éxito
bool hello_is_done(const enum hello_state state, bool *errored);

// Escribe la respuesta del saludo en el buffer de salida: [0x05, seleccionado]
int hello_marshall(buffer *b, const uint8_t method);

#endif
