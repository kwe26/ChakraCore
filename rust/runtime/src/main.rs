use rustyline::completion::{Completer, Pair};
use rustyline::error::ReadlineError;
use rustyline::highlight::Highlighter;
use rustyline::hint::Hinter;
use rustyline::validate::{ValidationContext, ValidationResult, Validator};
use rustyline::{Context as RustylineContext, Editor, Helper};
use libloading::{Library, Symbol};
use std::borrow::Cow;
use std::env;
use std::ffi::c_void;
use std::fmt::Write as _;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::ExitCode;
use std::ptr;

type JsErrorCode = i32;
type JsRuntimeHandle = *mut c_void;
type JsContextRef = *mut c_void;
type JsValueRef = *mut c_void;
type JsPropertyIdRef = *mut c_void;
type JsSourceContext = usize;
type JsRuntimeAttributes = u32;
type JsParseScriptAttributes = u32;
type JsValueType = u32;

const JS_NO_ERROR: JsErrorCode = 0;
const JS_ERROR_SCRIPT_EXCEPTION: JsErrorCode = 0x30001;
const JS_ERROR_SCRIPT_COMPILE: JsErrorCode = 0x30002;
const JS_RUNTIME_ATTRIBUTE_NONE: JsRuntimeAttributes = 0;
const JS_PARSE_SCRIPT_ATTRIBUTE_NONE: JsParseScriptAttributes = 0;
const JS_VALUE_TYPE_UNDEFINED: JsValueType = 0;

#[cfg(target_os = "windows")]
type JsNativeFunction = Option<
    unsafe extern "system" fn(
        callee: JsValueRef,
        is_construct_call: bool,
        arguments: *mut JsValueRef,
        argument_count: u16,
        callback_state: *mut c_void,
    ) -> JsValueRef,
>;

#[cfg(not(target_os = "windows"))]
type JsNativeFunction = Option<
    unsafe extern "C" fn(
        callee: JsValueRef,
        is_construct_call: bool,
        arguments: *mut JsValueRef,
        argument_count: u16,
        callback_state: *mut c_void,
    ) -> JsValueRef,
>;

#[cfg(target_os = "windows")]
type JsCreateRuntimeFn =
    unsafe extern "system" fn(JsRuntimeAttributes, *mut c_void, *mut JsRuntimeHandle) -> JsErrorCode;
#[cfg(not(target_os = "windows"))]
type JsCreateRuntimeFn =
    unsafe extern "C" fn(JsRuntimeAttributes, *mut c_void, *mut JsRuntimeHandle) -> JsErrorCode;

#[cfg(target_os = "windows")]
type JsDisposeRuntimeFn = unsafe extern "system" fn(JsRuntimeHandle) -> JsErrorCode;
#[cfg(not(target_os = "windows"))]
type JsDisposeRuntimeFn = unsafe extern "C" fn(JsRuntimeHandle) -> JsErrorCode;

#[cfg(target_os = "windows")]
type JsCreateContextFn = unsafe extern "system" fn(JsRuntimeHandle, *mut JsContextRef) -> JsErrorCode;
#[cfg(not(target_os = "windows"))]
type JsCreateContextFn = unsafe extern "C" fn(JsRuntimeHandle, *mut JsContextRef) -> JsErrorCode;

#[cfg(target_os = "windows")]
type JsSetCurrentContextFn = unsafe extern "system" fn(JsContextRef) -> JsErrorCode;
#[cfg(not(target_os = "windows"))]
type JsSetCurrentContextFn = unsafe extern "C" fn(JsContextRef) -> JsErrorCode;

#[cfg(target_os = "windows")]
type JsCreateStringFn = unsafe extern "system" fn(*const u8, usize, *mut JsValueRef) -> JsErrorCode;
#[cfg(not(target_os = "windows"))]
type JsCreateStringFn = unsafe extern "C" fn(*const u8, usize, *mut JsValueRef) -> JsErrorCode;

#[cfg(target_os = "windows")]
type JsRunFn = unsafe extern "system" fn(
    JsValueRef,
    JsSourceContext,
    JsValueRef,
    JsParseScriptAttributes,
    *mut JsValueRef,
) -> JsErrorCode;
#[cfg(not(target_os = "windows"))]
type JsRunFn = unsafe extern "C" fn(
    JsValueRef,
    JsSourceContext,
    JsValueRef,
    JsParseScriptAttributes,
    *mut JsValueRef,
) -> JsErrorCode;

#[cfg(target_os = "windows")]
type JsGetAndClearExceptionFn = unsafe extern "system" fn(*mut JsValueRef) -> JsErrorCode;
#[cfg(not(target_os = "windows"))]
type JsGetAndClearExceptionFn = unsafe extern "C" fn(*mut JsValueRef) -> JsErrorCode;

#[cfg(target_os = "windows")]
type JsConvertValueToStringFn = unsafe extern "system" fn(JsValueRef, *mut JsValueRef) -> JsErrorCode;
#[cfg(not(target_os = "windows"))]
type JsConvertValueToStringFn = unsafe extern "C" fn(JsValueRef, *mut JsValueRef) -> JsErrorCode;

#[cfg(target_os = "windows")]
type JsCopyStringFn = unsafe extern "system" fn(JsValueRef, *mut i8, usize, *mut usize) -> JsErrorCode;
#[cfg(not(target_os = "windows"))]
type JsCopyStringFn = unsafe extern "C" fn(JsValueRef, *mut i8, usize, *mut usize) -> JsErrorCode;

#[cfg(target_os = "windows")]
type JsGetGlobalObjectFn = unsafe extern "system" fn(*mut JsValueRef) -> JsErrorCode;
#[cfg(not(target_os = "windows"))]
type JsGetGlobalObjectFn = unsafe extern "C" fn(*mut JsValueRef) -> JsErrorCode;

#[cfg(target_os = "windows")]
type JsCreateFunctionFn =
    unsafe extern "system" fn(JsNativeFunction, *mut c_void, *mut JsValueRef) -> JsErrorCode;
#[cfg(not(target_os = "windows"))]
type JsCreateFunctionFn =
    unsafe extern "C" fn(JsNativeFunction, *mut c_void, *mut JsValueRef) -> JsErrorCode;

#[cfg(target_os = "windows")]
type JsCreateObjectFn = unsafe extern "system" fn(*mut JsValueRef) -> JsErrorCode;
#[cfg(not(target_os = "windows"))]
type JsCreateObjectFn = unsafe extern "C" fn(*mut JsValueRef) -> JsErrorCode;

#[cfg(target_os = "windows")]
type JsCreatePropertyIdFn = unsafe extern "system" fn(*const u8, usize, *mut JsPropertyIdRef) -> JsErrorCode;
#[cfg(not(target_os = "windows"))]
type JsCreatePropertyIdFn = unsafe extern "C" fn(*const u8, usize, *mut JsPropertyIdRef) -> JsErrorCode;

#[cfg(target_os = "windows")]
type JsSetPropertyFn = unsafe extern "system" fn(
    JsValueRef,
    JsPropertyIdRef,
    JsValueRef,
    bool,
) -> JsErrorCode;
#[cfg(not(target_os = "windows"))]
type JsSetPropertyFn =
    unsafe extern "C" fn(JsValueRef, JsPropertyIdRef, JsValueRef, bool) -> JsErrorCode;

#[cfg(target_os = "windows")]
type JsGetUndefinedValueFn = unsafe extern "system" fn(*mut JsValueRef) -> JsErrorCode;
#[cfg(not(target_os = "windows"))]
type JsGetUndefinedValueFn = unsafe extern "C" fn(*mut JsValueRef) -> JsErrorCode;

#[cfg(target_os = "windows")]
type JsGetValueTypeFn = unsafe extern "system" fn(JsValueRef, *mut JsValueType) -> JsErrorCode;
#[cfg(not(target_os = "windows"))]
type JsGetValueTypeFn = unsafe extern "C" fn(JsValueRef, *mut JsValueType) -> JsErrorCode;

#[cfg(target_os = "windows")]
type JsInstallChakraSystemRequireFn = unsafe extern "system" fn(*mut JsValueRef) -> JsErrorCode;
#[cfg(not(target_os = "windows"))]
type JsInstallChakraSystemRequireFn = unsafe extern "C" fn(*mut JsValueRef) -> JsErrorCode;

#[cfg(target_os = "windows")]
type JsChakraEs2021TransformFn = unsafe extern "system" fn(JsValueRef, *mut JsValueRef) -> JsErrorCode;
#[cfg(not(target_os = "windows"))]
type JsChakraEs2021TransformFn = unsafe extern "C" fn(JsValueRef, *mut JsValueRef) -> JsErrorCode;

struct ChakraApi {
    _library: Library,
    js_create_runtime: JsCreateRuntimeFn,
    js_dispose_runtime: JsDisposeRuntimeFn,
    js_create_context: JsCreateContextFn,
    js_set_current_context: JsSetCurrentContextFn,
    js_create_string: JsCreateStringFn,
    js_run: JsRunFn,
    js_get_and_clear_exception: JsGetAndClearExceptionFn,
    js_convert_value_to_string: JsConvertValueToStringFn,
    js_copy_string: JsCopyStringFn,
    js_get_global_object: JsGetGlobalObjectFn,
    js_create_function: JsCreateFunctionFn,
    js_create_object: JsCreateObjectFn,
    js_create_property_id: JsCreatePropertyIdFn,
    js_set_property: JsSetPropertyFn,
    js_get_undefined_value: JsGetUndefinedValueFn,
    js_get_value_type: JsGetValueTypeFn,
    js_install_chakra_system_require: Option<JsInstallChakraSystemRequireFn>,
    js_chakra_es2021_transform: Option<JsChakraEs2021TransformFn>,
}

struct HostRuntime {
    api: ChakraApi,
    runtime: JsRuntimeHandle,
    print_callback_state: Box<PrintCallbackState>,
    console_callback_states: Vec<Box<ConsoleCallbackState>>,
}

struct PrintCallbackState {
    api: *const ChakraApi,
}

#[derive(Copy, Clone)]
enum ConsoleMethodKind {
    Log,
    Info,
    Warn,
    Error,
    Debug,
    Dir,
}

struct ConsoleCallbackState {
    api: *const ChakraApi,
    kind: ConsoleMethodKind,
}

struct ReplHelper {
    keywords: &'static [&'static str],
}

impl Helper for ReplHelper {}

impl Completer for ReplHelper {
    type Candidate = Pair;

    fn complete(
        &self,
        _line: &str,
        _pos: usize,
        _ctx: &RustylineContext<'_>,
    ) -> rustyline::Result<(usize, Vec<Self::Candidate>)> {
        Ok((0, Vec::new()))
    }
}

impl Hinter for ReplHelper {
    type Hint = String;

    fn hint(&self, _line: &str, _pos: usize, _ctx: &RustylineContext<'_>) -> Option<String> {
        None
    }
}

impl Validator for ReplHelper {
    fn validate(&self, _ctx: &mut ValidationContext<'_>) -> rustyline::Result<ValidationResult> {
        Ok(ValidationResult::Valid(None))
    }
}

impl Highlighter for ReplHelper {
    fn highlight<'l>(&self, line: &'l str, _pos: usize) -> Cow<'l, str> {
        Cow::Owned(highlight_js_line(line, self.keywords))
    }

    fn highlight_prompt<'b, 's: 'b, 'p: 'b>(
        &'s self,
        prompt: &'p str,
        _default: bool,
    ) -> Cow<'b, str> {
        Cow::Owned(format!("\x1b[1;32m{}\x1b[0m", prompt))
    }

    fn highlight_hint<'h>(&self, hint: &'h str) -> Cow<'h, str> {
        Cow::Owned(format!("\x1b[2m{}\x1b[0m", hint))
    }
}

#[cfg(target_os = "windows")]
unsafe extern "system" fn print_callback(
    _callee: JsValueRef,
    _is_construct_call: bool,
    arguments: *mut JsValueRef,
    argument_count: u16,
    callback_state: *mut c_void,
) -> JsValueRef {
    print_callback_impl(arguments, argument_count, callback_state)
}

#[cfg(not(target_os = "windows"))]
unsafe extern "C" fn print_callback(
    _callee: JsValueRef,
    _is_construct_call: bool,
    arguments: *mut JsValueRef,
    argument_count: u16,
    callback_state: *mut c_void,
) -> JsValueRef {
    print_callback_impl(arguments, argument_count, callback_state)
}

unsafe fn print_callback_impl(
    arguments: *mut JsValueRef,
    argument_count: u16,
    callback_state: *mut c_void,
) -> JsValueRef {
    if callback_state.is_null() {
        return ptr::null_mut();
    }

    let state = &*(callback_state as *const PrintCallbackState);
    let api = &*state.api;

    let mut output = String::new();
    for i in 1..(argument_count as usize) {
        let value = *arguments.add(i);
        match value_to_string(api, value) {
            Ok(text) => {
                if !output.is_empty() {
                    output.push(' ');
                }
                output.push_str(&text);
            }
            Err(err) => {
                if !output.is_empty() {
                    output.push(' ');
                }
                let _ = write!(output, "<toString failed: {}>", err);
            }
        }
    }

    println!("{}", output);

    let mut undefined = ptr::null_mut();
    if (api.js_get_undefined_value)(&mut undefined) != JS_NO_ERROR {
        return ptr::null_mut();
    }

    undefined
}

#[cfg(target_os = "windows")]
unsafe extern "system" fn console_callback_windows(
    _callee: JsValueRef,
    _is_construct_call: bool,
    arguments: *mut JsValueRef,
    argument_count: u16,
    callback_state: *mut c_void,
) -> JsValueRef {
    console_callback_impl(arguments, argument_count, callback_state)
}

#[cfg(not(target_os = "windows"))]
unsafe extern "C" fn console_callback_unix(
    _callee: JsValueRef,
    _is_construct_call: bool,
    arguments: *mut JsValueRef,
    argument_count: u16,
    callback_state: *mut c_void,
) -> JsValueRef {
    console_callback_impl(arguments, argument_count, callback_state)
}

#[cfg(target_os = "windows")]
const CONSOLE_CALLBACK: JsNativeFunction = Some(console_callback_windows);
#[cfg(not(target_os = "windows"))]
const CONSOLE_CALLBACK: JsNativeFunction = Some(console_callback_unix);

unsafe fn console_callback_impl(
    arguments: *mut JsValueRef,
    argument_count: u16,
    callback_state: *mut c_void,
) -> JsValueRef {
    if callback_state.is_null() {
        return ptr::null_mut();
    }

    let state = &*(callback_state as *const ConsoleCallbackState);
    let api = &*state.api;

    let mut output = String::new();
    let prefix = match state.kind {
        ConsoleMethodKind::Log | ConsoleMethodKind::Info | ConsoleMethodKind::Debug | ConsoleMethodKind::Dir => "",
        ConsoleMethodKind::Warn => "warn: ",
        ConsoleMethodKind::Error => "error: ",
    };
    output.push_str(prefix);

    match state.kind {
        ConsoleMethodKind::Dir => {
            if argument_count > 1 {
                match value_to_string(api, *arguments.add(1)) {
                    Ok(text) => output.push_str(&text),
                    Err(err) => {
                        let _ = write!(output, "<toString failed: {}>", err);
                    }
                }
            }
        }
        _ => {
            for i in 1..(argument_count as usize) {
                let value = *arguments.add(i);
                match value_to_string(api, value) {
                    Ok(text) => {
                        if !output.is_empty() && !output.ends_with(' ') {
                            output.push(' ');
                        }
                        output.push_str(&text);
                    }
                    Err(err) => {
                        if !output.is_empty() && !output.ends_with(' ') {
                            output.push(' ');
                        }
                        let _ = write!(output, "<toString failed: {}>", err);
                    }
                }
            }
        }
    }

    match state.kind {
        ConsoleMethodKind::Warn | ConsoleMethodKind::Error => eprintln!("{}", output),
        _ => println!("{}", output),
    }

    let mut undefined = ptr::null_mut();
    if (api.js_get_undefined_value)(&mut undefined) != JS_NO_ERROR {
        return ptr::null_mut();
    }

    undefined
}

impl ChakraApi {
    fn load(path_hint: Option<&Path>) -> Result<Self, String> {
        let candidates = chakra_library_candidates(path_hint);

        let mut last_error = String::new();
        for candidate in &candidates {
            let result = unsafe { Library::new(candidate) };
            match result {
                Ok(library) => {
                    return unsafe { Self::from_library(library) };
                }
                Err(err) => {
                    last_error = format!("{}: {}", candidate.display(), err);
                }
            }
        }

        if candidates.is_empty() {
            Err("No ChakraCore shared library candidates found.".to_string())
        } else {
            Err(format!(
                "Unable to load ChakraCore shared library. Last error: {}",
                last_error
            ))
        }
    }

    unsafe fn from_library(library: Library) -> Result<Self, String> {
        unsafe fn get_required<T: Copy>(library: &Library, symbol: &[u8]) -> Result<T, String> {
            let loaded: Symbol<T> = library
                .get(symbol)
                .map_err(|e| format!("Missing symbol {}: {}", String::from_utf8_lossy(symbol), e))?;
            Ok(*loaded)
        }

        unsafe fn get_optional<T: Copy>(library: &Library, symbol: &[u8]) -> Option<T> {
            match library.get::<T>(symbol) {
                Ok(loaded) => Some(*loaded),
                Err(_) => None,
            }
        }

        let js_create_runtime = get_required::<JsCreateRuntimeFn>(&library, b"JsCreateRuntime")?;
        let js_dispose_runtime = get_required::<JsDisposeRuntimeFn>(&library, b"JsDisposeRuntime")?;
        let js_create_context = get_required::<JsCreateContextFn>(&library, b"JsCreateContext")?;
        let js_set_current_context =
            get_required::<JsSetCurrentContextFn>(&library, b"JsSetCurrentContext")?;
        let js_create_string = get_required::<JsCreateStringFn>(&library, b"JsCreateString")?;
        let js_run = get_required::<JsRunFn>(&library, b"JsRun")?;
        let js_get_and_clear_exception =
            get_required::<JsGetAndClearExceptionFn>(&library, b"JsGetAndClearException")?;
        let js_convert_value_to_string =
            get_required::<JsConvertValueToStringFn>(&library, b"JsConvertValueToString")?;
        let js_copy_string = get_required::<JsCopyStringFn>(&library, b"JsCopyString")?;
        let js_get_global_object =
            get_required::<JsGetGlobalObjectFn>(&library, b"JsGetGlobalObject")?;
        let js_create_function =
            get_required::<JsCreateFunctionFn>(&library, b"JsCreateFunction")?;
        let js_create_object = get_required::<JsCreateObjectFn>(&library, b"JsCreateObject")?;
        let js_create_property_id =
            get_required::<JsCreatePropertyIdFn>(&library, b"JsCreatePropertyId")?;
        let js_set_property = get_required::<JsSetPropertyFn>(&library, b"JsSetProperty")?;
        let js_get_undefined_value =
            get_required::<JsGetUndefinedValueFn>(&library, b"JsGetUndefinedValue")?;
        let js_get_value_type = get_required::<JsGetValueTypeFn>(&library, b"JsGetValueType")?;

        let js_install_chakra_system_require =
            get_optional::<JsInstallChakraSystemRequireFn>(&library, b"JsInstallChakraSystemRequire");
        let js_chakra_es2021_transform =
            get_optional::<JsChakraEs2021TransformFn>(&library, b"JsChakraEs2021Transform");

        Ok(Self {
            _library: library,
            js_create_runtime,
            js_dispose_runtime,
            js_create_context,
            js_set_current_context,
            js_create_string,
            js_run,
            js_get_and_clear_exception,
            js_convert_value_to_string,
            js_copy_string,
            js_get_global_object,
            js_create_function,
            js_create_object,
            js_create_property_id,
            js_set_property,
            js_get_undefined_value,
            js_get_value_type,
            js_install_chakra_system_require,
            js_chakra_es2021_transform,
        })
    }
}

impl HostRuntime {
    fn create(chakra_library_hint: Option<&Path>) -> Result<Self, String> {
        let api = ChakraApi::load(chakra_library_hint)?;

        let mut runtime = ptr::null_mut();
        let create_runtime = unsafe {
            (api.js_create_runtime)(
                JS_RUNTIME_ATTRIBUTE_NONE,
                ptr::null_mut(),
                &mut runtime as *mut JsRuntimeHandle,
            )
        };
        ensure_js_ok(create_runtime, "JsCreateRuntime")?;

        let mut context = ptr::null_mut();
        let create_context = unsafe { (api.js_create_context)(runtime, &mut context as *mut JsContextRef) };
        if create_context != JS_NO_ERROR {
            unsafe {
                (api.js_dispose_runtime)(runtime);
            }
            return Err(format_js_error("JsCreateContext", create_context));
        }

        let set_context = unsafe { (api.js_set_current_context)(context) };
        if set_context != JS_NO_ERROR {
            unsafe {
                (api.js_dispose_runtime)(runtime);
            }
            return Err(format_js_error("JsSetCurrentContext", set_context));
        }

        let print_callback_state = Box::new(PrintCallbackState {
            api: &api as *const ChakraApi,
        });

        let mut host = Self {
            api,
            runtime,
            print_callback_state,
            console_callback_states: Vec::new(),
        };

        host.install_print()?;
        host.install_console()?;
        host.try_install_chakra_system_require();

        Ok(host)
    }

    fn run_script_file(&self, script_path: &Path) -> Result<(), String> {
        let script_source = fs::read_to_string(script_path)
            .map_err(|e| format!("Failed to read script {}: {}", script_path.display(), e))?;

        let source_label = script_path
            .to_str()
            .map(|s| s.to_string())
            .unwrap_or_else(|| script_path.display().to_string());

        self.run_script_source(&script_source, &source_label)?;
        Ok(())
    }

    fn run_script_source(&self, source_text: &str, source_label: &str) -> Result<JsValueRef, String> {
        let runtime_script_source = self.maybe_transform_entry_source(source_text);
        let script_value = self.create_js_string(&runtime_script_source)?;
        let source_url = self.create_js_string(source_label)?;

        let mut result_value = ptr::null_mut();
        let run_result = unsafe {
            (self.api.js_run)(
                script_value,
                0,
                source_url,
                JS_PARSE_SCRIPT_ATTRIBUTE_NONE,
                &mut result_value as *mut JsValueRef,
            )
        };

        if run_result != JS_NO_ERROR {
            return Err(self.report_script_failure("JsRun", run_result).unwrap_err());
        }

        Ok(result_value)
    }

    fn maybe_transform_entry_source(&self, source_text: &str) -> String {
        if !should_try_es2021_transform(source_text) {
            return source_text.to_string();
        }

        let Some(transform_fn) = self.api.js_chakra_es2021_transform else {
            return source_text.to_string();
        };

        let source_value = match self.create_js_string(source_text) {
            Ok(value) => value,
            Err(_) => return source_text.to_string(),
        };

        let mut transformed_value = ptr::null_mut();
        let transform_result = unsafe {
            transform_fn(source_value, &mut transformed_value as *mut JsValueRef)
        };

        if transform_result != JS_NO_ERROR {
            eprintln!(
                "warning: ES2021 transform failed, running original source: {}",
                self.describe_current_exception()
                    .unwrap_or_else(|| format_js_error("JsChakraEs2021Transform", transform_result))
            );
            return source_text.to_string();
        }

        match value_to_string(&self.api, transformed_value) {
            Ok(transformed_text) if !transformed_text.is_empty() => transformed_text,
            _ => source_text.to_string(),
        }
    }

    fn install_print(&mut self) -> Result<(), String> {
        let mut global = ptr::null_mut();
        let get_global = unsafe { (self.api.js_get_global_object)(&mut global as *mut JsValueRef) };
        ensure_js_ok(get_global, "JsGetGlobalObject")?;

        let mut print_function = ptr::null_mut();
        let create_function = unsafe {
            (self.api.js_create_function)(
                Some(print_callback),
                self.print_callback_state.as_mut() as *mut PrintCallbackState as *mut c_void,
                &mut print_function as *mut JsValueRef,
            )
        };
        ensure_js_ok(create_function, "JsCreateFunction(print)")?;

        let property_id = self.create_property_id("print")?;
        let set_property = unsafe {
            (self.api.js_set_property)(
                global,
                property_id,
                print_function,
                true,
            )
        };

        ensure_js_ok(set_property, "JsSetProperty(global.print)")
    }

    fn install_console(&mut self) -> Result<(), String> {
        let mut global = ptr::null_mut();
        let get_global = unsafe { (self.api.js_get_global_object)(&mut global as *mut JsValueRef) };
        ensure_js_ok(get_global, "JsGetGlobalObject")?;

        let mut console_object = ptr::null_mut();
        let create_object = unsafe { (self.api.js_create_object)(&mut console_object as *mut JsValueRef) };
        ensure_js_ok(create_object, "JsCreateObject(console)")?;

        let console_methods = [
            ("log", ConsoleMethodKind::Log),
            ("info", ConsoleMethodKind::Info),
            ("warn", ConsoleMethodKind::Warn),
            ("error", ConsoleMethodKind::Error),
            ("debug", ConsoleMethodKind::Debug),
            ("dir", ConsoleMethodKind::Dir),
        ];

        for (method_name, method_kind) in console_methods {
            let callback_state = Box::new(ConsoleCallbackState {
                api: &self.api as *const ChakraApi,
                kind: method_kind,
            });
            let callback_state_ptr = callback_state.as_ref() as *const ConsoleCallbackState as *mut c_void;
            self.console_callback_states.push(callback_state);

            let mut method_function = ptr::null_mut();
            let create_function = unsafe {
                (self.api.js_create_function)(
                    CONSOLE_CALLBACK,
                    callback_state_ptr,
                    &mut method_function as *mut JsValueRef,
                )
            };
            ensure_js_ok(create_function, &format!("JsCreateFunction(console.{})", method_name))?;

            let property_id = self.create_property_id(method_name)?;
            let set_property = unsafe {
                (self.api.js_set_property)(console_object, property_id, method_function, true)
            };
            ensure_js_ok(set_property, &format!("JsSetProperty(console.{})", method_name))?;
        }

        let console_property = self.create_property_id("console")?;
        let set_global_console = unsafe {
            (self.api.js_set_property)(global, console_property, console_object, true)
        };
        ensure_js_ok(set_global_console, "JsSetProperty(global.console)")
    }

    fn try_install_chakra_system_require(&self) {
        let Some(install_require) = self.api.js_install_chakra_system_require else {
            eprintln!("warning: JsInstallChakraSystemRequire is unavailable in this ChakraCore build");
            return;
        };

        let mut require_function = ptr::null_mut();
        let install_result = unsafe { install_require(&mut require_function as *mut JsValueRef) };
        if install_result != JS_NO_ERROR {
            eprintln!(
                "warning: failed to install chakra system require: {}",
                self.describe_current_exception()
                    .unwrap_or_else(|| format_js_error("JsInstallChakraSystemRequire", install_result))
            );
        }
    }

    fn create_js_string(&self, text: &str) -> Result<JsValueRef, String> {
        let mut value = ptr::null_mut();
        let create_string = unsafe {
            (self.api.js_create_string)(
                text.as_ptr(),
                text.len(),
                &mut value as *mut JsValueRef,
            )
        };
        ensure_js_ok(create_string, "JsCreateString")?;
        Ok(value)
    }

    fn create_property_id(&self, name: &str) -> Result<JsPropertyIdRef, String> {
        let mut property_id = ptr::null_mut();
        let create_property_id = unsafe {
            (self.api.js_create_property_id)(
                name.as_ptr(),
                name.len(),
                &mut property_id as *mut JsPropertyIdRef,
            )
        };
        ensure_js_ok(create_property_id, "JsCreatePropertyId")?;
        Ok(property_id)
    }

    fn report_script_failure(&self, operation: &str, error_code: JsErrorCode) -> Result<(), String> {
        let exception_text = self.describe_current_exception();

        if let Some(text) = exception_text {
            Err(format!(
                "{} failed with {}\nJavaScript exception: {}",
                operation,
                error_name(error_code),
                text
            ))
        } else {
            Err(format_js_error(operation, error_code))
        }
    }

    fn describe_current_exception(&self) -> Option<String> {
        let mut exception: JsValueRef = ptr::null_mut();
        let error_code = unsafe {
            (self.api.js_get_and_clear_exception)(&mut exception as *mut JsValueRef)
        };

        if error_code != JS_NO_ERROR || exception.is_null() {
            return None;
        }

        value_to_string(&self.api, exception).ok()
    }

    fn run_repl(&self) -> Result<(), String> {
        let helper = ReplHelper {
            keywords: JS_KEYWORDS,
        };

        let mut editor: Editor<ReplHelper, rustyline::history::DefaultHistory> =
            Editor::new().map_err(|e| format!("Failed to initialize interactive editor: {}", e))?;
        editor.set_helper(Some(helper));
        let _ = editor.load_history(".chakra_runtime_history");

        println!("ChakraCore Rust runtime REPL");
        println!("Type .exit or press Ctrl-D to quit.");

        let mut buffer = String::new();
        loop {
            let prompt = if buffer.is_empty() { "chakra> " } else { "...> " };
            match editor.readline(prompt) {
                Ok(line) => {
                    let trimmed = line.trim();
                    if buffer.is_empty() && (trimmed == ".exit" || trimmed == ".quit") {
                        break;
                    }

                    if buffer.is_empty() && trimmed.is_empty() {
                        continue;
                    }

                    buffer.push_str(&line);
                    buffer.push('\n');

                    if js_source_needs_more_input(&buffer) {
                        continue;
                    }

                    let submitted = buffer.trim_end().to_string();
                    if submitted.is_empty() {
                        buffer.clear();
                        continue;
                    }

                    let _ = editor.add_history_entry(submitted.as_str());
                    match self.run_script_source(&submitted, "<repl>") {
                        Ok(result_value) => {
                            if !is_undefined_value(&self.api, result_value) {
                                match value_to_string(&self.api, result_value) {
                                    Ok(text) if !text.is_empty() => println!("\x1b[1;36m{}\x1b[0m", text),
                                    Ok(_) => {}
                                    Err(err) => eprintln!("repl result conversion failed: {}", err),
                                }
                            }
                        }
                        Err(err) => eprintln!("{}", err),
                    }

                    buffer.clear();
                }
                Err(ReadlineError::Interrupted) => {
                    println!("^C");
                    buffer.clear();
                }
                Err(ReadlineError::Eof) => break,
                Err(err) => return Err(format!("Interactive editor failed: {}", err)),
            }
        }

        let _ = editor.save_history(".chakra_runtime_history");
        Ok(())
    }
}

impl Drop for HostRuntime {
    fn drop(&mut self) {
        unsafe {
            let _ = (self.api.js_set_current_context)(ptr::null_mut());
            let _ = (self.api.js_dispose_runtime)(self.runtime);
        }
    }
}

fn value_to_string(api: &ChakraApi, value: JsValueRef) -> Result<String, String> {
    let mut string_value = ptr::null_mut();
    let convert_result = unsafe {
        (api.js_convert_value_to_string)(value, &mut string_value as *mut JsValueRef)
    };
    ensure_js_ok(convert_result, "JsConvertValueToString")?;

    let mut required_len: usize = 0;
    let size_result = unsafe {
        (api.js_copy_string)(
            string_value,
            ptr::null_mut(),
            0,
            &mut required_len as *mut usize,
        )
    };
    ensure_js_ok(size_result, "JsCopyString(length)")?;

    if required_len == 0 {
        return Ok(String::new());
    }

    let mut buffer = vec![0u8; required_len];
    let mut written_len: usize = 0;
    let copy_result = unsafe {
        (api.js_copy_string)(
            string_value,
            buffer.as_mut_ptr() as *mut i8,
            buffer.len(),
            &mut written_len as *mut usize,
        )
    };
    ensure_js_ok(copy_result, "JsCopyString(data)")?;

    buffer.truncate(written_len);
    String::from_utf8(buffer)
        .map_err(|e| format!("String conversion from UTF-8 failed: {}", e))
}

fn is_undefined_value(api: &ChakraApi, value: JsValueRef) -> bool {
    let mut value_type = JS_VALUE_TYPE_UNDEFINED;
    let get_type = unsafe { (api.js_get_value_type)(value, &mut value_type as *mut JsValueType) };
    get_type == JS_NO_ERROR && value_type == JS_VALUE_TYPE_UNDEFINED
}

fn ensure_js_ok(error_code: JsErrorCode, operation: &str) -> Result<(), String> {
    if error_code == JS_NO_ERROR {
        Ok(())
    } else {
        Err(format_js_error(operation, error_code))
    }
}

fn format_js_error(operation: &str, error_code: JsErrorCode) -> String {
    format!("{} failed with {} (0x{:X})", operation, error_name(error_code), error_code)
}

fn error_name(error_code: JsErrorCode) -> &'static str {
    match error_code {
        JS_NO_ERROR => "JsNoError",
        0x10001 => "JsErrorInvalidArgument",
        0x10003 => "JsErrorNoCurrentContext",
        0x10004 => "JsErrorInExceptionState",
        0x10007 => "JsErrorRuntimeInUse",
        0x20000 => "JsErrorCategoryEngine",
        0x20001 => "JsErrorOutOfMemory",
        JS_ERROR_SCRIPT_EXCEPTION => "JsErrorScriptException",
        JS_ERROR_SCRIPT_COMPILE => "JsErrorScriptCompile",
        0x30003 => "JsErrorScriptTerminated",
        0x40001 => "JsErrorFatal",
        _ => "JsErrorCode(unknown)",
    }
}

fn chakra_library_candidates(path_hint: Option<&Path>) -> Vec<PathBuf> {
    let mut candidates = Vec::new();

    if let Some(hint) = path_hint {
        candidates.push(hint.to_path_buf());
    }

    if let Ok(path_from_env) = env::var("CHAKRA_CORE_PATH") {
        if !path_from_env.trim().is_empty() {
            candidates.push(PathBuf::from(path_from_env));
        }
    }

    let default_name = default_chakra_library_name();

    if let Ok(exe_path) = env::current_exe() {
        if let Some(exe_dir) = exe_path.parent() {
            candidates.push(exe_dir.join(default_name));
        }
    }

    if let Ok(current_dir) = env::current_dir() {
        candidates.push(current_dir.join(default_name));
    }

    candidates.push(PathBuf::from(default_name));

    dedupe_paths(candidates)
}

fn dedupe_paths(paths: Vec<PathBuf>) -> Vec<PathBuf> {
    let mut unique = Vec::new();
    for path in paths {
        if !unique.iter().any(|existing| existing == &path) {
            unique.push(path);
        }
    }
    unique
}

fn should_try_es2021_transform(source_text: &str) -> bool {
    source_text.contains("&&=") || source_text.contains("||=") || source_text.contains("??=")
}

fn js_source_needs_more_input(source: &str) -> bool {
    let mut brace_depth = 0i32;
    let mut bracket_depth = 0i32;
    let mut paren_depth = 0i32;
    let mut in_single = false;
    let mut in_double = false;
    let mut in_template = false;
    let mut in_line_comment = false;
    let mut in_block_comment = false;
    let mut escaped = false;

    for ch in source.chars() {
        if in_line_comment {
            if ch == '\n' {
                in_line_comment = false;
            }
            continue;
        }

        if in_block_comment {
            if ch == '*' {
                escaped = true;
            } else if ch == '/' && escaped {
                in_block_comment = false;
                escaped = false;
            } else {
                escaped = false;
            }
            continue;
        }

        if in_single {
            if ch == '\\' && !escaped {
                escaped = true;
                continue;
            }
            if ch == '\'' && !escaped {
                in_single = false;
            }
            escaped = false;
            continue;
        }

        if in_double {
            if ch == '\\' && !escaped {
                escaped = true;
                continue;
            }
            if ch == '"' && !escaped {
                in_double = false;
            }
            escaped = false;
            continue;
        }

        if in_template {
            if ch == '\\' && !escaped {
                escaped = true;
                continue;
            }
            if ch == '`' && !escaped {
                in_template = false;
            }
            escaped = false;
            continue;
        }

        match ch {
            '/' => {
                escaped = !escaped;
            }
            '\'' => in_single = true,
            '"' => in_double = true,
            '`' => in_template = true,
            '{' => brace_depth += 1,
            '}' => brace_depth -= 1,
            '[' => bracket_depth += 1,
            ']' => bracket_depth -= 1,
            '(' => paren_depth += 1,
            ')' => paren_depth -= 1,
            _ => {}
        }

        if ch == '/' && !escaped {
            let prev_two = source.chars().rev().take(2).collect::<Vec<char>>();
            if prev_two.len() == 2 && prev_two[0] == '/' && prev_two[1] == '/' {
                in_line_comment = true;
            }
        }

        if ch == '*' && !escaped {
            let prev_two = source.chars().rev().take(2).collect::<Vec<char>>();
            if prev_two.len() == 2 && prev_two[0] == '/' && prev_two[1] == '*' {
                in_block_comment = true;
            }
        }
    }

    if in_single || in_double || in_template || in_line_comment || in_block_comment {
        return true;
    }

    if brace_depth > 0 || bracket_depth > 0 || paren_depth > 0 {
        return true;
    }

    let trimmed = source.trim_end();
    if trimmed.is_empty() {
        return true;
    }

    matches!(trimmed.chars().last(), Some('\\' | '.' | ',' | ':' | '=' | '+' | '-' | '*' | '/' | '%' | '&' | '|' | '^' | '!' | '?' | '(' | '[' | '{'))
}

fn highlight_js_line(line: &str, keywords: &[&str]) -> String {
    let mut output = String::with_capacity(line.len() + 32);
    let mut chars = line.chars().peekable();
    let mut in_single = false;
    let mut in_double = false;
    let mut in_template = false;
    let mut in_block_comment = false;

    while let Some(ch) = chars.next() {
        if in_block_comment {
            output.push_str("\x1b[38;5;244m");
            output.push(ch);
            if ch == '*' && matches!(chars.peek(), Some('/')) {
                output.push('/');
                let _ = chars.next();
                output.push_str("\x1b[0m");
                in_block_comment = false;
            }
            continue;
        }

        if in_single {
            output.push_str("\x1b[38;5;214m");
            output.push(ch);
            if ch == '\\' {
                if let Some(next) = chars.next() {
                    output.push(next);
                }
            } else if ch == '\'' {
                output.push_str("\x1b[0m");
                in_single = false;
            }
            continue;
        }

        if in_double {
            output.push_str("\x1b[38;5;214m");
            output.push(ch);
            if ch == '\\' {
                if let Some(next) = chars.next() {
                    output.push(next);
                }
            } else if ch == '"' {
                output.push_str("\x1b[0m");
                in_double = false;
            }
            continue;
        }

        if in_template {
            output.push_str("\x1b[38;5;214m");
            output.push(ch);
            if ch == '\\' {
                if let Some(next) = chars.next() {
                    output.push(next);
                }
            } else if ch == '`' {
                output.push_str("\x1b[0m");
                in_template = false;
            }
            continue;
        }

        if ch == '/' {
            match chars.peek() {
                Some('/') => {
                    output.push_str("\x1b[38;5;244m//");
                    let _ = chars.next();
                    for rest in chars {
                        output.push(rest);
                    }
                    output.push_str("\x1b[0m");
                    break;
                }
                Some('*') => {
                    output.push_str("\x1b[38;5;244m/*");
                    let _ = chars.next();
                    in_block_comment = true;
                    continue;
                }
                _ => {}
            }
        }

        if ch == '\'' {
            output.push_str("\x1b[38;5;214m'");
            in_single = true;
            continue;
        }

        if ch == '"' {
            output.push_str("\x1b[38;5;214m\"");
            in_double = true;
            continue;
        }

        if ch == '`' {
            output.push_str("\x1b[38;5;214m`");
            in_template = true;
            continue;
        }

        if ch.is_ascii_digit() {
            output.push_str("\x1b[38;5;81m");
            output.push(ch);
            while let Some(next) = chars.peek() {
                if next.is_ascii_alphanumeric() || matches!(next, '.' | '_' | 'x' | 'X' | 'b' | 'B' | 'o' | 'O') {
                    output.push(*next);
                    let _ = chars.next();
                } else {
                    break;
                }
            }
            output.push_str("\x1b[0m");
            continue;
        }

        if ch.is_ascii_alphabetic() || ch == '_' || ch == '$' {
            let mut word = String::new();
            word.push(ch);
            while let Some(next) = chars.peek() {
                if next.is_ascii_alphanumeric() || *next == '_' || *next == '$' {
                    word.push(*next);
                    let _ = chars.next();
                } else {
                    break;
                }
            }

            if keywords.contains(&word.as_str()) {
                output.push_str("\x1b[1;34m");
                output.push_str(&word);
                output.push_str("\x1b[0m");
            } else if word == "console" || word == "print" || word == "require" {
                output.push_str("\x1b[1;36m");
                output.push_str(&word);
                output.push_str("\x1b[0m");
            } else {
                output.push_str(&word);
            }
            continue;
        }

        output.push(ch);
    }

    output.push_str("\x1b[0m");
    output
}

const JS_KEYWORDS: &[&str] = &[
    "await", "async", "break", "case", "catch", "class", "const", "continue", "debugger",
    "default", "delete", "do", "else", "enum", "export", "extends", "false", "finally",
    "for", "function", "if", "import", "in", "instanceof", "let", "new", "null", "return",
    "super", "switch", "this", "throw", "true", "try", "typeof", "var", "void", "while",
    "with", "yield",
];

fn default_chakra_library_name() -> &'static str {
    #[cfg(target_os = "windows")]
    {
        "ChakraCore.dll"
    }
    #[cfg(target_os = "linux")]
    {
        "libChakraCore.so"
    }
    #[cfg(target_os = "macos")]
    {
        "libChakraCore.dylib"
    }
    #[cfg(not(any(target_os = "windows", target_os = "linux", target_os = "macos")))]
    {
        "libChakraCore.so"
    }
}

struct CliArgs {
    chakra_lib: Option<PathBuf>,
    script_path: Option<PathBuf>,
    repl: bool,
}

fn parse_cli_args() -> Result<CliArgs, String> {
    let mut args = env::args().skip(1);
    let mut chakra_lib = None;
    let mut script_path = None;
    let mut repl = false;

    while let Some(arg) = args.next() {
        match arg.as_str() {
            "-h" | "--help" => {
                print_usage();
                return Err(String::new());
            }
            "--chakra-lib" => {
                let value = args
                    .next()
                    .ok_or_else(|| "--chakra-lib expects a path argument".to_string())?;
                chakra_lib = Some(PathBuf::from(value));
            }
            "--repl" | "-i" => {
                repl = true;
            }
            _ => {
                if script_path.is_none() {
                    script_path = Some(PathBuf::from(arg));
                } else {
                    return Err("Only one script path is supported currently.".to_string());
                }
            }
        }
    }

    if script_path.is_none() && !repl {
        return Err("Missing script path. Use --help to see usage, or pass --repl to start the interactive shell.".to_string());
    }

    Ok(CliArgs {
        chakra_lib,
        script_path,
        repl,
    })
}

fn print_usage() {
    println!("chakra_runtime - cross-platform Rust ChakraCore host");
    println!("Usage:");
    println!("  chakra_runtime [--chakra-lib <path-to-ChakraCore-shared-library>] <script.js>");
    println!("  chakra_runtime --repl");
}

fn run() -> Result<(), String> {
    let cli = match parse_cli_args() {
        Ok(value) => value,
        Err(message) if message.is_empty() => return Ok(()),
        Err(message) => return Err(message),
    };

    let runtime = HostRuntime::create(cli.chakra_lib.as_deref())?;

    if cli.repl || cli.script_path.is_none() {
        return runtime.run_repl();
    }

    runtime.run_script_file(cli.script_path.as_ref().unwrap())
}

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            if !error.trim().is_empty() {
                eprintln!("{}", error);
            }
            ExitCode::from(1)
        }
    }
}
