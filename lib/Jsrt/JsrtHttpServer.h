// Copyright (C) ChakraCore Project Contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.

/// ChakraCore HTTP Server JSRT API
/// Provides high-performance HTTP server capabilities via Rust

#ifndef CHAKRA_JSRT_HTTP_SERVER_H
#define CHAKRA_JSRT_HTTP_SERVER_H

#include "ChakraCommon.h"

/// Install the cHttp (single-threaded) HTTP server on global scope.
/// Returns JsNoError on success.
CHAKRA_API JsInstallHttpServer(_Out_opt_ JsValueRef* serverObject);

/// Install the cHttpK (multi-threaded) HTTP server on global scope.
/// Returns JsNoError on success.
CHAKRA_API JsInstallHttpServerMulti(_Out_opt_ JsValueRef* serverObject);

#endif // CHAKRA_JSRT_HTTP_SERVER_H
