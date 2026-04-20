# chakra_packages

Rust-backed system packages for `ch.exe`.

## Build

From this directory:

```bash
cargo build --release
```

The host will try to load this library in the following order:

1. `CHAKRA_RUST_PACKAGES_PATH` (file path or directory)
2. next to `ch.exe`
3. relative repo path from `ch.exe`: `../../../../bin/ch/rust/chakra_packages/target/release/`
4. default dynamic loader lookup path

Export currently available:

- `chakra_info_version` used by `require("chakra:info").version()`
