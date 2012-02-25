all: gnutella

gnutella.o: gnutella.cc
	g++ -c gnutella.cc

gnutella: gnutella.o
	g++ gnutella.o -o gnutella

clean:
	rm -f *.o
	rm -f gnutella
