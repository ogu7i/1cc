CFLAGS=-std=c11 -g -fno-common

1cc: main.o
	$(CC) -o 1cc main.o $(LDFLAGS)

test: 1cc
	./test.sh

clean:
	rm -f 1cc *.o *~ tmp*

.PHONY: test clean
