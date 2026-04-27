# --- Directories ---
IDIR = include
SDIR = src
ODIR = obj
BDIR = bin

# --- Compiler Settings ---
CC = gcc
CFLAGS = -I$(IDIR) -Wall -Wextra -g
LIBS = -lwiringPi -lcjson -lm

# --- Files ---
_SRCS = $(wildcard $(SDIR)/*.c)
_OBJS = $(patsubst $(SDIR)/%.c, $(ODIR)/%.o, $(_SRCS))
TARGET = $(BDIR)/allstarlink.exe

# --- Rules ---
all: $(TARGET)

$(TARGET): check-dependencies $(_OBJS) $(BDIR) 
	@echo "Starting build..."
	$(CC) $(_OBJS) -o $@ $(LIBS)

$(ODIR)/%.o: $(SDIR)/%.c | $(ODIR)
	$(CC) -c -o $@ $< $(CFLAGS)

$(BDIR) $(ODIR):
	@echo "Creating directory $@..."
	mkdir -p $@

check-dependencies:
	@echo "Checking for required dependencies..."
	@if ! dpkg -s libcjson-dev >/dev/null 2>&1; then \
		echo "Installing libcjson-dev..."; \
		sudo apt update; \
		if ! sudo apt install -y libcjson-dev; then \
			echo "WARNING: Failed to install libcjson-dev!"; \
		fi \
	fi
	@if ! dpkg -s wiringpi >/dev/null 2>&1; then \
		echo "Installing wiringpi..."; \
		if ! sudo apt install -y wiringpi; then \
			echo "WARNING: Failed to install wiringpi!"; \
		fi \
	fi
	@echo "All dependencies are installed."

# Removes the compiled files so you can start fresh
clean:
	rm -rf $(ODIR) $(BDIR)

.PHONY: all clean check-dependencies   