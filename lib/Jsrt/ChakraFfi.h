// ChakraCore FFI Header
// C interface for FFI functions implemented in Rust

#ifndef CHAKRA_FFI_H
#define CHAKRA_FFI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ─── FFI Handle ───────────────────────────────────────────────────────────

typedef struct {
    uint64_t handle;
} ChakraFfiHandle;

static inline int chakra_ffi_handle_is_null(ChakraFfiHandle h) {
    return h.handle == 0;
}

// ─── FFI Functions ────────────────────────────────────────────────────────

/// Load a dynamic library by path.
ChakraFfiHandle chakra_ffi_dlopen(const uint8_t* path, size_t path_len);

/// Get a function pointer from a loaded library.
void* chakra_ffi_dlsym(ChakraFfiHandle handle, const uint8_t* symbol, size_t symbol_len);

/// Close a loaded library handle.
int chakra_ffi_dlclose(ChakraFfiHandle handle);

/// Call a function with arguments (up to 8 u64 args).
uint64_t chakra_ffi_call(void* func_ptr, uint32_t argc, const uint64_t* argv);

/// Get the last error message.
uint8_t* chakra_ffi_get_last_error(void);

/// Free error string.
void chakra_ffi_free_string(uint8_t* ptr);

/// Cleanup all FFI resources.
void chakra_ffi_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // CHAKRA_FFI_H
