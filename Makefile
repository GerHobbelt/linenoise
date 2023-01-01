PREFIX = /usr/local
LIBDIR = $(PREFIX)/lib
INCDIR = $(PREFIX)/include
MANDIR = $(PREFIX)/share/man
CC = cc
CFLAGS = -Os -Wall -Wextra

SRC = linenoise.c utf8.c
OBJ = $(SRC:.c=.o)
LIB = liblinenoise.a
SLIB = liblinenoise.so
INC = linenoise.h utf8.h
MAN = linenoise.3

all: $(LIB) $(SLIB) example

$(LIB): $(OBJ)
	$(AR) -rcs $@ $(OBJ)

$(SLIB): $(OBJ)
	$(CC) -shared $(OBJ) -o $@

example: example.o $(LIB)
	$(CC) -o $@ example.o $(LIB)

.c.o:
	$(CC) $(CFLAGS) -c $<

install: $(LIB) $(INC) $(MAN)
	mkdir -p $(DESTDIR)$(LIBDIR)
	cp $(LIB) $(SLIB) $(DESTDIR)$(LIBDIR)
	mkdir -p $(DESTDIR)$(INCDIR)
	cp -t $(DESTDIR)$(INCDIR) $(INC)
	mkdir -p $(DESTDIR)$(MANDIR)/man3
	cp $(MAN) $(DESTDIR)$(MANDIR)/man3/$(MAN)

lib: linenoise.h linenoise.c
	$(CC) -Wall -W -Os -o linenoise.o linenoise.c encodings/utf8.c
	ar rcs liblinenoise.a linenoise.o utf8.o

clean:
	rm -f $(LIB) example example.o $(OBJ)
