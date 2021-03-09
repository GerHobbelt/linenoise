CC = clang
CFLAGS = --target=wasm32-wasi --sysroot=/opt/wasi-sdk/wasi-sysroot -Wall -W -Os -g 

all: linenoise_example linenoise.o

.c.o:
	$(CC) $(CFLAGS) -c $<

linenoise_example: linenoise.o example.c
	$(CC) $(CFLAGS) -o linenoise_example linenoise.c example.c

clean:
	rm -f linenoise_example linenoise.o
