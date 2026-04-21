// Chakra HTTP Server Implementation - Rust
// Provides a simple HTTP server package for JavaScript via Rust

use std::collections::{HashMap, HashSet};
use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Mutex, OnceLock};
use std::thread::{self, JoinHandle};
use std::time::Duration;
use std::sync::mpsc::{self, Sender, Receiver};

// ─── C FFI Types ──────────────────────────────────────────────────────────

/// Opaque handle to a server instance
#[repr(transparent)]
pub struct HttpServerHandle {
    pub id: u64,
}

impl HttpServerHandle {
    pub fn null() -> Self {
        HttpServerHandle { id: 0 }
    }

    pub fn is_null(&self) -> bool {
        self.id == 0
    }
}

// ─── HTTP Request & Response ──────────────────────────────────────────────

pub struct IncomingRequest {
    pub id: u64,
    pub method: String,
    pub path: String,
    pub body: String,
    pub response_sender: Sender<OutgoingResponse>,
}

pub struct OutgoingResponse {
    pub status: u16,
    pub content_type: String,
    pub body: String,
}

// ─── Global Server Registry ───────────────────────────────────────────────

static SERVER_REGISTRY: OnceLock<Mutex<HashMap<u64, ServerInstance>>> = OnceLock::new();
static SERVER_COUNTER: AtomicU64 = AtomicU64::new(1);
static REQUEST_COUNTER: AtomicU64 = AtomicU64::new(1);

struct ServerInstance {
    port: u16,
    host: String,
    routes: std::sync::Arc<std::sync::Mutex<HashSet<String>>>,
    thread_count: usize,
    started: bool,
    stop_signal: std::sync::Arc<std::sync::atomic::AtomicBool>,
    worker: Option<JoinHandle<()>>,
    request_receiver: std::sync::Arc<std::sync::Mutex<Receiver<IncomingRequest>>>,
    request_sender: Sender<IncomingRequest>,
}

fn get_registry() -> &'static Mutex<HashMap<u64, ServerInstance>> {
    SERVER_REGISTRY.get_or_init(|| Mutex::new(HashMap::new()))
}

fn normalize_route_key(route_key: &str) -> String {
    if let Some(separator_index) = route_key.find(':') {
        let method = route_key[..separator_index].trim().to_ascii_uppercase();
        let path = route_key[(separator_index + 1)..].trim();
        format!("{method}:{path}")
    } else {
        route_key.trim().to_string()
    }
}

fn normalize_request_key(method: &str, path: &str) -> String {
    let normalized_path = path.split('?').next().unwrap_or(path);
    format!("{}:{}", method.trim().to_ascii_uppercase(), normalized_path)
}

fn write_http_response(stream: &mut TcpStream, response: &OutgoingResponse) {
    let response_str = format!(
        "HTTP/1.1 {} OK\r\nContent-Type: {}\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}",
        response.status,
        response.content_type,
        response.body.len(),
        response.body
    );
    let _ = stream.write_all(response_str.as_bytes());
    let _ = stream.flush();
}

fn write_error_response(stream: &mut TcpStream, status_code: u16, reason_phrase: &str, body: &str) {
    let response = format!(
        "HTTP/1.1 {} {}\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}",
        status_code,
        reason_phrase,
        body.len(),
        body
    );
    let _ = stream.write_all(response.as_bytes());
    let _ = stream.flush();
}

fn handle_http_connection(
    mut stream: TcpStream, 
    routes: &std::sync::Arc<std::sync::Mutex<HashSet<String>>>,
    request_sender: &Sender<IncomingRequest>
) {
    let _ = stream.set_read_timeout(Some(Duration::from_millis(500)));

    let mut buffer = [0u8; 8192];
    let bytes_read = match stream.read(&mut buffer) {
        Ok(size) if size > 0 => size,
        _ => return,
    };

    let request_text = match std::str::from_utf8(&buffer[..bytes_read]) {
        Ok(text) => text,
        Err(_) => {
            write_error_response(&mut stream, 400, "Bad Request", "Invalid HTTP request encoding");
            return;
        }
    };

    let mut lines = request_text.lines();
    let request_line = lines.next().unwrap_or_default();
    let mut parts = request_line.split_whitespace();
    let method = parts.next().unwrap_or_default().to_string();
    let path = parts.next().unwrap_or_default().to_string();

    if method.is_empty() || path.is_empty() {
        write_error_response(&mut stream, 400, "Bad Request", "Malformed HTTP request line");
        return;
    }

    // Read headers until empty line
    for line in &mut lines {
        if line.trim().is_empty() {
            break;
        }
    }
    
    // Remaining is body
    let body = lines.collect::<Vec<_>>().join("\n");

    let request_key = normalize_request_key(&method, &path);
    
    // Check if route is registered
    if !routes.lock().unwrap().contains(&request_key) {
        write_error_response(&mut stream, 404, "Not Found", "No registered route for this request");
        return;
    }

    let (response_tx, response_rx) = mpsc::channel();
    let req_id = REQUEST_COUNTER.fetch_add(1, Ordering::Relaxed);

    let req = IncomingRequest {
        id: req_id,
        method,
        path,
        body,
        response_sender: response_tx,
    };

    if request_sender.send(req).is_ok() {
        // Wait for JS to process the response
        if let Ok(response) = response_rx.recv() {
            write_http_response(&mut stream, &response);
        } else {
            write_error_response(&mut stream, 500, "Internal Server Error", "JavaScript thread panicked or dropped response handle");
        }
    } else {
        write_error_response(&mut stream, 503, "Service Unavailable", "Server is shutting down");
    }
}

// ─── HTTP Server Functions ────────────────────────────────────────────────

/// Create a single-threaded HTTP server.
/// Returns server handle or null on failure.
#[no_mangle]
pub unsafe extern "C" fn chakra_http_serve_single(
    port: u16,
    host: *const u8,
    host_len: usize,
) -> HttpServerHandle {
    if host.is_null() || host_len == 0 {
        return HttpServerHandle::null();
    }

    let host_slice = std::slice::from_raw_parts(host, host_len);
    let host_str = match std::str::from_utf8(host_slice) {
        Ok(s) => s.to_string(),
        Err(_) => return HttpServerHandle::null(),
    };

    let mut registry = get_registry().lock().unwrap();
    let server_id = SERVER_COUNTER.fetch_add(1, Ordering::Relaxed);
    let (tx, rx) = mpsc::channel();

    registry.insert(
        server_id,
        ServerInstance {
            port,
            host: host_str,
            routes: std::sync::Arc::new(std::sync::Mutex::new(HashSet::new())),
            thread_count: 1,
            started: false,
            stop_signal: std::sync::Arc::new(std::sync::atomic::AtomicBool::new(false)),
            worker: None,
            request_receiver: std::sync::Arc::new(std::sync::Mutex::new(rx)),
            request_sender: tx,
        },
    );

    HttpServerHandle { id: server_id }
}

/// Create a multi-threaded HTTP server.
/// Returns server handle or null on failure.
#[no_mangle]
pub unsafe extern "C" fn chakra_http_serve_multi(
    port: u16,
    host: *const u8,
    host_len: usize,
    thread_count: u32,
) -> HttpServerHandle {
    if host.is_null() || host_len == 0 || thread_count == 0 {
        return HttpServerHandle::null();
    }

    let host_slice = std::slice::from_raw_parts(host, host_len);
    let host_str = match std::str::from_utf8(host_slice) {
        Ok(s) => s.to_string(),
        Err(_) => return HttpServerHandle::null(),
    };

    let mut registry = get_registry().lock().unwrap();
    let server_id = SERVER_COUNTER.fetch_add(1, Ordering::Relaxed);
    let (tx, rx) = mpsc::channel();

    registry.insert(
        server_id,
        ServerInstance {
            port,
            host: host_str,
            routes: std::sync::Arc::new(std::sync::Mutex::new(HashSet::new())),
            thread_count: thread_count as usize,
            started: false,
            stop_signal: std::sync::Arc::new(std::sync::atomic::AtomicBool::new(false)),
            worker: None,
            request_receiver: std::sync::Arc::new(std::sync::Mutex::new(rx)),
            request_sender: tx,
        },
    );

    HttpServerHandle { id: server_id }
}

/// Register a route handler for a specific method and path.
/// route_key: "GET:/" or "POST:/api/users" etc.
/// Returns 1 on success, 0 on failure.
#[no_mangle]
pub unsafe extern "C" fn chakra_http_on_route(
    server: HttpServerHandle,
    route_key: *const u8,
    route_key_len: usize,
) -> i32 {
    if server.is_null() || route_key.is_null() || route_key_len == 0 {
        return 0;
    }

    let route_slice = std::slice::from_raw_parts(route_key, route_key_len);
    let route_str = match std::str::from_utf8(route_slice) {
        Ok(s) => s.to_string(),
        Err(_) => return 0,
    };

    let mut registry = get_registry().lock().unwrap();
    if let Some(server_instance) = registry.get_mut(&server.id) {
        let normalized = normalize_route_key(&route_str);
        server_instance.routes.lock().unwrap().insert(normalized);
        1
    } else {
        0
    }
}

/// Start the HTTP server.
/// Returns 1 on success, 0 on failure.
#[no_mangle]
pub unsafe extern "C" fn chakra_http_start(server: HttpServerHandle) -> i32 {
    if server.is_null() {
        return 0;
    }

    let mut registry = get_registry().lock().unwrap();
    if let Some(srv) = registry.get_mut(&server.id) {
        if srv.started {
            return 1;
        }

        let addr = format!("{}:{}", srv.host, srv.port);
        let listener = match TcpListener::bind(&addr) {
            Ok(l) => l,
            Err(_) => return 0,
        };

        let _ = listener.set_nonblocking(true);
        let routes = srv.routes.clone();
        let stop_signal = srv.stop_signal.clone();
        let sender = srv.request_sender.clone();

        let handle = thread::spawn(move || {
            loop {
                if stop_signal.load(Ordering::Relaxed) {
                    break;
                }
                match listener.accept() {
                    Ok((stream, _)) => {
                        handle_http_connection(stream, &routes, &sender);
                    }
                    Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                        thread::sleep(Duration::from_millis(50));
                    }
                    Err(_) => {
                        thread::sleep(Duration::from_millis(50));
                    }
                }
            }
        });

        srv.worker = Some(handle);
        srv.started = true;
        1
    } else {
        0
    }
}

/// Stop the HTTP server.
/// Returns 1 on success, 0 on failure.
#[no_mangle]
pub unsafe extern "C" fn chakra_http_stop(server: HttpServerHandle) -> i32 {
    if server.is_null() {
        return 0;
    }

    let mut registry = get_registry().lock().unwrap();
    if let Some(mut srv) = registry.remove(&server.id) {
        srv.stop_signal.store(true, Ordering::Relaxed);
        if let Some(worker) = srv.worker.take() {
            let _ = worker.join();
        }
        1
    } else {
        0
    }
}

/// Get server configuration (port, host, threads).
/// Caller must free returned string via chakra_http_free_string.
#[no_mangle]
pub unsafe extern "C" fn chakra_http_get_config(server: HttpServerHandle) -> *mut u8 {
    if server.is_null() {
        return std::ptr::null_mut();
    }

    let registry = get_registry().lock().unwrap();
    if let Some(srv) = registry.get(&server.id) {
        let config = format!(
            r#"{{"port":{},"host":"{}","threads":{}}}"#,
            srv.port, srv.host, srv.thread_count
        );
        let bytes = config.into_bytes();
        let len = bytes.len();
        let ptr = unsafe { libc::malloc(len) as *mut u8 };
        if !ptr.is_null() {
            unsafe { std::ptr::copy_nonoverlapping(bytes.as_ptr(), ptr, len) };
        }
        ptr
    } else {
        std::ptr::null_mut()
    }
}

/// Free a string returned by HTTP server functions.
#[no_mangle]
pub unsafe extern "C" fn chakra_http_free_string(ptr: *mut u8) {
    if !ptr.is_null() {
        unsafe { libc::free(ptr as *mut libc::c_void) };
    }
}

/// Cleanup all HTTP servers.
#[no_mangle]
pub unsafe extern "C" fn chakra_http_cleanup() {
    let mut registry = get_registry().lock().unwrap();
    registry.clear();
}

// ─── Event Loop Extensions ───────────────────────────────────────────────

static RESPONSE_SENDERS: OnceLock<Mutex<HashMap<u64, Sender<OutgoingResponse>>>> = OnceLock::new();

fn get_senders() -> &'static Mutex<HashMap<u64, Sender<OutgoingResponse>>> {
    RESPONSE_SENDERS.get_or_init(|| Mutex::new(HashMap::new()))
}

/// Accept the next request from the queue. Blocks if none are available.
/// Returns a JSON string containing `req_id`, `method`, `path`, `body`, or NULL if stopped/error.
/// The caller must free the string using `chakra_http_free_string`.
#[no_mangle]
pub unsafe extern "C" fn chakra_http_accept(server: HttpServerHandle) -> *mut u8 {
    if server.is_null() {
        return std::ptr::null_mut();
    }

    let req_result = {
        let registry = get_registry().lock().unwrap();
        if let Some(srv) = registry.get(&server.id) {
            srv.request_receiver.lock().unwrap().recv()
        } else {
            return std::ptr::null_mut();
        }
    };

    if let Ok(req) = req_result {
        get_senders().lock().unwrap().insert(req.id, req.response_sender);
        
        let json = format!(
            r#"{{"id":{},"method":"{}","path":"{}","body":"{}"}}"#,
            req.id, 
            req.method.replace('"', "\\\""), 
            req.path.replace('"', "\\\""), 
            req.body.replace('"', "\\\"").replace('\n', "\\n")
        );
        let bytes = json.into_bytes();
        let len = bytes.len();
        let ptr = libc::malloc(len + 1) as *mut u8;
        if !ptr.is_null() {
            std::ptr::copy_nonoverlapping(bytes.as_ptr(), ptr, len);
            *ptr.add(len) = 0;
        }
        ptr
    } else {
        std::ptr::null_mut() // Channel disconnected (server stopped)
    }
}

/// Send a response for a specific request ID.
#[no_mangle]
pub unsafe extern "C" fn chakra_http_respond(
    server: HttpServerHandle,
    req_id: u64,
    status: u16,
    content_type: *const u8,
    content_type_len: usize,
    body: *const u8,
    body_len: usize,
) -> i32 {
    if server.is_null() {
        return 0;
    }

    let ct_str = if content_type.is_null() || content_type_len == 0 {
        "text/plain".to_string()
    } else {
        let slice = std::slice::from_raw_parts(content_type, content_type_len);
        std::str::from_utf8(slice).unwrap_or("text/plain").to_string()
    };

    let body_str = if body.is_null() || body_len == 0 {
        "".to_string()
    } else {
        let slice = std::slice::from_raw_parts(body, body_len);
        std::str::from_utf8(slice).unwrap_or("").to_string()
    };

    if let Some(sender) = get_senders().lock().unwrap().remove(&req_id) {
        let response = OutgoingResponse {
            status,
            content_type: ct_str,
            body: body_str,
        };
        if sender.send(response).is_ok() {
            return 1;
        }
    }
    0
}