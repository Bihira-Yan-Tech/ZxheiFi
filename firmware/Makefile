# Makefile for building zxheifi_firmware.bin
#
# Requires arduino-cli (https://arduino.github.io/arduino-cli/) with the
# esp8266 core and ArduinoJson installed:
#   arduino-cli core install esp8266:esp8266
#   arduino-cli lib install "ArduinoJson"

PROJECT_NAME := zxheifi_firmware
SKETCH_DIR   := $(CURDIR)
BUILD_DIR    := $(SKETCH_DIR)/build
BOARD        := esp8266:esp8266:nodemcuv2
MAX_BYTES    := 900000

.PHONY: all compile clean

all: compile

compile:
	@echo "=== Building $(PROJECT_NAME) for $(BOARD) ==="
	arduino-cli compile \
	  --fqbn $(BOARD) \
	  --libraries "ArduinoJson" \
	  --build-property "build.extra_flags=-D CORE_DEBUG_LEVEL=0" \
	  --output-dir "$(BUILD_DIR)" \
	  "$(SKETCH_DIR)"
	@BIN=$$(find "$(BUILD_DIR)" -maxdepth 1 -name '*.bin' | head -n1); \
	if [ -z "$$BIN" ]; then echo "ERROR: no .bin produced"; exit 1; fi; \
	cp "$$BIN" "$(SKETCH_DIR)/$(PROJECT_NAME).bin"; \
	SIZE=$$(stat -c%s "$(SKETCH_DIR)/$(PROJECT_NAME).bin" 2>/dev/null || stat -f%z "$(SKETCH_DIR)/$(PROJECT_NAME).bin"); \
	echo "=== Built $(PROJECT_NAME).bin ($$SIZE bytes) ==="; \
	if [ $$SIZE -gt $(MAX_BYTES) ]; then echo "ERROR: exceeds $(MAX_BYTES) byte limit!"; exit 1; fi; \
	echo "OK: within size limit"

clean:
	@rm -rf "$(BUILD_DIR)" "$(SKETCH_DIR)/$(PROJECT_NAME).bin"
	@echo "Cleaned build artifacts"
