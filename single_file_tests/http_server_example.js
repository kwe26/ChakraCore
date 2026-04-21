// ChakraCore HTTP Server Examples
// Shows how to use cHttp (single-threaded) and cHttpK (multi-threaded)

console.log("=== HTTP Server Examples ===\n");

// Example 1: Single-threaded HTTP server
if (typeof cHttp !== 'undefined') {
    console.log("cHttp (single-threaded server) is available!");
    
    // Create a server listening on port 3000
    let httpServer = cHttp.serve(3000, "0.0.0.0");
    console.log("cHttp server started on http://0.0.0.0:3000");
    console.log("cHttp internal handle:", httpServer._handle);
    
    // Register GET handler for root path
    httpServer.on('get', '/', (req, res) => {
        console.log("GET / - request received");
        console.log("  Headers:", req);
        
        // Send JSON response
        res.json({
            message: "Hello from ChakraCore HTTP Server!",
            method: "GET",
            path: "/"
        });
    });
    
    // Register POST handler for /api/data
    httpServer.on('post', '/api/data', (req, res) => {
        console.log("POST /api/data - request received");
        
        // Send plain text response
        res.send("Data received successfully");
    });
    
    // Register file serving
    httpServer.on('get', '/file/:name', (req, res) => {
        console.log("GET /file/:name - request received");
        
        // Send a file
        // res.sendFile("./path/to/file.txt");
        
        // For now, just send acknowledgment
        res.send("File endpoint");
    });
    
    // Note: Call end() to gracefully shutdown the server
    // httpServer.end();
    
} else {
    console.log("cHttp is not available in this context");
}

console.log();

// Example 2: Multi-threaded HTTP server
if (typeof cHttpK !== 'undefined') {
    console.log("cHttpK (multi-threaded server) is available!");
    
    // Create a server with 4 threads listening on port 3001
    let httpcServer = cHttpK.serve(3001, "0.0.0.0", 4);
    console.log("cHttpK server started on http://0.0.0.0:3001");
    console.log("cHttpK internal handle:", httpcServer._handle);
    
    // Register handlers
    httpcServer.on('get', '/', (req, res) => {
        console.log("cHttpK GET / - request received (thread-safe)");
        
        res.json({
            message: "Multi-threaded server response",
            threads: 4
        });
    });
    
    // Handle file uploads
    httpcServer.on('post', '/upload', (req, res) => {
        console.log("cHttpK POST /upload - file upload request");
        
        // File handling is done in Rust
        // Multipart form data is automatically parsed
        res.json({
            message: "File uploaded successfully",
            size: req.contentLength
        });
    });
    
    // Note: Call end() to gracefully shutdown the server
    // httpcServer.end();
    
} else {
    console.log("cHttpK is not available in this context");
}

console.log("\n=== Server Configuration ===");
console.log("Single-threaded server (cHttp) examples");
console.log("  - GET / : Returns JSON response");
console.log("  - POST /api/data : Accepts and echoes data");
console.log("  - GET /file/:name : Serves files");
console.log();
console.log("Multi-threaded server (cHttpK) examples");
console.log("  - 4 worker threads for concurrent request handling");
console.log("  - GET / : Thread-safe JSON response");
console.log("  - POST /upload : File upload with multipart parsing");
console.log();
console.log("Response object methods:");
console.log("  - res.json(object) : Send JSON response");
console.log("  - res.send(string) : Send plain text response");
console.log("  - res.sendFile(path) : Send file from disk");
console.log("  - res.end() : End response");
console.log();
console.log("Request object properties:");
console.log("  - req.method : HTTP method (GET, POST, etc.)");
console.log("  - req.path : Request path");
console.log("  - req.headers : Object with HTTP headers");
console.log("  - req.body : Request body (for POST/PUT)");
console.log("  - req.contentLength : Content length in bytes");
console.log();
console.log("Optional keep-alive:");
console.log("  Run with a numeric argument to keep this script alive for manual testing.");
console.log("  Example: ch http_server_example.js 30000");

// Keep alive only when explicitly requested so automated test runs stay fast.
let keepAliveMs = 0;
if (typeof WScript !== 'undefined' && typeof WScript.Arguments !== 'undefined') {
    try {
        if (typeof WScript.Arguments.length === 'number' && WScript.Arguments.length > 0) {
            keepAliveMs = parseInt(WScript.Arguments[0], 10) || 0;
        } else if (typeof WScript.Arguments.Count === 'function' && WScript.Arguments.Count() > 0) {
            keepAliveMs = parseInt(WScript.Arguments.Item(0), 10) || 0;
        }
    } catch (e) {
        keepAliveMs = 0;
    }
}

if (keepAliveMs > 0 && typeof WScript !== 'undefined' && typeof WScript.Sleep === 'function') {
    console.log("Keeping process alive for " + keepAliveMs + "ms...");
    WScript.Sleep(keepAliveMs);
}
