// Create a server listening on port 3000
let httpServer = cHttp.serve(3000, "0.0.0.0");
console.log("cHttp server started on http://0.0.0.0:3000");
console.log("cHttp internal handle:", httpServer._handle);

// Register GET handler for root path
httpServer.on('get', '/', (req, res) => {
    console.log("GET / - request received");
    console.log("  Headers:", Object.keys(req));

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

httpServer.listen();
