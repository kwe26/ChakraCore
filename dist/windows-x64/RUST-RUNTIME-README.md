# chakra_runtime

Cross-platform Rust host runtime for ChakraCore.

## Goals

- Run JavaScript files using ChakraCore on Windows, Linux, and macOS.
- Expose a host `print(...)` function.
- Expose a host `console` object with `log`, `info`, `warn`, `error`, `debug`, and `dir`.
- Install `require("chakra:...")` via `JsInstallChakraSystemRequire` when available.
- Apply ES2021 logical-assignment entry-source transform via `JsChakraEs2021Transform` when available.
- Provide a colored interactive REPL with syntax highlighting.

## Build

```bash
cargo build --release --manifest-path rust/runtime/Cargo.toml
```

## Usage

```bash
# auto-discover ChakraCore shared library
cargo run --manifest-path rust/runtime/Cargo.toml -- path/to/script.js

# explicit ChakraCore shared library path
cargo run --manifest-path rust/runtime/Cargo.toml -- --chakra-lib /path/to/libChakraCore.so path/to/script.js

# interactive REPL
cargo run --manifest-path rust/runtime/Cargo.toml -- --repl
```

Windows shared library name: `ChakraCore.dll`

Linux shared library name: `libChakraCore.so`

macOS shared library name: `libChakraCore.dylib`

You can also set `CHAKRA_CORE_PATH` to a specific shared library path.

The REPL uses a colored prompt, simple JavaScript syntax highlighting, and the same host console/print bindings as script execution.
