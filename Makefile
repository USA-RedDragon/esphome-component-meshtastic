.PHONY: gen proto-gen proto-clean clean
PROTO_ROOT=./meshtastic-protobufs
PROTO_DIR=$(PROTO_ROOT)/meshtastic
PROTO_OUT=./components/meshtastic

# Transitive imports reachable from mesh.proto + mqtt.proto + deviceonly.proto.
PROTO_NAMES=atak channel config device_ui deviceonly localonly mesh module_config mqtt portnums telemetry xmodem

PROTO_FILES=$(addprefix $(PROTO_DIR)/,$(addsuffix .proto,$(PROTO_NAMES)))

clean: proto-clean gen-clean

proto-gen:
	@echo "Generating protobuf files..."
	@mkdir -p $(PROTO_OUT)
	@if [ ! -f $(PROTO_DIR)/mesh.proto ]; then \
		echo "Error: meshtastic-protobufs not found. Please run: git submodule update --init --recursive"; \
		exit 1; \
	fi
	@command -v protoc >/dev/null 2>&1 || { echo "protoc not found. Please install Protocol Buffers compiler."; exit 1; }
	@command -v protoc-gen-nanopb >/dev/null 2>&1 || { echo "protoc-gen-nanopb not found. Please install nanopb generator."; exit 1; }
	protoc \
		--proto_path=$(PROTO_ROOT) \
		--nanopb_opt=-I$(PROTO_ROOT) \
		--nanopb_opt=-Q'#include "../%s"' \
		--nanopb_out=$(PROTO_OUT) \
		$(PROTO_FILES)
	mv -f $(PROTO_OUT)/meshtastic/*.pb.c $(PROTO_OUT)/meshtastic/*.pb.h $(PROTO_OUT)/
	rmdir $(PROTO_OUT)/meshtastic
	@for h in $(PROTO_OUT)/*.pb.h; do \
		if grep -q '#include <vector>' "$$h" && [ -f "$${h%.h}.c" ]; then \
			mv "$${h%.h}.c" "$${h%.h}.cpp"; \
		fi; \
	done
	@echo "Protobuf generation complete"

proto-clean:
	@echo "Cleaning generated protobuf files..."
	@rm -f $(PROTO_OUT)/*.pb.c $(PROTO_OUT)/*.pb.h $(PROTO_OUT)/*.pb.cpp
	@echo "Protobuf files cleaned"

gen: proto-gen
	@echo "Generating YAML enums..."
	python3 scripts/gen_enums.py --proto-root $(PROTO_ROOT) --out components/meshtastic/_enums.py

gen-clean:
	@echo "Cleaning generated YAML enums..."
	@rm -f components/meshtastic/_enums.py
	@echo "YAML enums cleaned"

