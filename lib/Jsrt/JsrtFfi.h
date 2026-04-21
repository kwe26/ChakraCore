// Copyright (C) ChakraCore Project Contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.

/// ChakraCore FFI (Foreign Function Interface) JSRT API
/// Provides low-level FFI capabilities to JavaScript code via the ffi global object

#ifndef CHAKRA_JSRT_FFI_H
#define CHAKRA_JSRT_FFI_H

#include "ChakraCommon.h"

typedef JsValueRef (*JsInstallFfiFn)(_Out_opt_ JsValueRef* ffiObject);

/// Install the ffi global object in the current JavaScript context.
///
/// The ffi object provides methods for dynamic library loading and function calling:
/// - ffi.dlopen(path: string): { handle: number } - Load a dynamic library
/// - ffi.dlsym(handle: number, symbol: string): { ptr: number } - Get function pointer
/// - ffi.call(ptr: number, args?: number[]): number - Call function with up to 8 u64 arguments
/// - ffi.close(handle: number): number - Close a loaded library
///
/// Example:
///   let lib = ffi.dlopen("kernel32.dll");
///   let func = ffi.dlsym(lib.handle, "GetProcAddress");
///   let result = ffi.call(func.ptr, [arg1, arg2]);
///   ffi.close(lib.handle);
///
/// Parameters:
///   ffiObject: Optional output for the installed ffi object reference.
///
/// Returns:
///   JsNoError if successful, otherwise a JS error code.
///
CHAKRA_API JsInstallFfi(_Out_opt_ JsValueRef* ffiObject);

#endif // CHAKRA_JSRT_FFI_H
