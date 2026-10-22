CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -fPIC
libmoepager_preload.so: moepager_preload.c
	$(CC) $(CFLAGS) -shared -o $@ $< -ldl
clean:
	rm -f libmoepager_preload.so
