use crate::common::{error, ffi};
use std::os::raw::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};

fn panic_message(payload: &(dyn std::any::Any + Send)) -> String {
    if let Some(message) = payload.downcast_ref::<&str>() {
        return (*message).to_owned();
    }

    if let Some(message) = payload.downcast_ref::<String>() {
        return message.clone();
    }

    "unknown panic payload".to_owned()
}

fn run_operation_with_guard<F>(operation_name: &str, operation: F) -> *mut c_char
where
    F: FnOnce() -> Result<String, String>,
{
    match catch_unwind(AssertUnwindSafe(operation)) {
        Ok(Ok(result)) => ffi::to_owned_c_string_ptr(result),
        Ok(Err(operation_error)) => {
            error::set_last_error(&format!("{} failed: {}", operation_name, operation_error));
            std::ptr::null_mut()
        }
        Err(payload) => {
            error::set_last_error(&format!(
                "{} panicked: {}",
                operation_name,
                panic_message(payload.as_ref())
            ));
            std::ptr::null_mut()
        }
    }
}

#[no_mangle]
pub extern "C" fn chakra_es2020_analyze(source: *const c_char) -> *mut c_char {
    error::clear_last_error();

    let source_text = match ffi::c_str_to_string(source, "source") {
        Ok(value) => value,
        Err(_) => return std::ptr::null_mut(),
    };

    run_operation_with_guard("es2020.analyze", || {
        chakra_es2020::analyze_source_to_json(&source_text)
    })
}

#[no_mangle]
pub extern "C" fn chakra_es2021_analyze(source: *const c_char) -> *mut c_char {
    error::clear_last_error();

    let source_text = match ffi::c_str_to_string(source, "source") {
        Ok(value) => value,
        Err(_) => return std::ptr::null_mut(),
    };

    run_operation_with_guard("es2021.analyze", || {
        chakra_es2021::analyze_source_to_json(&source_text)
    })
}

#[no_mangle]
pub extern "C" fn chakra_es2021_transform(source: *const c_char) -> *mut c_char {
    error::clear_last_error();

    let source_text = match ffi::c_str_to_string(source, "source") {
        Ok(value) => value,
        Err(_) => return std::ptr::null_mut(),
    };

    run_operation_with_guard("es2021.transform", || {
        chakra_es2021::transform_source_for_runtime(&source_text)
    })
}
