CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -g
LDFLAGS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11

SRC = $(wildcard src/*.c)
OUT = bitty

all: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(OUT) $(LDFLAGS)

run: $(OUT)
	./$(OUT)

clean:
	rm -f $(OUT)

.PHONY: all run clean
