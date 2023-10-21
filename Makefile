CFLAGS=-std=c11 -g -fno-common -static
SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

1cc: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJS): 1cc.h

test: 1cc
	./test.sh

clean:
	rm -f 1cc *.o *~ tmp*

.PHONY: test clean
