CC=mpicc
CFLAGS=-std=c11 -O2 -Wall -Wextra -Wpedantic -Iinclude
LDFLAGS=

SRCS=$(wildcard src/*.c)
COMMON_SRCS=$(filter-out src/main.c src/main_serial.c src/main_central.c src/main_decentralized.c,$(SRCS))
COMMON_OBJS=$(COMMON_SRCS:.c=.o)

TARGETS=parallel_cbs central_cbs serial_cbs decentralized_cbs

all: $(TARGETS)

parallel_cbs: $(COMMON_OBJS) src/main.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

central_cbs: $(COMMON_OBJS) src/main_central.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

serial_cbs: $(COMMON_OBJS) src/main_serial.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

decentralized_cbs: $(COMMON_OBJS) src/main_decentralized.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(COMMON_OBJS) src/main.o src/main_serial.o src/main_central.o src/main_decentralized.o $(TARGETS)

.PHONY: all clean
