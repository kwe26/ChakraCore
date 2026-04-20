use std::cell::RefCell;
use std::ffi::CString;
use std::os::raw::c_char;

thread_local! {
    static LAST_ERROR_MESSAGE: RefCell<Option<CString>> = RefCell::new(None);
}

fn to_cstring_lossy(value: &str) -> CString {
    let sanitized = value.replace('\0', " ");
    CString::new(sanitized).unwrap_or_else(|_| CString::new("unknown error").unwrap())
}

pub fn set_last_error(value: &str) {
    LAST_ERROR_MESSAGE.with(|slot| {
        *slot.borrow_mut() = Some(to_cstring_lossy(value));
    });
}

pub fn clear_last_error() {
    LAST_ERROR_MESSAGE.with(|slot| {
        *slot.borrow_mut() = None;
    });
}

pub fn get_last_error_ptr() -> *const c_char {
    LAST_ERROR_MESSAGE.with(|slot| {
        let borrowed = slot.borrow();
        match borrowed.as_ref() {
            Some(error_message) => error_message.as_ptr(),
            None => std::ptr::null(),
        }
    })
}

#[no_mangle]
pub extern "C" fn chakra_last_error_message() -> *const c_char {
    get_last_error_ptr()
}
