all: dirs \
	 c/example \
	 c/linenoise \
	 h/linenoise

dirs:
	mkdir -p c h

c/linenoise: ../linenoise.c
	ln -sf ../$? $@
h/linenoise: ../linenoise.h
	ln -sf ../$? $@
c/example: ../example.c
	ln -sf ../$? $@
