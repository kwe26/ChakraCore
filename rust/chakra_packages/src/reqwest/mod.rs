use crate::common::{error, ffi};
use reqwest::blocking::Client;
use reqwest::header::{ACCEPT_RANGES, CONTENT_LENGTH, RANGE};
use reqwest::Method;
use std::fs;
use std::io::Write;
use std::os::raw::c_char;
use std::path::Path;
use std::sync::OnceLock;
use std::thread;
use std::time::Duration;

static HTTP_CLIENT: OnceLock<Result<Client, String>> = OnceLock::new();

fn get_http_client() -> Result<&'static Client, String> {
    match HTTP_CLIENT.get_or_init(|| {
        Client::builder()
            .user_agent("chakra:reqwest/0.2")
            .connect_timeout(Duration::from_secs(10))
            .timeout(Duration::from_secs(30))
            .pool_max_idle_per_host(16)
            .pool_idle_timeout(Duration::from_secs(90))
            .tcp_nodelay(true)
            .build()
            .map_err(|build_error| format!("failed to initialize HTTP client: {}", build_error))
    }) {
        Ok(client) => Ok(client),
        Err(error_message) => Err(error_message.clone()),
    }
}

fn parse_http_method(method: &str) -> Result<Method, String> {
    Method::from_bytes(method.as_bytes())
        .map_err(|parse_error| format!("invalid HTTP method '{}': {}", method, parse_error))
}

fn request_text(method: &str, url: &str, body: Option<&str>) -> Result<String, String> {
    let client = get_http_client()?;
    let parsed_method = parse_http_method(method)?;

    let mut request_builder = client.request(parsed_method, url);
    if let Some(body_text) = body {
        request_builder = request_builder.body(body_text.to_owned());
    }

    let response = request_builder
        .send()
        .map_err(|request_error| format!("request failed for '{}': {}", url, request_error))?;

    if !response.status().is_success() {
        return Err(format!(
            "request failed for '{}': HTTP {}",
            url,
            response.status().as_u16()
        ));
    }

    response
        .text()
        .map_err(|read_error| format!("failed to read response body for '{}': {}", url, read_error))
}

fn ensure_parent_directory(output_path: &str) -> Result<(), String> {
    let path = Path::new(output_path);
    if let Some(parent) = path.parent() {
        if !parent.as_os_str().is_empty() {
            fs::create_dir_all(parent).map_err(|create_error| {
                format!(
                    "failed to create parent directory for '{}': {}",
                    output_path, create_error
                )
            })?;
        }
    }

    Ok(())
}

fn download_single(client: &Client, url: &str, output_path: &str) -> Result<(), String> {
    let response = client
        .get(url)
        .send()
        .map_err(|request_error| format!("download request failed for '{}': {}", url, request_error))?;

    if !response.status().is_success() {
        return Err(format!(
            "download request failed for '{}': HTTP {}",
            url,
            response.status().as_u16()
        ));
    }

    let bytes = response
        .bytes()
        .map_err(|read_error| format!("failed to read download response for '{}': {}", url, read_error))?;

    ensure_parent_directory(output_path)?;
    fs::write(output_path, &bytes).map_err(|write_error| {
        format!(
            "failed to write download output to '{}': {}",
            output_path, write_error
        )
    })
}

fn discover_parallel_content_length(client: &Client, url: &str) -> Option<u64> {
    let response = client.head(url).send().ok()?;
    if !response.status().is_success() {
        return None;
    }

    let supports_ranges = response
        .headers()
        .get(ACCEPT_RANGES)
        .and_then(|value| value.to_str().ok())
        .map(|value| value.to_ascii_lowercase().contains("bytes"))
        .unwrap_or(false);

    if !supports_ranges {
        return None;
    }

    response
        .headers()
        .get(CONTENT_LENGTH)
        .and_then(|value| value.to_str().ok())
        .and_then(|value| value.parse::<u64>().ok())
        .filter(|length| *length > 0)
}

fn normalize_part_count(requested_part_count: i32, content_length: u64) -> usize {
    if content_length == 0 {
        return 1;
    }

    let requested = if requested_part_count > 0 {
        requested_part_count as usize
    } else {
        4
    };

    let mut part_count = requested.max(1).min(32);
    if content_length < part_count as u64 {
        part_count = content_length as usize;
    }

    part_count.max(1)
}

fn download_parallel(
    client: &Client,
    url: &str,
    output_path: &str,
    content_length: u64,
    requested_part_count: i32,
) -> Result<(), String> {
    let part_count = normalize_part_count(requested_part_count, content_length);
    if part_count <= 1 {
        return download_single(client, url, output_path);
    }

    let base_chunk_size = content_length / part_count as u64;
    let remainder = content_length % part_count as u64;

    let mut workers = Vec::with_capacity(part_count);
    let mut start = 0u64;
    for index in 0..part_count {
        let extra = if index < remainder as usize { 1 } else { 0 };
        let end = start + base_chunk_size + extra - 1;

        let request_url = url.to_owned();
        let request_client = client.clone();
        let range_start = start;
        let range_end = end;

        workers.push(thread::spawn(move || -> Result<(usize, Vec<u8>), String> {
            let range_header = format!("bytes={}-{}", range_start, range_end);
            let response = request_client
                .get(&request_url)
                .header(RANGE, range_header.as_str())
                .send()
                .map_err(|request_error| {
                    format!(
                        "range download failed for '{}', {}: {}",
                        request_url, range_header, request_error
                    )
                })?;

            if response.status().as_u16() != 206 {
                return Err(format!(
                    "server did not honor range request '{}': HTTP {}",
                    range_header,
                    response.status().as_u16()
                ));
            }

            let bytes = response.bytes().map_err(|read_error| {
                format!(
                    "failed to read range response '{}': {}",
                    range_header, read_error
                )
            })?;

            let expected_length = range_end - range_start + 1;
            if bytes.len() as u64 != expected_length {
                return Err(format!(
                    "range '{}' size mismatch: expected {}, got {}",
                    range_header,
                    expected_length,
                    bytes.len()
                ));
            }

            Ok((index, bytes.to_vec()))
        }));

        start = end + 1;
    }

    let mut chunks = vec![Vec::<u8>::new(); part_count];
    for worker in workers {
        let joined = worker
            .join()
            .map_err(|_| "parallel download worker panicked".to_owned())?;
        let (index, bytes) = joined?;
        chunks[index] = bytes;
    }

    ensure_parent_directory(output_path)?;
    let mut output_file = fs::File::create(output_path).map_err(|create_error| {
        format!(
            "failed to create download output '{}': {}",
            output_path, create_error
        )
    })?;

    for chunk in chunks {
        output_file.write_all(&chunk).map_err(|write_error| {
            format!(
                "failed writing download output '{}': {}",
                output_path, write_error
            )
        })?;
    }

    output_file.flush().map_err(|flush_error| {
        format!(
            "failed to flush download output '{}': {}",
            output_path, flush_error
        )
    })
}

fn download_fetch_parallel(url: &str, output_path: &str, requested_part_count: i32) -> Result<(), String> {
    let client = get_http_client()?;

    if let Some(content_length) = discover_parallel_content_length(client, url) {
        let parallel_result = download_parallel(client, url, output_path, content_length, requested_part_count);
        if parallel_result.is_ok() {
            return Ok(());
        }
    }

    download_single(client, url, output_path)
}

#[no_mangle]
pub extern "C" fn chakra_reqwest_get_text(url: *const c_char) -> *mut c_char {
    error::clear_last_error();

    let parsed_url = match ffi::c_str_to_string(url, "url") {
        Ok(value) => value,
        Err(_) => return std::ptr::null_mut(),
    };

    match request_text("GET", &parsed_url, None) {
        Ok(body_text) => ffi::to_owned_c_string_ptr(body_text),
        Err(request_error) => {
            error::set_last_error(&format!("reqwest.get failed for '{}': {}", parsed_url, request_error));
            std::ptr::null_mut()
        }
    }
}

#[no_mangle]
pub extern "C" fn chakra_reqwest_post_text(url: *const c_char, body: *const c_char) -> *mut c_char {
    error::clear_last_error();

    let parsed_url = match ffi::c_str_to_string(url, "url") {
        Ok(value) => value,
        Err(_) => return std::ptr::null_mut(),
    };

    let parsed_body = match ffi::c_str_to_string(body, "body") {
        Ok(value) => value,
        Err(_) => return std::ptr::null_mut(),
    };

    match request_text("POST", &parsed_url, Some(&parsed_body)) {
        Ok(body_text) => ffi::to_owned_c_string_ptr(body_text),
        Err(request_error) => {
            error::set_last_error(&format!("reqwest.post failed for '{}': {}", parsed_url, request_error));
            std::ptr::null_mut()
        }
    }
}

#[no_mangle]
pub extern "C" fn chakra_reqwest_fetch_text(
    method: *const c_char,
    url: *const c_char,
    body: *const c_char,
) -> *mut c_char {
    error::clear_last_error();

    let parsed_method = match ffi::c_str_to_string(method, "method") {
        Ok(value) => value,
        Err(_) => return std::ptr::null_mut(),
    };

    let parsed_url = match ffi::c_str_to_string(url, "url") {
        Ok(value) => value,
        Err(_) => return std::ptr::null_mut(),
    };

    let parsed_body = if body.is_null() {
        None
    } else {
        match ffi::c_str_to_string(body, "body") {
            Ok(value) => Some(value),
            Err(_) => return std::ptr::null_mut(),
        }
    };

    match request_text(&parsed_method, &parsed_url, parsed_body.as_deref()) {
        Ok(body_text) => ffi::to_owned_c_string_ptr(body_text),
        Err(request_error) => {
            error::set_last_error(&format!(
                "reqwest.fetch failed for method '{}' and url '{}': {}",
                parsed_method, parsed_url, request_error
            ));
            std::ptr::null_mut()
        }
    }
}

#[no_mangle]
pub extern "C" fn chakra_reqwest_download_fetch_parallel(
    url: *const c_char,
    output_path: *const c_char,
    parallel_part_count: i32,
) -> i32 {
    error::clear_last_error();

    let parsed_url = match ffi::c_str_to_string(url, "url") {
        Ok(value) => value,
        Err(_) => return 0,
    };

    let parsed_output_path = match ffi::c_str_to_string(output_path, "outputPath") {
        Ok(value) => value,
        Err(_) => return 0,
    };

    match download_fetch_parallel(&parsed_url, &parsed_output_path, parallel_part_count) {
        Ok(()) => 1,
        Err(download_error) => {
            error::set_last_error(&format!(
                "reqwest.downloadFetch failed for url '{}' to '{}': {}",
                parsed_url, parsed_output_path, download_error
            ));
            0
        }
    }
}
