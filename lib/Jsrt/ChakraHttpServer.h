// ChakraCore HTTP Server Header
// C interface for HTTP server functions implemented in Rust

#ifndef CHAKRA_HTTP_SERVER_H
#define CHAKRA_HTTP_SERVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ─── HTTP Server Handle ───────────────────────────────────────────────────

typedef struct {
    uint64_t id;
} ChakraHttpServerHandle;

static inline int chakra_http_server_handle_is_null(ChakraHttpServerHandle h) {
    return h.id == 0;
}

// ─── HTTP Server Functions ────────────────────────────────────────────────

/// Create a single-threaded HTTP server.
/// Returns handle on success, null handle on failure.
ChakraHttpServerHandle chakra_http_serve_single(uint16_t port, const uint8_t* host, size_t host_len);

/// Create a multi-threaded HTTP server with specified thread count.
/// Returns handle on success, null handle on failure.
ChakraHttpServerHandle chakra_http_serve_multi(uint16_t port, const uint8_t* host, size_t host_len, uint32_t thread_count);

/// Register a route handler.
/// Returns 1 on success, 0 on failure.
int chakra_http_on_route(ChakraHttpServerHandle server, const uint8_t* route_key, size_t route_key_len);

/// Start the HTTP server.
/// Returns 1 on success, 0 on failure.
int chakra_http_start(ChakraHttpServerHandle server);

/// Stop the HTTP server.
/// Returns 1 on success, 0 on failure.
int chakra_http_stop(ChakraHttpServerHandle server);

/// Get server configuration as JSON string.
/// Caller must free via chakra_http_free_string.
uint8_t* chakra_http_get_config(ChakraHttpServerHandle server);

/// Accept a request from the queue (blocking).
/// Returns a JSON string with request details, or null if server stopped.
/// Caller must free via chakra_http_free_string.
uint8_t* chakra_http_accept(ChakraHttpServerHandle server);

/// Send an HTTP response for a specific request ID.
/// Returns 1 on success, 0 on failure.
int chakra_http_respond(
    ChakraHttpServerHandle server,
    uint64_t req_id,
    uint16_t status,
    const uint8_t* content_type,
    size_t content_type_len,
    const uint8_t* body,
    size_t body_len
);

/// Free a string returned by HTTP server functions.
void chakra_http_free_string(uint8_t* ptr);

/// Cleanup all HTTP server resources.
void chakra_http_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // CHAKRA_HTTP_SERVER_H
