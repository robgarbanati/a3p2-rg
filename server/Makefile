CC      = $(CROSS_COMPILE)gcc
CFLAGS  = -Wall -Werror

.PHONY: all default clean

all default: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f aesdsocket
