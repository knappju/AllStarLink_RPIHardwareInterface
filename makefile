# --- Directories ---
INCLUDE_DIR = include
SOURCE_DIR = src
OBJECT_DIR = obj
BUILD_DIR = /usr/bin
SERVICE_DIR = systemd

TARGET_SERVICE_DIR = /etc/systemd/system

# --- Files ---
SOURCE_FILES = $(wildcard $(SOURCE_DIR)/*.c)
OBJECT_FILES = $(patsubst $(SOURCE_DIR)/%.c, $(OBJECT_DIR)/%.o, $(SOURCE_FILES))
TARGET_FILE = $(BUILD_DIR)/asl-interface

# --- Compiler Settings ---
CC = gcc
CFLAGS = -I$(INCLUDE_DIR) -Wall -Wextra -g
LIBS = -lwiringPi -lcjson

# --- Rules ---
all: check-dependencies $(OBJECT_FILES) stop-and-update-service $(TARGET_FILE) run-service

$(TARGET_FILE): $(OBJECT_FILES)
	$(CC) $(OBJECT_FILES) -o $@ $(LIBS)

# Used to generate each of the object files.
$(OBJECT_DIR)/%.o: $(SOURCE_DIR)/%.c | $(OBJECT_DIR)
	@echo "Compiling $<..."
	$(CC) -c -o $@ $< $(CFLAGS)

# Create the object directory if it doesn't exist.
$(OBJECT_DIR):
	mkdir -p $@

# Check for required dependencies and attempt to install them if missing.
check-dependencies:
	@echo "Checking for required dependencies..."
	@if ! dpkg -s libcjson-dev >/dev/null 2>&1; then \
		echo "Installing libcjson-dev..."; \
		sudo apt update; \
		if ! sudo apt install -y libcjson-dev; then \
			echo "WARNING: Failed to install libcjson-dev!"; \
			exit 1; \
		fi \
	fi
	@if ! dpkg -s wiringpi >/dev/null 2>&1; then \
		echo "Installing wiringpi..."; \
		if ! sudo apt install -y wiringpi; then \
			echo "WARNING: Failed to install wiringpi!"; \
			exit 1; \
		fi \
	fi
	@echo "All dependencies are installed..."

# checks if the service exists, stops it if it does, and then updates or creates the service file as needed.
stop-and-update-service:
	@echo "Checking service status..."
	@if systemctl status asl-interface.service >/dev/null 2>&1; then \
		echo "Service exists. Stopping..."; \
		sudo systemctl stop asl-interface.service; \
		if systemctl is-active --quiet asl-interface.service; then \
			echo "Failed to stop service!"; \
			exit 1; \
		else \
			echo "Service stopped successfully."; \
		fi \
	else \
		echo "Service not running..."; \
	fi; \
	if ! cmp -s $(SERVICE_DIR)/asl-interface.service $(TARGET_SERVICE_DIR)/asl-interface.service; then \
		echo "Updating service file..."; \
		sudo cp $(SERVICE_DIR)/asl-interface.service $(TARGET_SERVICE_DIR)/; \
	else \
		echo "Service file is up to date. No changes made."; \
	fi 

run-service:
	@echo "Starting service..."
	sudo systemctl enable asl-interface.service
	sudo systemctl daemon-reload
	sudo systemctl start asl-interface.service

clean:
	rm -rf $(OBJECT_DIR)

.PHONY: all clean check-dependencies stop-and-update-service run-service