# --- Directories ---
INCLUDE_DIR = include
SOURCE_DIR = src
OBJECT_DIR = obj
BUILD_DIR = /usr/bin
SERVICE_DIR = systemd
CONFIG_DIR = configs

TARGET_CONFIG_DIR = /etc/asl-interface
TARGET_SERVICE_DIR = /etc/systemd/system

# --- Files ---
CONFIG_FILES = $(wildcard $(CONFIG_DIR)/*.json)
SOURCE_FILES = $(wildcard $(SOURCE_DIR)/*.c)
OBJECT_FILES = $(patsubst $(SOURCE_DIR)/%.c, $(OBJECT_DIR)/%.o, $(SOURCE_FILES))
TARGET_FILE = $(BUILD_DIR)/asl-interface

# --- Compiler Settings ---
CC = gcc
CFLAGS = -I$(INCLUDE_DIR) -Wall -Wextra -g
LIBS = -lwiringPi -lcjson

# --- Rules ---
all: check-dependencies $(OBJECT_FILES) stop-service cp-service cp-configs $(TARGET_FILE) run-service

debug: check-dependencies $(OBJECT_FILES) stop-service cp-configs $(TARGET_FILE)
	@echo "Debug build complete. You can run the program with: sudo $(TARGET_FILE)"

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
	@if ! dpkg -s libjsmn-dev >/dev/null 2>&1; then \
		echo "Installing libjsmn-dev..."; \
		sudo apt update; \
		if ! sudo apt install -y libjsmn-dev; then \
			echo "WARNING: Failed to install libjsmn-dev!"; \
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

stop-service:
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
	fi; \

cp-service:
	@echo "Confirming difference in service file..."
	@if ! cmp -s $(SERVICE_DIR)/asl-interface.service $(TARGET_SERVICE_DIR)/asl-interface.service; then \
		echo "Updating service file..."; \
		sudo cp $(SERVICE_DIR)/asl-interface.service $(TARGET_SERVICE_DIR)/; \
	else \
		echo "Service file is up to date. No changes made."; \
	fi 

cp-configs:
	@echo "Ensuring target config directory exists..."
	@if [ ! -d "$(TARGET_CONFIG_DIR)" ]; then \
		echo "Creating target config directory at $(TARGET_CONFIG_DIR)..."; \
		sudo mkdir -p $(TARGET_CONFIG_DIR); \
	fi
	@echo "checking config files for differences..."
	@for config in $(CONFIG_FILES); do \
		target_config=$(TARGET_CONFIG_DIR)/$$(basename $$config); \
		if [ ! -f "$$target_config" ] || ! cmp -s "$$config" "$$target_config"; then \
			echo "Updating config file: $$(basename $$config)"; \
			sudo cp "$$config" "$$target_config"; \
		else \
			echo "Config file $$(basename $$config) is up to date. No changes made."; \
		fi; \
	done

run-service:
	@echo "Starting service..."
	sudo systemctl enable asl-interface.service
	sudo systemctl daemon-reload
	sudo systemctl start asl-interface.service

clean:
	rm -rf $(OBJECT_DIR)

.PHONY: all clean debug check-dependencies stop-service cp-service run-service cp-configs