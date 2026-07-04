COMPILER=gcc

COMPILER_FLAGS=-std=c11 -D_POSIX_C_SOURCE=200112L -Wall -pedantic -g -I./src/shared -pthread

LD_FLAGS=-pthread
