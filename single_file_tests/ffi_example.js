// ChakraCore FFI Example
// Shows how to use the ffi global object to load libraries and call functions

// Example 1: Load kernel32.dll and get a function pointer
if (typeof ffi !== 'undefined') {
    console.log("FFI is available!");
    
    // Load a library (platform-specific)
    // On Windows:
    let lib = ffi.dlopen("kernel32.dll");
    console.log("  Loaded library handle:", lib.handle);
    let func = ffi.dlsym(lib.handle, "GetCurrentProcessId");
    console.log("  Resolved function pointer:", func.ptr);
    let result = ffi.call(func.ptr, []);
    console.log("  GetCurrentProcessId() =>", result);
    ffi.close(lib.handle);
    console.log("  Closed library handle");
    
    // On Linux:
    // let lib = ffi.dlopen("libc.so.6");
    // let func = ffi.dlsym(lib.handle, "getpid");
    // let result = ffi.call(func.ptr, []);
    // ffi.close(lib.handle);
} else {
    console.log("FFI is not available in this context");
}
