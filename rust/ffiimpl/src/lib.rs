// Chakra FFI Implementation - Rust
// Provides foreign function interface capabilities for JavaScript via Rust

use std::collections::HashMap;
use std::ffi::CStr;
use std::ffi::CString;
use std::mem;
use std::ptr;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Mutex, OnceLock};

use libloading::Library;

// Simple global registry for loaded libraries
// In production, this would use thread-local or per-context storage
static FFI_HANDLE_REGISTRY: OnceLock<Mutex<HashMap<u64, Box<Library>>>> = OnceLock::new();
static HANDLE_COUNTER: AtomicU64 = AtomicU64::new(1);

fn get_registry() -> &'static Mutex<HashMap<u64, Box<Library>>> {
    FFI_HANDLE_REGISTRY.get_or_init(|| Mutex::new(HashMap::new()))
}

// ─── FFI Handle Type ──────────────────────────────────────────────────────

/// Opaque handle to a loaded library
#[repr(transparent)]
pub struct FfiHandle(pub u64);

impl FfiHandle {
    pub fn null() -> Self {
        FfiHandle(0)
    }

    pub fn is_null(&self) -> bool {
        self.0 == 0
    }
}

// ─── Error Types ──────────────────────────────────────────────────────────

#[repr(i32)]
#[derive(Debug, Clone, Copy)]
pub enum FfiError {
    Success = 0,
    InvalidHandle = 1,
    InvalidSymbol = 2,
    LibraryNotFound = 3,
    SymbolNotFound = 4,
    InvalidArgument = 5,
    AlreadyLoaded = 6,
    UnknownError = 255,
}

impl From<libloading::Error> for FfiError {
    fn from(err: libloading::Error) -> Self {
        match err {
            libloading::Error::DlOpen { .. } => FfiError::LibraryNotFound,
            libloading::Error::DlSym { .. } => FfiError::SymbolNotFound,
            libloading::Error::DlOpenUnknown => FfiError::LibraryNotFound,
            libloading::Error::DlSymUnknown => FfiError::SymbolNotFound,
            _ => FfiError::UnknownError,
        }
    }
}

// ─── C FFI Interface ──────────────────────────────────────────────────────

/// Load a dynamic library and return a handle.
/// Returns 0 on failure, non-zero handle on success.
#[no_mangle]
pub unsafe extern "C" fn chakra_ffi_dlopen(path: *const u8, path_len: usize) -> FfiHandle {
    if path.is_null() || path_len <= 0 {
        return FfiHandle::null();
    }

    let path_slice = std::slice::from_raw_parts(path, path_len);
    let path_str = match std::str::from_utf8(path_slice) {
        Ok(s) => s,
        Err(_) => return FfiHandle::null(),
    };

    if let Ok(lib) = Library::new(path_str) {
        let mut registry = get_registry().lock().unwrap();
        let handle = FfiHandle(HANDLE_COUNTER.fetch_add(1, Ordering::Relaxed));
        registry.insert(handle.0, Box::new(lib));
        handle
    } else {
        FfiHandle::null()
    }
}

/// Get a function pointer from a loaded library.
/// Returns null if handle or symbol not found.
#[no_mangle]
pub unsafe extern "C" fn chakra_ffi_dlsym(handle: FfiHandle, symbol: *const u8, symbol_len: usize) -> *mut std::ffi::c_void {
    if handle.is_null() || symbol.is_null() || symbol_len == 0 {
        return ptr::null_mut();
    }

    let registry = get_registry().lock().unwrap();
    let lib = match registry.get(&handle.0) {
        Some(lib) => lib,
        None => return ptr::null_mut(),
    };

    let symbol_slice = std::slice::from_raw_parts(symbol, symbol_len);
    let symbol_bytes = if symbol_slice.ends_with(&[0]) {
        symbol_slice
    } else {
        // Need to handle non-null-terminated symbol names
        return ptr::null_mut();
    };

    let symbol_name = match CStr::from_bytes_with_nul(symbol_bytes) {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };

    match unsafe { lib.get::<*mut std::ffi::c_void>(symbol_name.to_bytes_with_nul()) } {
        Ok(sym) => *sym,
        Err(_) => ptr::null_mut(),
    }
}

/// Close a loaded library handle.
/// Returns 1 on success, 0 on failure.
#[no_mangle]
pub unsafe extern "C" fn chakra_ffi_dlclose(handle: FfiHandle) -> i32 {
    if handle.is_null() {
        return 0;
    }

    let mut registry = get_registry().lock().unwrap();
    if registry.remove(&handle.0).is_some() {
        1
    } else {
        0
    }
}

/// Call a function with up to 8 arguments (all as u64/pointers).
/// This is a basic calling convention; extended calling for complex types can be added.
/// Returns result as u64 (caller interprets).
#[no_mangle]
pub unsafe extern "C" fn chakra_ffi_call(
    func_ptr: *mut std::ffi::c_void,
    argc: u32,
    argv: *const u64,
) -> u64 {
    if func_ptr.is_null() || argc > 8 {
        return 0;
    }

    let args = if argc > 0 && !argv.is_null() {
        std::slice::from_raw_parts(argv, argc as usize)
    } else {
        &[]
    };

    // This is a simplified approach: call function with up to 8 u64 arguments.
    // For more complex calling conventions, we'd need additional metadata.
    type RawFn = unsafe extern "C" fn(u64, u64, u64, u64, u64, u64, u64, u64) -> u64;

    let fn_ptr: RawFn = mem::transmute(func_ptr);

    let a0 = args.get(0).copied().unwrap_or(0);
    let a1 = args.get(1).copied().unwrap_or(0);
    let a2 = args.get(2).copied().unwrap_or(0);
    let a3 = args.get(3).copied().unwrap_or(0);
    let a4 = args.get(4).copied().unwrap_or(0);
    let a5 = args.get(5).copied().unwrap_or(0);
    let a6 = args.get(6).copied().unwrap_or(0);
    let a7 = args.get(7).copied().unwrap_or(0);

    fn_ptr(a0, a1, a2, a3, a4, a5, a6, a7)
}

/// Get the last error message from the FFI subsystem.
/// Caller is responsible for freeing the returned string via chakra_ffi_free_string.
#[no_mangle]
pub unsafe extern "C" fn chakra_ffi_get_last_error() -> *mut u8 {
    // Placeholder: in a real implementation, store last error per thread
    let msg = "FFI operation failed";
    let cstr = CString::new(msg).unwrap();
    let bytes = cstr.into_bytes_with_nul();
    let ptr = libc::malloc(bytes.len()) as *mut u8;
    if !ptr.is_null() {
        std::ptr::copy_nonoverlapping(bytes.as_ptr(), ptr, bytes.len());
    }
    ptr
}

/// Free a string returned by FFI functions.
#[no_mangle]
pub unsafe extern "C" fn chakra_ffi_free_string(ptr: *mut u8) {
    if !ptr.is_null() {
        libc::free(ptr as *mut libc::c_void);
    }
}

/// Free all loaded libraries (cleanup).
#[no_mangle]
pub unsafe extern "C" fn chakra_ffi_cleanup() {
    let mut registry = get_registry().lock().unwrap();
    registry.clear();
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_ffi_handle_creation() {
        let h = FfiHandle(42);
        assert!(!h.is_null());
        assert_eq!(h.0, 42);

        let null_h = FfiHandle::null();
        assert!(null_h.is_null());
    }
}
