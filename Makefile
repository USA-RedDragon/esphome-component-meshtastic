.PHONY: gen proto-gen proto-clean clean format lint
PROTO_ROOT=./meshtastic-protobufs
PROTO_DIR=$(PROTO_ROOT)/meshtastic
PROTO_OUT=./components/meshtastic

# Hand-written sources only (generated *.pb.* / enum_names.* / _enums.py are excluded).
CPP_FILES=$(shell find components/meshtastic \( -name '*.cpp' -o -name '*.h' \) | grep -vE '\.pb\.|enum_names')
PY_FILES=components/meshtastic/__init__.py components/meshtastic/sensor/__init__.py \
         components/meshtastic/text_sensor/__init__.py components/meshtastic/packet_transport/__init__.py \
         scripts/gen_enums.py

# Transitive imports reachable from mesh.proto + mqtt.proto + deviceonly.proto.
PROTO_NAMES=atak channel config device_ui deviceonly localonly mesh module_config mqtt portnums remote_hardware telemetry xmodem

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
	@echo "Cleaning generated enums..."
	@rm -f components/meshtastic/_enums.py components/meshtastic/enum_names.h components/meshtastic/enum_names.cpp
	@echo "Generated enums cleaned"

format:
	clang-format -i $(CPP_FILES)
	ruff format $(PY_FILES)
	ruff check --fix $(PY_FILES)

lint:
	clang-format --dry-run --Werror $(CPP_FILES)
	ruff format --check $(PY_FILES)
	ruff check $(PY_FILES)
