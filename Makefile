PORT ?= 8000
WASM_DIR := build/wasm

.PHONY: wasm configure-wasm serve run clean-wasm

# Build the Emscripten target. Requires `source $(EMSDK)/emsdk_env.sh`
# (or the equivalent) so emcc is on PATH and EMSDK is set.
wasm: configure-wasm
	cmake --build --preset wasm

# Re-configure only if the build directory hasn't been initialized yet;
# otherwise CMake auto-rebuilds when CMakeLists.txt changes.
configure-wasm:
	@[ -f $(WASM_DIR)/build.ninja ] || cmake --preset wasm

# Serve the built artifacts over plain HTTP. Use PORT=NNNN to override.
serve:
	cd $(WASM_DIR) && python3 -m http.server $(PORT) --bind 127.0.0.1

# Convenience: build then serve.
run: wasm
	@echo "Open http://127.0.0.1:$(PORT)/vtl_live.html"
	@$(MAKE) serve

clean-wasm:
	rm -rf $(WASM_DIR)
