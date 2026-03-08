.PHONY: gen proto-gen proto-clean
PROTO_ROOT=./meshtastic-protobufs
PROTO_DIR=$(PROTO_ROOT)/meshtastic
PROTO_OUT=./components/meshtastic/proto
PROTO_FILES=$(wildcard $(PROTO_DIR)/*.proto)

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
	@echo "Protobuf generation complete"

gen: proto-gen
	@echo "Generating YAML enums..."
	python3 scripts/gen_enums.py --proto-root $(PROTO_ROOT) --out components/meshtastic/_enums.py

proto-clean:
	@echo "Cleaning generated protobuf files..."
	@rm -rf $(PROTO_OUT)/meshtastic
	@echo "Protobuf files cleaned"
