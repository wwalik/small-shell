CC := gcc
CFLAGS := --std=gnu99

.PHONY: clean

smallsh: main.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm smallsh
