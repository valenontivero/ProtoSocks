#include "args_client.h"
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned short port(const char *s)
{
	char *end = 0;
	const long sl = strtol(s, &end, 10);

	if (end == s || '\0' != *end || ((LONG_MIN == sl || LONG_MAX == sl) && ERANGE == errno) || sl < 0 || sl > USHRT_MAX)
	{
		fprintf(stderr, "port should be in the range of 1-65535: %s\n", s);
		exit(1);
	}

	return (unsigned short) sl;
}

static void usage(const char *progname)
{
	fprintf(stderr,
			"Usage: %s [OPTION]...\n"
			"\n"
			"   -L <conf addr>   Dirección del servicio de management.\n"
			"   -P <conf port>   Puerto del servicio de management.\n"
			"   -t <token>       Token de autenticación del admin.\n"
			"\n",
			progname);
	exit(1);
}

void parse_client_args(int argc, char **argv, struct client_args *args)
{
	args->mng_addr = "127.0.0.1";
	args->mng_port = 8080;
	args->token = "admin123";

	int c;
	while ((c = getopt(argc, argv, "L:P:t:h")) != -1)
	{
		switch (c)
		{
			case 'L':
				args->mng_addr = optarg;
				break;
			case 'P':
				args->mng_port = port(optarg);
				break;
			case 't':
				args->token = optarg;
				break;
			case 'h':
			default:
				usage(argv[0]);
				break;
		}
	}
}