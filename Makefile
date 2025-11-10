CC=mpicc
CFLAGS=-std=c11 -O2 -Wall -Wextra -Wpedantic -Iinclude
LDFLAGS=
TARGET=parallel_cbs
SRCS=$(wildcard src/*.c)
OBJS=$(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
