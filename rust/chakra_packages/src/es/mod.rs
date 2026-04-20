use crate::common::{error, ffi};
use std::os::raw::c_char;

#[no_mangle]
pub extern "C" fn chakra_es2020_analyze(source: *const c_char) -> *mut c_char {
    error::clear_last_error();

    let source_text = match ffi::c_str_to_string(source, "source") {
        Ok(value) => value,
        Err(_) => return std::ptr::null_mut(),
    };

    match chakra_es2020::analyze_source_to_json(&source_text) {
        Ok(report_json) => ffi::to_owned_c_string_ptr(report_json),
        Err(parse_error) => {
            error::set_last_error(&format!("es2020.analyze failed: {}", parse_error));
            std::ptr::null_mut()
        }
    }
}

#[no_mangle]
pub extern "C" fn chakra_es2021_analyze(source: *const c_char) -> *mut c_char {
    error::clear_last_error();

    let source_text = match ffi::c_str_to_string(source, "source") {
        Ok(value) => value,
        Err(_) => return std::ptr::null_mut(),
    };

    match chakra_es2021::analyze_source_to_json(&source_text) {
        Ok(report_json) => ffi::to_owned_c_string_ptr(report_json),
        Err(parse_error) => {
            error::set_last_error(&format!("es2021.analyze failed: {}", parse_error));
            std::ptr::null_mut()
        }
    }
}

#[no_mangle]
pub extern "C" fn chakra_es2021_transform(source: *const c_char) -> *mut c_char {
    error::clear_last_error();

    let source_text = match ffi::c_str_to_string(source, "source") {
        Ok(value) => value,
        Err(_) => return std::ptr::null_mut(),
    };

    match chakra_es2021::transform_source_for_runtime(&source_text) {
        Ok(transformed_source) => ffi::to_owned_c_string_ptr(transformed_source),
        Err(transform_error) => {
            error::set_last_error(&format!("es2021.transform failed: {}", transform_error));
            std::ptr::null_mut()
        }
    }
}
