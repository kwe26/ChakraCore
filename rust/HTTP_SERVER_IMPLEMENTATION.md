# ChakraCore HTTP Server Implementation

## Overview

Added **high-performance HTTP server support** powered by Rust (using Hyper), available through global `cHttp` (single-threaded) and `cHttpK` (multi-threaded) objects in JavaScript.

## Features

✅ **Single-threaded HTTP Server** - `cHttp.serve(port, host)`
✅ **Multi-threaded HTTP Server** - `cHttpK.serve(port, host, threads)`
✅ **Route Registration** - Register handlers for HTTP methods and paths
✅ **Request Parsing** - Headers, body, and multipart form data automatically parsed in Rust
✅ **Response Helpers** - JSON, file serving, plain text responses
✅ **File Uploads** - Full multipart/form-data support with temporary file handling
✅ **Async-Friendly** - Built on Tokio async runtime
✅ **Cross-Platform** - Windows, Linux, macOS support

## JavaScript API

### Single-Threaded Server: `cHttp`

```javascript
// Create server
let httpServer = cHttp.serve(3000, "0.0.0.0");

// Register route handler
httpServer.on('get', '/', (req, res) => {
    // req.headers - parsed HTTP headers
    // req.method - HTTP method
    // req.path - request path
    // req.body - request body for POST/PUT
    
    // Response methods:
    res.json({ key: "value" });        // Send JSON
    res.send("Plain text response");   // Send text
    res.sendFile("./path/to/file");    // Send file
    res.end();                         // End response
});

// Shutdown
httpServer.end();
```

### Multi-Threaded Server: `cHttpK`

```javascript
// Create server with 4 worker threads
let httpcServer = cHttpK.serve(3001, "0.0.0.0", 4);

// Same route registration as cHttp
httpcServer.on('get', '/', (req, res) => {
    res.json({ message: "Thread-safe response" });
});

httpcServer.end();
```

## Architecture

### Components

1. **Rust HTTP Server** (`rust/httpserver/`)
   - Single & multi-threaded implementations using Hyper
   - Request parsing and header extraction
   - Multipart form-data handling for file uploads
   - Response serialization (JSON, file, text)

2. **JSRT Extension** (`lib/Jsrt/JsrtChakraExtensions.cpp`)
   - JavaScript callbacks for serve, on, end methods
   - Route handler registration and dispatch
   - Request/response object wrapping

3. **C FFI Interface** (`lib/Jsrt/ChakraHttpServer.h`)
   - Low-level HTTP server C API
   - Handle management for server instances
   - Configuration queries

4. **Public JSRT API** (`lib/Jsrt/JsrtHttpServer.h`)
   - `JsInstallHttpServer()` - Install cHttp
   - `JsInstallHttpServerMulti()` - Install cHttpK

### Auto-Installation

HTTP servers are automatically installed when a JavaScript context is created:
- `JsSetCurrentContext()` triggers `JsEnsureHttpServerIfMissing()`
- Both `cHttp` and `cHttpK` are installed automatically
- No manual setup required for any JSRT host

## Usage Examples

### Example 1: Simple GET Handler

```javascript
let server = cHttp.serve(8080, "127.0.0.1");

server.on('get', '/', (req, res) => {
    res.json({
        status: "ok",
        timestamp: Date.now()
    });
});
```

### Example 2: POST with JSON Body

```javascript
server.on('post', '/api/users', (req, res) => {
    // Request body is automatically parsed if JSON
    let userData = JSON.parse(req.body);
    
    res.json({
        message: "User created",
        id: 123,
        name: userData.name
    });
});
```

### Example 3: File Upload

```javascript
server.on('post', '/upload', (req, res) => {
    // Multipart form data is parsed in Rust
    // File saved to temporary location
    // Accessible via req.files[0].path
    
    res.json({
        message: "File uploaded",
        filename: req.files[0].filename,
        size: req.files[0].size
    });
});
```

### Example 4: Serving Static Files

```javascript
server.on('get', '/static/:filename', (req, res) => {
    let filepath = "./public/" + req.params.filename;
    res.sendFile(filepath);
});
```

### Example 5: Multi-Threaded Processing

```javascript
let multiServer = cHttpK.serve(3001, "0.0.0.0", 8);

multiServer.on('post', '/process', (req, res) => {
    // Processing is done in 8 worker threads
    // Automatic load balancing across threads
    
    let result = heavyProcessing(JSON.parse(req.body));
    res.json({ result: result });
});
```

## Request Object Properties

| Property | Type | Description |
|----------|------|-------------|
| `method` | string | HTTP method (GET, POST, PUT, DELETE, etc.) |
| `path` | string | Request path (e.g., "/api/users") |
| `headers` | object | Parsed HTTP headers (lowercase keys) |
| `body` | string/buffer | Request body content |
| `contentLength` | number | Content-Length header value |
| `contentType` | string | Content-Type header value |
| `files` | array | Uploaded files (multipart requests) |
| `params` | object | Path parameters (if using :paramName syntax) |
| `query` | object | Query string parameters |

## Response Object Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `json` | `res.json(object)` | Send JSON response with Content-Type: application/json |
| `send` | `res.send(string)` | Send plain text response |
| `sendFile` | `res.sendFile(path)` | Send file from disk |
| `setHeader` | `res.setHeader(name, value)` | Set response header |
| `status` | `res.status(code)` | Set HTTP status code (default 200) |
| `end` | `res.end()` | End response (optional, auto-called) |

## Files Created

1. **rust/httpserver/**
   - `Cargo.toml` - Package manifest
   - `src/lib.rs` - Hyper-based HTTP server implementation

2. **lib/Jsrt/ChakraHttpServer.h** - Internal C FFI interface

3. **lib/Jsrt/JsrtHttpServer.h** - Public JSRT API

4. **single_file_tests/http_server_example.js** - Usage examples

## Files Modified

1. **lib/Jsrt/JsrtChakraExtensions.cpp**
   - Added HTTP server callbacks (~200 lines)
   - Added `JsInstallHttpServer()` and `JsInstallHttpServerMulti()`
   - Added `JsEnsureHttpServerIfMissing()` helper

2. **lib/Jsrt/Jsrt.cpp**
   - Added auto-install hook in `JsSetCurrentContext()`

## Building

### Dependencies

- Rust `tokio` - Async runtime
- Rust `hyper` - HTTP protocol
- Rust `mime` - MIME type handling
- Rust `multipart` - Form data parsing
- Rust `tempfile` - Temporary file management

### Compilation

```bash
cd rust/httpserver
cargo build --release
```

## Performance Considerations

### Single-Threaded (cHttp)
- **Best for**: Simple APIs, prototyping, low-concurrency scenarios
- **Latency**: Low
- **Throughput**: Limited to single core
- **Memory**: Minimal overhead

### Multi-Threaded (cHttpK)
- **Best for**: Production APIs, high concurrency, CPU-bound processing
- **Latency**: Slightly higher due to thread coordination
- **Throughput**: Scales with thread count
- **Memory**: Higher per-thread overhead

## Thread Count Recommendations

For `cHttpK.serve(port, host, threads)`:
- `1-2 threads`: Lightweight, low-latency scenarios
- `4-8 threads`: General purpose servers
- `8-16 threads`: High-concurrency APIs
- `> 16 threads`: Consider system CPU count (usually not beneficial)

## Error Handling

Errors are reported via exceptions thrown in JavaScript:

```javascript
try {
    let server = cHttp.serve(80, "0.0.0.0");
} catch (e) {
    console.error("Failed to create server:", e);
}

server.on('get', '/', (req, res) => {
    try {
        res.json({ data: processData(req.body) });
    } catch (e) {
        res.status(500).json({ error: "Internal Server Error" });
    }
});
```

## Limitations & Future Work

### Current Limitations
- HTTP/1.1 only (HTTP/2 not yet supported)
- No SSL/TLS (HTTPS not yet supported)
- No streaming responses (full buffering)
- Path parameters only support `:name` pattern

### Possible Enhancements
1. HTTP/2 support via hyper
2. HTTPS/TLS via rustls
3. Response streaming for large files
4. Middleware system
5. Built-in logging and metrics
6. Cookie handling
7. CORS support
8. Compression (gzip, brotli)

## Integration Notes

- HTTP servers work alongside `require` and `ffi` - all auto-installed
- No breaking changes to existing JSRT API
- Fully backward compatible
- Rust HTTP server module is self-contained in `rust/httpserver/`
- Server instances are managed per-context

## Testing

Run the example:

```bash
ch single_file_tests/http_server_example.js
```

Or embed in your JSRT application:

```cpp
#include "JsrtHttpServer.h"

// After JsSetCurrentContext(context):
JsValueRef cHttp = nullptr;
JsInstallHttpServer(&cHttp);

JsValueRef cHttpK = nullptr;
JsInstallHttpServerMulti(&cHttpK);

// Now JavaScript can call cHttp.serve() and cHttpK.serve()
```

## Architecture Diagram

```
JavaScript Code (User)
        ↓
   cHttp.serve() / cHttpK.serve()
        ↓
JSRT Extension Callbacks (C++)
        ↓
Rust HTTP Server (tokio + hyper)
        ↓
Network I/O
```

## Platform Support

- ✅ Windows
- ✅ Linux
- ✅ macOS

All platforms fully supported via Rust standard library and cross-platform crates.
