SRCDIR = src
INCDIR = include
BINDIR = bin
BLDDIR = build
RSRCDIR = rsrc

CC = gcc
SRC = $(SRCDIR)/utfconverter.c
BIN = $(BINDIR)/utf
CFLAGS = -g -Wall -Werror -pedantic -Wextra -I$(INCDIR)

OBJS = $(SRC:$(SRCDIR)/%.c=$(BLDDIR)/%.o)

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(SRC) -o $(BIN)

$(BLDDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -f $(OBJS) $(BIN)
