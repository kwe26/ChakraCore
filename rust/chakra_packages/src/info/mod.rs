use std::os::raw::c_char;

static CHAKRA_INFO_VERSION: &[u8] = b"1.13.0\0";

#[no_mangle]
pub extern "C" fn chakra_info_version() -> *const c_char {
    CHAKRA_INFO_VERSION.as_ptr() as *const c_char
}
