COMPILER=gcc

COMPILER_FLAGS=-std=c11 -D_POSIX_C_SOURCE=200112L -Wall -pedantic -g -I./include/shared -I./include/socks5 -I./include/server -pthread

LD_FLAGS=-pthread