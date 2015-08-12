all: example

example: linenoise.hpp linenoise.cpp example.cpp
	g++ -std=c++11 -Wall -W -Os -g -pthread -o $@ linenoise.cpp example.cpp

clean:
	rm -f example *.o
