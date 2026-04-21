// ChakraCore FFI Example
// Shows how to use the ffi global object to load libraries and call functions

// Example 1: Load kernel32.dll and get a function pointer
if (typeof ffi !== 'undefined') {
    console.log("FFI is available!");
    
    // Load a library (platform-specific)
    // On Windows:
    let lib = ffi.dlopen("kernel32.dll");
    console.log("  Loaded library handle:", lib.handle);
    let getCurrentProcessId = ffi.func(lib.handle, "GetCurrentProcessId");
    let result = getCurrentProcessId();
    console.log("  GetCurrentProcessId() =>", result);

    if (typeof ffi.type === 'function') {
        // Typed variant: optional signature with args/returns descriptors.
        let t = ffi.types || {};
        let int32 = t.i32 || ffi.type("i32");
        let cstring = t.cstring || ffi.type(String);
        let getCurrentProcessIdTyped = ffi.func(lib.handle, "GetCurrentProcessId", {
            args: [],
            returns: int32
        });
        console.log("  Typed GetCurrentProcessId() =>", getCurrentProcessIdTyped());

        // Struct-style descriptor (field unpacking into positional ABI args).
        let SomeStruct = ffi.type({
            field1: "i32",
            field2: "i32*"
        });
        console.log("  Example struct type descriptor created:", SomeStruct);
        console.log("  Example string type descriptor created:", cstring);
    } else {
        console.log("  Typed API not available in this runtime binary yet (missing ffi.type).");
    }

    ffi.close(lib.handle);
    console.log("  Closed library handle");
    
    // On Linux:
    // let lib = ffi.dlopen("libc.so.6");
    // let getpid = ffi.func(lib.handle, "getpid");
    // let result = getpid();
    // ffi.close(lib.handle);
} else {
    console.log("FFI is not available in this context");
}
