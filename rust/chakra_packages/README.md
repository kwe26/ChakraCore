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
3. relative repo path from `ch.exe`: `../../../../rust/chakra_packages/target/release/`
4. legacy relative repo path from `ch.exe`: `../../../../bin/ch/rust/chakra_packages/target/release/`
5. default dynamic loader lookup path

Export currently available:

- `chakra_info_version` used by `require("chakra:info").version()`
- `chakra_fs_read_file_utf8` used by `require("chakra:fs").readFileSync(path)`
- `chakra_fs_write_file_utf8` used by `require("chakra:fs").writeFileSync(path, content)`
- `chakra_fs_exists` used by `require("chakra:fs").existsSync(path)`
- `chakra_reqwest_get_text` used by `require("chakra:reqwest").get(url)`
- `chakra_reqwest_post_text` used by `require("chakra:reqwest").post(url, body)`
- `chakra_reqwest_fetch_text` used by `require("chakra:reqwest").fetch(method, url, body?)`
- `chakra_reqwest_download_fetch_parallel` used by `require("chakra:reqwest").downloadFetch(url, outputPath, parallelParts?)`

## Source layout

The crate is split by module under `src/`:

- `src/common/` shared FFI and error helpers
- `src/info/` implementation for `chakra:info`
- `src/fs/` implementation for `chakra:fs`
- `src/reqwest/` implementation for `chakra:reqwest`
