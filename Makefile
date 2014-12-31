LINK = gcc
CC = gcc
LDFLAGS = $(shell sdl2-config --libs) -lGL -lm
CFLAGS = -std=c11 -Wall -Wextra -pedantic -Wno-missing-field-initializers $(shell sdl2-config --cflags)
RM = rm -f

OBJS = $(patsubst %.c,%.o,$(wildcard *.c))

.PHONY: clean all

all: oszim

oszim: $(OBJS)
	$(LINK) $^ -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) oszim $(OBJS)
