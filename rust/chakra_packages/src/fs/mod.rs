use crate::common::{error, ffi};
use std::fs;
use std::os::raw::c_char;

#[no_mangle]
pub extern "C" fn chakra_fs_read_file_utf8(path: *const c_char) -> *mut c_char {
    error::clear_last_error();

    let path = match ffi::c_str_to_string(path, "path") {
        Ok(value) => value,
        Err(_) => return std::ptr::null_mut(),
    };

    match fs::read_to_string(&path) {
        Ok(contents) => ffi::to_owned_c_string_ptr(contents),
        Err(read_error) => {
            error::set_last_error(&format!(
                "fs.readFileSync failed for '{}': {}",
                path, read_error
            ));
            std::ptr::null_mut()
        }
    }
}

#[no_mangle]
pub extern "C" fn chakra_fs_write_file_utf8(path: *const c_char, content: *const c_char) -> i32 {
    error::clear_last_error();

    let path = match ffi::c_str_to_string(path, "path") {
        Ok(value) => value,
        Err(_) => return 0,
    };

    let content = match ffi::c_str_to_string(content, "content") {
        Ok(value) => value,
        Err(_) => return 0,
    };

    match fs::write(&path, content.as_bytes()) {
        Ok(()) => 1,
        Err(write_error) => {
            error::set_last_error(&format!(
                "fs.writeFileSync failed for '{}': {}",
                path, write_error
            ));
            0
        }
    }
}

#[no_mangle]
pub extern "C" fn chakra_fs_exists(path: *const c_char) -> i32 {
    error::clear_last_error();

    let path = match ffi::c_str_to_string(path, "path") {
        Ok(value) => value,
        Err(_) => return -1,
    };

    match fs::metadata(&path) {
        Ok(_) => 1,
        Err(metadata_error) => {
            if metadata_error.kind() == std::io::ErrorKind::NotFound {
                0
            } else {
                error::set_last_error(&format!(
                    "fs.existsSync failed for '{}': {}",
                    path, metadata_error
                ));
                -1
            }
        }
    }
}
