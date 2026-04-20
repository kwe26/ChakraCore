use crate::common::error;
use std::ffi::{CStr, CString};
use std::os::raw::c_char;

pub fn c_str_to_string(value: *const c_char, argument_name: &str) -> Result<String, ()> {
    if value.is_null() {
        error::set_last_error(&format!("{} is null", argument_name));
        return Err(());
    }

    let c_str = unsafe { CStr::from_ptr(value) };
    match c_str.to_str() {
        Ok(parsed) => Ok(parsed.to_owned()),
        Err(parse_error) => {
            error::set_last_error(&format!(
                "{} is not valid UTF-8: {}",
                argument_name, parse_error
            ));
            Err(())
        }
    }
}

pub fn to_owned_c_string_ptr(value: String) -> *mut c_char {
    let sanitized = value.replace('\0', "");
    match CString::new(sanitized) {
        Ok(c_string) => c_string.into_raw(),
        Err(_) => {
            error::set_last_error("failed to encode output as C string");
            std::ptr::null_mut()
        }
    }
}

#[no_mangle]
pub extern "C" fn chakra_string_free(value: *mut c_char) {
    if value.is_null() {
        return;
    }

    unsafe {
        let _ = CString::from_raw(value);
    }
}
