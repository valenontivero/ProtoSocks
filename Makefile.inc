COMPILER=gcc

COMPILER_FLAGS=-std=c11 -D_POSIX_C_SOURCE=200112L -Wall -pedantic -g -I./include/shared -I./include/socks5 -I./include/server -I./include/client -pthread
CLANG_TIDY_FIX_FLAGS=--fix-errors --format-style=file --use-color

LD_FLAGS=-pthread