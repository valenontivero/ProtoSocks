#include "hello.h"
#include "parser.h"
#include <stddef.h>

// Tipos de eventos
#define EVENT_NMETHODS HELLO_NMETHODS
#define EVENT_METHOD HELLO_METHODS
#define EVENT_ERROR HELLO_ERROR

// Eventos
static void version_arrival(struct parser_event *ret, const uint8_t c)
{
	(void) c;	// unused parameter
	(void) ret; // unused parameter
				// La transición ya verificó que sea 0x05. Si llegamos acá es éxito
				// de versión. No necesitamos emitir evento, simplemente avanzamos.
}

static void error_arrival(struct parser_event *ret, const uint8_t c)
{
	(void) c; // unused parameter
	ret->type = EVENT_ERROR;
	ret->n = 0;
}

static void nmethods_arrival(struct parser_event *ret, const uint8_t c)
{
	ret->type = EVENT_NMETHODS;
	ret->n = 1;
	ret->data[0] = c;
}

static void method_arrival(struct parser_event *ret, const uint8_t c)
{
	ret->type = EVENT_METHOD;
	ret->n = 1;
	ret->data[0] = c;
}

// Transisiones de estado

static const struct parser_state_transition st_version[] = {
	{.when = 0x05, .dest = HELLO_NMETHODS, .act1 = version_arrival},
	{.when = ANY, .dest = HELLO_ERROR, .act1 = error_arrival},
};

static const struct parser_state_transition st_nmethods[] = {
	{.when = ANY, .dest = HELLO_METHODS, .act1 = nmethods_arrival},
};

static const struct parser_state_transition st_methods[] = {
	{.when = ANY, .dest = HELLO_METHODS, .act1 = method_arrival},
};

// Declaraciones de estado
static const struct parser_state_transition *states[] = {
	st_version,	 // 0
	st_nmethods, // 1
	st_methods,	 // 2
	NULL,		 // DONE
	NULL,		 // ERROR
};

static const size_t states_n[] = {
	sizeof(st_version) / sizeof(st_version[0]),
	sizeof(st_nmethods) / sizeof(st_nmethods[0]),
	sizeof(st_methods) / sizeof(st_methods[0]),
	0, // DONE
	0, // ERROR
};

// Parser definition
static const struct parser_definition definition = {
	.states_count = 5,
	.states = states,
	.states_n = states_n,
	.start_state = HELLO_VERSION,
};

void hello_parser_init(struct hello_parser *p)
{
	p->parser = parser_init(parser_no_classes(), &definition);
	p->nmethods_remaining = 0;
	p->state = HELLO_VERSION;
}

void hello_parser_close(struct hello_parser *p)
{
	if (p->parser != NULL)
	{
		parser_destroy(p->parser);
		p->parser = NULL;
	}
}

enum hello_state hello_consume(buffer *b, struct hello_parser *p, bool *errored)
{
	while (buffer_can_read(b) && p->state != HELLO_DONE && p->state != HELLO_ERROR)
	{
		const uint8_t c = buffer_read(b);
		const struct parser_event *e = parser_feed(p->parser, c);

		if (e != NULL)
		{
			switch (e->type)
			{
				case EVENT_ERROR:
					p->state = HELLO_ERROR;
					if (errored != NULL)
						*errored = true;
					break;
				case EVENT_NMETHODS:
					p->nmethods_remaining = e->data[0];
					p->state = HELLO_METHODS;
					if (p->nmethods_remaining == 0)
					{
						p->state = HELLO_ERROR;
						if (errored != NULL)
							*errored = true;
					}
					break;
				case EVENT_METHOD:
					if (p->on_authentication_method != NULL)
					{
						p->on_authentication_method(p, e->data[0]);
					}
					p->nmethods_remaining--;
					if (p->nmethods_remaining == 0)
					{
						p->state = HELLO_DONE;
					}
					break;
				default:
					if (p->state == HELLO_VERSION)
					{
						p->state = HELLO_NMETHODS;
					}
					break;
			}
		}
	}
	return p->state;
}

bool hello_is_done(const enum hello_state state, bool *errored)
{
	if (state == HELLO_ERROR)
	{
		if (errored != NULL)
			*errored = true;
		return false;
	}
	return state == HELLO_DONE;
}

int hello_marshall(buffer *b, const uint8_t method)
{
	if (!buffer_can_write(b))
	{
		return -1;
	}
	buffer_write(b, SOCKS_HELLO_VERSION);
	if (!buffer_can_write(b))
	{
		return -1;
	}
	buffer_write(b, method);
	return 0;
}
