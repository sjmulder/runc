CC = clang
CFLAGS = -Wall -std=c99
LDFLAGS = -lcrypto

all: runc
runc: runc.c
