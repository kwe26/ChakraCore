# ChakraCore FFI Implementation - Summary

## What Was Implemented

Added **Foreign Function Interface (FFI)** support powered by Rust, making native functions callable from JavaScript through the global `ffi` object.

### Key Features

✅ **Rust-Powered FFI Core** - High-performance dynamic library loading via `libloading` crate
✅ **Seamless JavaScript Integration** - Global `ffi` object with standard methods
✅ **Auto-Installation** - FFI automatically available on any new JavaScript context (like `require`)
✅ **Cross-Platform** - Works on Windows, Linux, macOS
✅ **Both Embedded & Runtime** - Available to ChakraCore.dll embedders AND the Rust runtime host

### JavaScript API

The `ffi` global object provides:

```javascript
// Load native library
ffi.dlopen(path: string)              → { handle: number }

// Get function pointer by name
ffi.dlsym(handle: number, symbol: string) → { ptr: number }

// Call native function (up to 8 u64 arguments)
ffi.call(ptr: number, args?: number[])    → number

// Unload library
ffi.close(handle: number)              → number (1 = success, 0 = failure)
```

### Example

```javascript
// Windows - Call GetCurrentProcessId from kernel32.dll
let kernel32 = ffi.dlopen("kernel32.dll");
let func = ffi.dlsym(kernel32.handle, "GetCurrentProcessId");
let processId = ffi.call(func.ptr, []);  // No args
ffi.close(kernel32.handle);
console.log("Process ID:", processId);
```

## Files Created

1. **rust/ffiimpl/** - Rust FFI library
   - `Cargo.toml` - Package manifest (depends on libloading)
   - `src/lib.rs` - FFI implementation (~250 lines)
     - `chakra_ffi_dlopen()` - Load library
     - `chakra_ffi_dlsym()` - Get function pointer
     - `chakra_ffi_call()` - Call function
     - `chakra_ffi_dlclose()` - Unload library
     - Thread-safe global handle registry

2. **lib/Jsrt/ChakraFfi.h** - Internal C FFI interface
   - Function signatures for C interop
   - Handle struct definitions

3. **lib/Jsrt/JsrtFfi.h** - Public JSRT FFI API
   - `JsInstallFfi()` - Install FFI on JavaScript context
   - Complete documentation and usage examples

4. **single_file_tests/ffi_example.js** - Usage example

## Files Modified

1. **lib/Jsrt/JsrtChakraExtensions.cpp** (~280 lines added)
   - `#include "ChakraFfi.h"` - Link to Rust FFI
   - FFI callback implementations:
     - `FfiDlopenCallback` (~35 lines)
     - `FfiDlsymCallback` (~40 lines)
     - `FfiCallCallback` (~60 lines)
     - `FfiCloseCallback` (~25 lines)
   - `JsInstallFfi()` - Public API to install ffi object
   - `JsEnsureFfiIfMissing()` - Helper for auto-installation

2. **lib/Jsrt/Jsrt.cpp** (~3 lines)
   - Added `JsEnsureFfiIfMissing()` declaration
   - Hooked FFI auto-install in `JsSetCurrentContext()` (after require installation)

## How It Works

1. When a new JavaScript context is created via `JsSetCurrentContext()`:
   - The runtime checks if `require` exists (installs if missing)
   - The runtime checks if `ffi` exists (installs if missing)

2. When `ffi.dlopen("libname")` is called:
   - JavaScript callback → JSRT extension → Rust FFI library
   - Library loaded and stored in global handle registry
   - Handle returned as `{ handle: number }`

3. When `ffi.call(ptr, args)` is called:
   - Arguments collected from JS array
   - Rust FFI interprets args as u64 values
   - Calls native function via transmuted function pointer
   - Returns result as JavaScript number

## Building

The implementation uses:
- Rust `libloading` crate for dynamic library loading
- Standard JSRT C++ API for JavaScript integration

To build, ensure:
1. Rust toolchain is installed
2. The `rust/ffiimpl` Cargo.toml dependencies resolve
3. C++ compiler can see ChakraFfi.h include path

## Testing

Try the example:

```bash
cd d:\Projects\ChakraCore
# With Rust runtime:
cargo run --release -p chakra-runtime -- single_file_tests/ffi_example.js
```

Or embed in your own JSRT application:

```cpp
#include "JsrtFfi.h"

// After JsSetCurrentContext(context):
JsValueRef ffiObject = nullptr;
JsInstallFfi(&ffiObject);

// Now JavaScript can call ffi.dlopen(), etc.
```

## Limitations & Future Work

### Current Limitations
- Basic x64 calling convention (8 u64 args max)
- No automatic type marshaling (caller casts values)
- Symbol names must be exact (case-sensitive, platform-specific mangling)

### Possible Enhancements
1. Different calling conventions (cdecl, stdcall, fastcall)
2. Struct and array marshaling
3. Callback support (C → JS function calls)
4. Type metadata system
5. Error strings and detailed diagnostics
6. Per-context handle isolation

## Integration Notes

- FFI works alongside `require()` - both auto-installed
- No breaking changes to existing JSRT API
- Backward compatible with ChakraCore releases
- Rust FFI module is self-contained in `rust/ffiimpl/`
