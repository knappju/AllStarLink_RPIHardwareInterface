# --- Directories ---
IDIR = include
SDIR = src
ODIR = obj
BDIR = bin

# --- Compiler Settings ---
CC = gcc
CFLAGS = -I$(IDIR) -Wall -Wextra -g
LIBS = -lwiringPi

# --- Files ---
_SRCS = $(wildcard $(SDIR)/*.c)
_OBJS = $(patsubst $(SDIR)/%.c, $(ODIR)/%.o, $(_SRCS))
TARGET = $(BDIR)/allstarlink.exe

# --- Rules ---
all: $(TARGET)

$(TARGET): $(_OBJS) | $(BDIR)
	$(CC) $(_OBJS) -o $@ $(LIBS)

$(ODIR)/%.o: $(SDIR)/%.c | $(ODIR)
	$(CC) -c -o $@ $< $(CFLAGS)

$(BDIR) $(ODIR):
	mkdir -p $@

# Removes the compiled files so you can start fresh
clean:
	rm -rf $(ODIR) $(BDIR)

.PHONY: all clean