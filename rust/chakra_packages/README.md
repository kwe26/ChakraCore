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
- `chakra_es2020_analyze` used by `require("chakra:es2020").analyze(source)`
- `chakra_es2021_analyze` used by `require("chakra:es2021").analyze(source)`
- `chakra_es2021_transform` used by host runtime fallback for ES2021 logical assignment syntax

## Source layout

The crate is split by module under `src/`:

- `src/common/` shared FFI and error helpers
- `src/es/` FFI exports for ES feature analyzers
- `src/info/` implementation for `chakra:info`
- `src/fs/` implementation for `chakra:fs`
- `src/reqwest/` implementation for `chakra:reqwest`

Versioned parser implementations live outside this crate:

- `../es2020/` ES2020 syntax feature analysis with SWC
- `../es2021/` ES2021 syntax feature analysis with SWC
