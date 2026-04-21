use rustyline::completion::{Completer, Pair};
use rustyline::error::ReadlineError;
use rustyline::highlight::Highlighter;
use rustyline::hint::Hinter;
use rustyline::validate::{ValidationContext, ValidationResult, Validator};
use rustyline::{Context as RustylineContext, Editor, Helper};
use libloading::{Library, Symbol};
use std::cell::RefCell;
use std::borrow::Cow;
use std::collections::{HashMap, VecDeque};
use std::env;
use std::ffi::c_void;
use std::fmt::Write as _;
use std::fs;
use std::io::IsTerminal;
use std::path::{Path, PathBuf};
use std::process::ExitCode;
use std::ptr;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::time::Instant;

// ─── JSRT type aliases ──────────────────────────────────────────────────────

type JsErrorCode         = i32;
type JsRuntimeHandle     = *mut c_void;
type JsContextRef        = *mut c_void;
type JsValueRef          = *mut c_void;
type JsPropertyIdRef     = *mut c_void;
type JsSourceContext     = usize;
type JsRuntimeAttributes = u32;
type JsParseScriptAttributes = u32;
type JsValueType         = u32;

// ─── JSRT constants ─────────────────────────────────────────────────────────

const JS_NO_ERROR: JsErrorCode          = 0;
const JS_ERROR_SCRIPT_EXCEPTION: JsErrorCode = 0x30001;
const JS_ERROR_SCRIPT_COMPILE: JsErrorCode   = 0x30002;
const JS_RUNTIME_ATTRIBUTE_NONE: JsRuntimeAttributes     = 0;
const JS_PARSE_SCRIPT_ATTRIBUTE_NONE: JsParseScriptAttributes = 0;
const JS_VALUE_TYPE_UNDEFINED: JsValueType = 0;

// ─── Calling-convention shims ────────────────────────────────────────────────
// All fn-pointer types are defined once per platform to avoid repetition.

macro_rules! js_fn {
    ($name:ident ( $($arg:ty),* ) -> $ret:ty) => {
        #[cfg(target_os = "windows")]
        type $name = unsafe extern "system" fn($($arg),*) -> $ret;
        #[cfg(not(target_os = "windows"))]
        type $name = unsafe extern "C" fn($($arg),*) -> $ret;
    };
}

js_fn!(JsNativeFunctionRaw(JsValueRef, bool, *mut JsValueRef, u16, *mut c_void) -> JsValueRef);
type JsNativeFunction = Option<JsNativeFunctionRaw>;

#[cfg(target_os = "windows")]
type JsPromiseContinuationCallbackRaw = unsafe extern "system" fn(JsValueRef, *mut c_void);
#[cfg(not(target_os = "windows"))]
type JsPromiseContinuationCallbackRaw = unsafe extern "C" fn(JsValueRef, *mut c_void);
type JsPromiseContinuationCallback = Option<JsPromiseContinuationCallbackRaw>;

js_fn!(JsCreateRuntimeFn    (JsRuntimeAttributes, *mut c_void, *mut JsRuntimeHandle) -> JsErrorCode);
js_fn!(JsDisposeRuntimeFn   (JsRuntimeHandle) -> JsErrorCode);
js_fn!(JsCreateContextFn    (JsRuntimeHandle, *mut JsContextRef) -> JsErrorCode);
js_fn!(JsSetCurrentContextFn(JsContextRef) -> JsErrorCode);
js_fn!(JsCreateStringFn     (*const u8, usize, *mut JsValueRef) -> JsErrorCode);
js_fn!(JsRunFn              (JsValueRef, JsSourceContext, JsValueRef, JsParseScriptAttributes, *mut JsValueRef) -> JsErrorCode);
js_fn!(JsGetAndClearExceptionFn(*mut JsValueRef) -> JsErrorCode);
js_fn!(JsConvertValueToStringFn(JsValueRef, *mut JsValueRef) -> JsErrorCode);
js_fn!(JsCopyStringFn       (JsValueRef, *mut i8, usize, *mut usize) -> JsErrorCode);
js_fn!(JsGetGlobalObjectFn  (*mut JsValueRef) -> JsErrorCode);
js_fn!(JsCreateFunctionFn   (JsNativeFunction, *mut c_void, *mut JsValueRef) -> JsErrorCode);
js_fn!(JsCreateObjectFn     (*mut JsValueRef) -> JsErrorCode);
js_fn!(JsCreatePropertyIdFn (*const u8, usize, *mut JsPropertyIdRef) -> JsErrorCode);
js_fn!(JsSetPropertyFn      (JsValueRef, JsPropertyIdRef, JsValueRef, bool) -> JsErrorCode);
js_fn!(JsGetUndefinedValueFn(*mut JsValueRef) -> JsErrorCode);
js_fn!(JsGetValueTypeFn     (JsValueRef, *mut JsValueType) -> JsErrorCode);
js_fn!(JsCallFunctionFn     (JsValueRef, *mut JsValueRef, u16, *mut JsValueRef) -> JsErrorCode);
js_fn!(JsAddRefFn           (JsValueRef, *mut u32) -> JsErrorCode);
js_fn!(JsReleaseFn          (JsValueRef, *mut u32) -> JsErrorCode);
js_fn!(JsSetPromiseContinuationCallbackFn(JsPromiseContinuationCallback, *mut c_void) -> JsErrorCode);
js_fn!(JsChakraEs2021TransformFn(JsValueRef, *mut JsValueRef) -> JsErrorCode);

// ─── ChakraApi ───────────────────────────────────────────────────────────────

struct ChakraApi {
    _library: Library,
    js_create_runtime:             JsCreateRuntimeFn,
    js_dispose_runtime:            JsDisposeRuntimeFn,
    js_create_context:             JsCreateContextFn,
    js_set_current_context:        JsSetCurrentContextFn,
    js_create_string:              JsCreateStringFn,
    js_run:                        JsRunFn,
    js_get_and_clear_exception:    JsGetAndClearExceptionFn,
    js_convert_value_to_string:    JsConvertValueToStringFn,
    js_copy_string:                JsCopyStringFn,
    js_get_global_object:          JsGetGlobalObjectFn,
    js_create_function:            JsCreateFunctionFn,
    js_create_object:              JsCreateObjectFn,
    js_create_property_id:         JsCreatePropertyIdFn,
    js_set_property:               JsSetPropertyFn,
    js_get_undefined_value:        JsGetUndefinedValueFn,
    js_get_value_type:             JsGetValueTypeFn,
    js_call_function:              JsCallFunctionFn,
    js_add_ref:                    JsAddRefFn,
    js_release:                    JsReleaseFn,
    js_set_promise_continuation_callback: Option<JsSetPromiseContinuationCallbackFn>,
    js_chakra_es2021_transform:    Option<JsChakraEs2021TransformFn>,
}

// ─── Console method kinds ────────────────────────────────────────────────────

#[derive(Copy, Clone, Debug)]
enum ConsoleMethodKind {
    Log,
    Info,
    Warn,
    Error,
    Debug,
    Dir,
    Trace,
    Assert,
    // counter variants store their label index into a shared AtomicUsize pool
    Count,
    CountReset,
    // timer variants
    Time,
    TimeEnd,
    TimeLog,
    // grouping
    Group,
    GroupEnd,
    // table
    Table,
    // clear terminal
    Clear,
}

// ─── Callback state structs ──────────────────────────────────────────────────

struct PrintCallbackState {
    api: *const ChakraApi,
}

struct PromiseQueueState {
    api: *const ChakraApi,
    pending: RefCell<VecDeque<JsValueRef>>,
}

impl PromiseQueueState {
    fn new() -> Self {
        Self {
            api: ptr::null(),
            pending: RefCell::new(VecDeque::new()),
        }
    }
}

// Shared state threaded through all console callbacks via a raw pointer
// to a ConsoleSharedState that lives in HostRuntime.
struct ConsoleSharedState {
    counters:    HashMap<String, u64>,
    timers:      HashMap<String, Instant>,
    group_depth: usize,
}

impl ConsoleSharedState {
    fn new() -> Self {
        Self {
            counters:    HashMap::new(),
            timers:      HashMap::new(),
            group_depth: 0,
        }
    }

    fn indent(&self) -> String {
        "  ".repeat(self.group_depth)
    }
}

// Extended callback state that also holds a pointer to shared state
struct ConsoleCallbackStateEx {
    api:    *const ChakraApi,
    kind:   ConsoleMethodKind,
    shared: *mut ConsoleSharedState,
    color_enabled: bool,
}

// ─── HostRuntime ─────────────────────────────────────────────────────────────

struct HostRuntime {
    api:                    ChakraApi,
    runtime:                JsRuntimeHandle,
    color_enabled:          bool,
    promise_state:          Box<PromiseQueueState>,
    // keep alive
    _print_state:           Box<PrintCallbackState>,
    _console_states:        Vec<Box<ConsoleCallbackStateEx>>,
    console_shared:         Box<ConsoleSharedState>,
}

// ─── ReplHelper (rustyline) ──────────────────────────────────────────────────

struct ReplHelper {
    keywords: &'static [&'static str],
    color_enabled: bool,
}

fn supports_color() -> bool {
    if env::var_os("NO_COLOR").is_some() {
        return false;
    }

    if env::var_os("FORCE_COLOR").is_some() {
        return true;
    }

    if !std::io::stdout().is_terminal() && !std::io::stderr().is_terminal() {
        return false;
    }

    if cfg!(windows) {
        if env::var_os("WT_SESSION").is_some() || env::var_os("ANSICON").is_some() {
            return true;
        }

        if env::var("ConEmuANSI").map(|value| value.eq_ignore_ascii_case("ON")).unwrap_or(false) {
            return true;
        }

        if let Ok(term) = env::var("TERM") {
            let term = term.to_ascii_lowercase();
            if term.contains("xterm") || term.contains("ansi") || term.contains("vscode") || term.contains("cygwin") {
                return true;
            }
        }

        return false;
    }

    true
}

fn colorize(enabled: bool, code: &str, text: &str) -> String {
    if enabled {
        format!("\x1b[{}m{}\x1b[0m", code, text)
    } else {
        text.to_string()
    }
}

fn clear_terminal(enabled: bool) {
    if enabled {
        print!("\x1b[2J\x1b[H");
    } else {
        print!("\n");
    }
}

impl Helper for ReplHelper {}

impl Completer for ReplHelper {
    type Candidate = Pair;
    fn complete(&self, _line: &str, _pos: usize, _ctx: &RustylineContext<'_>)
        -> rustyline::Result<(usize, Vec<Pair>)> { Ok((0, vec![])) }
}

impl Hinter for ReplHelper {
    type Hint = String;
    fn hint(&self, _line: &str, _pos: usize, _ctx: &RustylineContext<'_>) -> Option<String> { None }
}

impl Validator for ReplHelper {
    fn validate(&self, _ctx: &mut ValidationContext<'_>) -> rustyline::Result<ValidationResult> {
        Ok(ValidationResult::Valid(None))
    }
}

impl Highlighter for ReplHelper {
    fn highlight<'l>(&self, line: &'l str, _pos: usize) -> Cow<'l, str> {
        Cow::Owned(highlight_js_line(line, self.keywords, self.color_enabled))
    }
    fn highlight_prompt<'b, 's: 'b, 'p: 'b>(&'s self, prompt: &'p str, _default: bool) -> Cow<'b, str> {
        if self.color_enabled {
            Cow::Owned(format!("\x1b[1;32m{}\x1b[0m", prompt))
        } else {
            Cow::Borrowed(prompt)
        }
    }
    fn highlight_hint<'h>(&self, hint: &'h str) -> Cow<'h, str> {
        if self.color_enabled {
            Cow::Owned(format!("\x1b[2m{}\x1b[0m", hint))
        } else {
            Cow::Borrowed(hint)
        }
    }
}

// ─── print() native callback ─────────────────────────────────────────────────

#[cfg(target_os = "windows")]
unsafe extern "system" fn print_callback(
    _: JsValueRef, _: bool, args: *mut JsValueRef, argc: u16, state: *mut c_void,
) -> JsValueRef { print_callback_impl(args, argc, state) }

#[cfg(not(target_os = "windows"))]
unsafe extern "C" fn print_callback(
    _: JsValueRef, _: bool, args: *mut JsValueRef, argc: u16, state: *mut c_void,
) -> JsValueRef { print_callback_impl(args, argc, state) }

unsafe fn print_callback_impl(args: *mut JsValueRef, argc: u16, state: *mut c_void) -> JsValueRef {
    if state.is_null() { return ptr::null_mut(); }
    let s = &*(state as *const PrintCallbackState);
    let api = &*s.api;
    let text = collect_args_as_string(api, args, argc, 1, " ");
    println!("{}", text);
    get_undefined(api)
}

#[cfg(target_os = "windows")]
unsafe extern "system" fn promise_continuation_callback(task: JsValueRef, state: *mut c_void) {
    promise_continuation_callback_impl(task, state)
}

#[cfg(not(target_os = "windows"))]
unsafe extern "C" fn promise_continuation_callback(task: JsValueRef, state: *mut c_void) {
    promise_continuation_callback_impl(task, state)
}

const PROMISE_CONTINUATION_CB: JsPromiseContinuationCallback = Some(promise_continuation_callback);

unsafe fn promise_continuation_callback_impl(task: JsValueRef, state: *mut c_void) {
    if task.is_null() || state.is_null() {
        return;
    }

    let promise_state = &*(state as *const PromiseQueueState);
    let api = &*promise_state.api;
    let _ = (api.js_add_ref)(task, ptr::null_mut());
    promise_state.pending.borrow_mut().push_back(task);
}

// ─── console.* native callback ───────────────────────────────────────────────

#[cfg(target_os = "windows")]
unsafe extern "system" fn console_callback(
    _: JsValueRef, _: bool, args: *mut JsValueRef, argc: u16, state: *mut c_void,
) -> JsValueRef { console_callback_impl(args, argc, state) }

#[cfg(not(target_os = "windows"))]
unsafe extern "C" fn console_callback(
    _: JsValueRef, _: bool, args: *mut JsValueRef, argc: u16, state: *mut c_void,
) -> JsValueRef { console_callback_impl(args, argc, state) }

const CONSOLE_CB: JsNativeFunction = Some(console_callback);

unsafe fn console_callback_impl(args: *mut JsValueRef, argc: u16, state: *mut c_void) -> JsValueRef {
    if state.is_null() { return ptr::null_mut(); }
    let s     = &*(state as *const ConsoleCallbackStateEx);
    let api   = &*s.api;
    let shared = &mut *s.shared;
    let indent = shared.indent();

    match s.kind {
        // ── log / info / debug ───────────────────────────────────────────────
        ConsoleMethodKind::Log | ConsoleMethodKind::Info | ConsoleMethodKind::Debug => {
            let text = collect_args_as_string(api, args, argc, 1, " ");
            println!("{}{}", indent, text);
        }

        // ── warn ─────────────────────────────────────────────────────────────
        ConsoleMethodKind::Warn => {
            let text = collect_args_as_string(api, args, argc, 1, " ");
            eprintln!("{}{} {}", indent, colorize(s.color_enabled, "33", "[warn]"), text);
        }

        // ── error ─────────────────────────────────────────────────────────────
        ConsoleMethodKind::Error => {
            let text = collect_args_as_string(api, args, argc, 1, " ");
            eprintln!("{}{} {}", indent, colorize(s.color_enabled, "31", "[error]"), text);
        }

        // ── trace ─────────────────────────────────────────────────────────────
        ConsoleMethodKind::Trace => {
            let text = if argc > 1 {
                collect_args_as_string(api, args, argc, 1, " ")
            } else {
                "Trace".to_string()
            };
            eprintln!("{}{} {}", indent, colorize(s.color_enabled, "35", "[trace]"), text);
            // A real stack trace would require JsGetStackTrace — print a note instead
            eprintln!("{}  (stack trace not available in this host)", indent);
        }

        // ── assert ────────────────────────────────────────────────────────────
        ConsoleMethodKind::Assert => {
            // First argument (index 1) is the condition; rest is the message
            let condition = if argc > 1 {
                is_truthy(api, *args.add(1))
            } else {
                false
            };
            if !condition {
                let msg = if argc > 2 {
                    collect_args_as_string(api, args, argc, 2, " ")
                } else {
                    "Assertion failed".to_string()
                };
                eprintln!("{}{} {}", indent, colorize(s.color_enabled, "31", "[assert]"), msg);
            }
        }

        // ── dir ───────────────────────────────────────────────────────────────
        ConsoleMethodKind::Dir => {
            if argc > 1 {
                let text = value_to_string(api, *args.add(1)).unwrap_or_else(|e| format!("<{}>", e));
                println!("{}{}", indent, text);
            }
        }

        // ── table ─────────────────────────────────────────────────────────────
        ConsoleMethodKind::Table => {
            // Best-effort: stringify the value and print it
            if argc > 1 {
                let text = value_to_string(api, *args.add(1)).unwrap_or_else(|e| format!("<{}>", e));
                println!("{}{} {}", indent, colorize(s.color_enabled, "1", "[table]"), text);
            }
        }

        // ── count ─────────────────────────────────────────────────────────────
        ConsoleMethodKind::Count => {
            let label = if argc > 1 {
                value_to_string(api, *args.add(1)).unwrap_or_else(|_| "default".to_string())
            } else {
                "default".to_string()
            };
            let entry = shared.counters.entry(label.clone()).or_insert(0);
            *entry += 1;
            println!("{}{}: {}", indent, label, *entry);
        }

        ConsoleMethodKind::CountReset => {
            let label = if argc > 1 {
                value_to_string(api, *args.add(1)).unwrap_or_else(|_| "default".to_string())
            } else {
                "default".to_string()
            };
            shared.counters.insert(label.clone(), 0);
            println!("{}{}: 0", indent, label);
        }

        // ── time ──────────────────────────────────────────────────────────────
        ConsoleMethodKind::Time => {
            let label = if argc > 1 {
                value_to_string(api, *args.add(1)).unwrap_or_else(|_| "default".to_string())
            } else {
                "default".to_string()
            };
            shared.timers.insert(label, Instant::now());
        }

        ConsoleMethodKind::TimeEnd => {
            let label = if argc > 1 {
                value_to_string(api, *args.add(1)).unwrap_or_else(|_| "default".to_string())
            } else {
                "default".to_string()
            };
            if let Some(start) = shared.timers.remove(&label) {
                let elapsed = start.elapsed();
                println!("{}{}: {:.3}ms", indent, label, elapsed.as_secs_f64() * 1000.0);
            } else {
                eprintln!("{}{} No such timer: '{}'", indent, colorize(s.color_enabled, "33", "[timer]"), label);
            }
        }

        ConsoleMethodKind::TimeLog => {
            let label = if argc > 1 {
                value_to_string(api, *args.add(1)).unwrap_or_else(|_| "default".to_string())
            } else {
                "default".to_string()
            };
            if let Some(start) = shared.timers.get(&label) {
                let elapsed = start.elapsed();
                println!("{}{}: {:.3}ms", indent, label, elapsed.as_secs_f64() * 1000.0);
            } else {
                eprintln!("{}{} No such timer: '{}'", indent, colorize(s.color_enabled, "33", "[timer]"), label);
            }
        }

        // ── group ─────────────────────────────────────────────────────────────
        ConsoleMethodKind::Group => {
            let label = if argc > 1 {
                collect_args_as_string(api, args, argc, 1, " ")
            } else {
                String::new()
            };
            if !label.is_empty() {
                println!("{}{}", indent, colorize(s.color_enabled, "1", &label));
            }
            shared.group_depth += 1;
        }

        ConsoleMethodKind::GroupEnd => {
            if shared.group_depth > 0 {
                shared.group_depth -= 1;
            }
        }

        // ── clear ─────────────────────────────────────────────────────────────
        ConsoleMethodKind::Clear => {
            // ANSI clear screen + move cursor to top
            clear_terminal(s.color_enabled);
        }
    }

    get_undefined(api)
}

// ─── Helpers called from callbacks ───────────────────────────────────────────

/// Collect arguments [start_idx..argc) into a single string, joined by sep.
unsafe fn collect_args_as_string(
    api: &ChakraApi,
    args: *mut JsValueRef,
    argc: u16,
    start_idx: usize,
    sep: &str,
) -> String {
    let mut out = String::new();
    for i in start_idx..(argc as usize) {
        if !out.is_empty() { out.push_str(sep); }
        match value_to_string(api, *args.add(i)) {
            Ok(s)  => out.push_str(&s),
            Err(e) => { let _ = write!(out, "<toString failed: {}>", e); }
        }
    }
    out
}

/// Return the JS undefined value, or null on error.
unsafe fn get_undefined(api: &ChakraApi) -> JsValueRef {
    let mut v = ptr::null_mut();
    (api.js_get_undefined_value)(&mut v);
    v
}

/// Cheap truthiness check — if JsGetValueType fails we treat it as falsy.
unsafe fn is_truthy(api: &ChakraApi, value: JsValueRef) -> bool {
    // Simplest approach: convert to string and check
    match value_to_string(api, value) {
        Ok(s) => !matches!(s.as_str(), "false" | "0" | "" | "null" | "undefined" | "NaN"),
        Err(_) => false,
    }
}

// ─── ChakraApi::load ─────────────────────────────────────────────────────────

impl ChakraApi {
    fn load(path_hint: Option<&Path>) -> Result<Self, String> {
        let candidates = chakra_library_candidates(path_hint);
        let mut last_error = String::new();
        for candidate in &candidates {
            match unsafe { Library::new(candidate) } {
                Ok(lib) => return unsafe { Self::from_library(lib) },
                Err(e)  => last_error = format!("{}: {}", candidate.display(), e),
            }
        }
        if candidates.is_empty() {
            Err("No ChakraCore shared library candidates found.".into())
        } else {
            Err(format!("Unable to load ChakraCore shared library. Last error: {}", last_error))
        }
    }

    unsafe fn from_library(library: Library) -> Result<Self, String> {
        macro_rules! req {
            ($sym:literal as $ty:ty) => {{
                let s: Symbol<$ty> = library.get($sym)
                    .map_err(|e| format!("Missing symbol {}: {}", String::from_utf8_lossy($sym), e))?;
                *s
            }};
        }
        macro_rules! opt {
            ($sym:literal as $ty:ty) => {
                library.get::<$ty>($sym).ok().map(|s| *s)
            };
        }

        Ok(Self {
            js_create_runtime:          req!(b"JsCreateRuntime"          as JsCreateRuntimeFn),
            js_dispose_runtime:         req!(b"JsDisposeRuntime"         as JsDisposeRuntimeFn),
            js_create_context:          req!(b"JsCreateContext"          as JsCreateContextFn),
            js_set_current_context:     req!(b"JsSetCurrentContext"      as JsSetCurrentContextFn),
            js_create_string:           req!(b"JsCreateString"           as JsCreateStringFn),
            js_run:                     req!(b"JsRun"                    as JsRunFn),
            js_get_and_clear_exception: req!(b"JsGetAndClearException"   as JsGetAndClearExceptionFn),
            js_convert_value_to_string: req!(b"JsConvertValueToString"   as JsConvertValueToStringFn),
            js_copy_string:             req!(b"JsCopyString"             as JsCopyStringFn),
            js_get_global_object:       req!(b"JsGetGlobalObject"        as JsGetGlobalObjectFn),
            js_create_function:         req!(b"JsCreateFunction"         as JsCreateFunctionFn),
            js_create_object:           req!(b"JsCreateObject"           as JsCreateObjectFn),
            js_create_property_id:      req!(b"JsCreatePropertyId"       as JsCreatePropertyIdFn),
            js_set_property:            req!(b"JsSetProperty"            as JsSetPropertyFn),
            js_get_undefined_value:     req!(b"JsGetUndefinedValue"      as JsGetUndefinedValueFn),
            js_get_value_type:          req!(b"JsGetValueType"           as JsGetValueTypeFn),
            js_call_function:           req!(b"JsCallFunction"           as JsCallFunctionFn),
            js_add_ref:                 req!(b"JsAddRef"                 as JsAddRefFn),
            js_release:                 req!(b"JsRelease"                as JsReleaseFn),
            js_set_promise_continuation_callback:
                                        opt!(b"JsSetPromiseContinuationCallback" as JsSetPromiseContinuationCallbackFn),
            js_chakra_es2021_transform: opt!(b"JsChakraEs2021Transform"  as JsChakraEs2021TransformFn),
            _library: library,
        })
    }
}

// ─── HostRuntime impl ────────────────────────────────────────────────────────

impl HostRuntime {
    /// Returns a `Box<HostRuntime>` (heap-pinned) so that the raw `*const ChakraApi`
    /// pointers stored in every callback state struct never go stale.
    ///
    /// Root cause of the original crash:
    ///   `PrintCallbackState { api: &api }` was created before `api` was moved into
    ///   the `HostRuntime` struct, and then again the struct itself was moved when
    ///   returned from `create()`.  Each move invalidated the raw pointer, so the
    ///   first `print()` / `console.log()` dereference caused STATUS_ACCESS_VIOLATION.
    ///
    /// Fix: build a `Box<HostRuntime>` with placeholder states, then — while the
    /// heap address is stable — overwrite the states with pointers derived from
    /// `&host.api` and `&host.console_shared`, which will never move again.
    fn create(chakra_library_hint: Option<&Path>) -> Result<Box<Self>, String> {
        let api = ChakraApi::load(chakra_library_hint)?;

        let mut runtime = ptr::null_mut();
        ensure_js_ok(
            unsafe { (api.js_create_runtime)(JS_RUNTIME_ATTRIBUTE_NONE, ptr::null_mut(), &mut runtime) },
            "JsCreateRuntime",
        )?;

        let mut context = ptr::null_mut();
        if unsafe { (api.js_create_context)(runtime, &mut context) } != JS_NO_ERROR {
            unsafe { (api.js_dispose_runtime)(runtime); }
            return Err("JsCreateContext failed".into());
        }
        if unsafe { (api.js_set_current_context)(context) } != JS_NO_ERROR {
            unsafe { (api.js_dispose_runtime)(runtime); }
            return Err("JsSetCurrentContext failed".into());
        }

        // ── Step 1: allocate on the heap with placeholder (null) api pointers.
        //    From this point on `host` will NOT move — all &-borrows below are
        //    into stable heap memory.
        let mut host = Box::new(Self {
            api,
            runtime,
            color_enabled: supports_color(),
            promise_state: Box::new(PromiseQueueState::new()),
            // Placeholder — real pointer patched in step 2.
            _print_state: Box::new(PrintCallbackState { api: ptr::null() }),
            _console_states: Vec::new(),
            console_shared: Box::new(ConsoleSharedState::new()),
        });

        // ── Step 2: now that `host` is at a fixed heap address, capture stable
        //    raw pointers to its fields and patch the states.
        let api_ptr: *const ChakraApi      = &host.api;
        let shared_ptr: *mut ConsoleSharedState = host.console_shared.as_mut();

        host._print_state = Box::new(PrintCallbackState { api: api_ptr });

        // ── Step 3: install globals (uses the already-stable api_ptr / shared_ptr).
        host.promise_state.api = api_ptr;
        host.install_print_with(api_ptr)?;
        host.install_console_with(api_ptr, shared_ptr)?;
        host.install_promise_continuation()?;

        Ok(host)
    }

    // ── script execution ─────────────────────────────────────────────────────

    fn run_script_file(&self, path: &Path) -> Result<(), String> {
        let source = fs::read_to_string(path)
            .map_err(|e| format!("Failed to read '{}': {}", path.display(), e))?;
        let label = path.to_string_lossy().into_owned();
        self.run_script_source(&source, &label)?;
        Ok(())
    }

    fn run_script_source(&self, source: &str, label: &str) -> Result<JsValueRef, String> {
        let transformed = self.maybe_transform_entry_source(source);
        let script_val  = self.create_js_string(&transformed)?;
        let label_val   = self.create_js_string(label)?;

        let mut result = ptr::null_mut();
        let rc = unsafe {
            (self.api.js_run)(script_val, 0, label_val, JS_PARSE_SCRIPT_ATTRIBUTE_NONE, &mut result)
        };
        if rc != JS_NO_ERROR {
            return Err(self.report_script_failure("JsRun", rc).unwrap_err());
        }
        self.drain_promise_queue()?;
        Ok(result)
    }

    fn maybe_transform_entry_source(&self, source: &str) -> String {
        if !should_try_es2021_transform(source) { return source.into(); }
        let Some(tfn) = self.api.js_chakra_es2021_transform else { return source.into(); };
        let Ok(sv) = self.create_js_string(source) else { return source.into(); };
        let mut out = ptr::null_mut();
        let rc = unsafe { tfn(sv, &mut out) };
        if rc != JS_NO_ERROR {
            eprintln!("warning: ES2021 transform failed, running original source.");
            return source.into();
        }
        match value_to_string(&self.api, out) {
            Ok(s) if !s.is_empty() => s,
            _ => source.into(),
        }
    }

    fn install_promise_continuation(&self) -> Result<(), String> {
        let Some(set_callback) = self.api.js_set_promise_continuation_callback else {
            eprintln!("warning: JsSetPromiseContinuationCallback not available in this build");
            return Ok(());
        };

        ensure_js_ok(
            unsafe {
                set_callback(
                    PROMISE_CONTINUATION_CB,
                    self.promise_state.as_ref() as *const PromiseQueueState as *mut c_void,
                )
            },
            "JsSetPromiseContinuationCallback",
        )
    }

    fn drain_promise_queue(&self) -> Result<(), String> {
        loop {
            let task = self.promise_state.pending.borrow_mut().pop_front();
            let Some(task) = task else {
                break;
            };

            let mut args = [unsafe { get_undefined(&self.api) }];
            let mut result = ptr::null_mut();
            let rc = unsafe {
                (self.api.js_call_function)(task, args.as_mut_ptr(), 1, &mut result)
            };
            let _ = unsafe { (self.api.js_release)(task, ptr::null_mut()) };

            if rc != JS_NO_ERROR {
                return Err(self.report_script_failure("JsCallFunction(PromiseContinuation)", rc).unwrap_err());
            }
        }

        Ok(())
    }

    // ── global function installers ───────────────────────────────────────────
    // Both methods receive raw pointers captured *after* HostRuntime was
    // heap-allocated in `create()`.  That guarantees they stay valid for the
    // entire lifetime of the host — no move can invalidate them.

    fn install_print_with(&mut self, api_ptr: *const ChakraApi) -> Result<(), String> {
        self._print_state.api = api_ptr;
        let global = self.get_global()?;
        let mut f = ptr::null_mut();
        ensure_js_ok(unsafe {
            (self.api.js_create_function)(
                Some(print_callback),
                self._print_state.as_mut() as *mut PrintCallbackState as *mut c_void,
                &mut f,
            )
        }, "JsCreateFunction(print)")?;
        self.set_property_on(global, "print", f)
    }

    fn install_console_with(
        &mut self,
        api_ptr: *const ChakraApi,
        shared_ptr: *mut ConsoleSharedState,
    ) -> Result<(), String> {
        let global = self.get_global()?;
        let mut obj = ptr::null_mut();
        ensure_js_ok(
            unsafe { (self.api.js_create_object)(&mut obj) },
            "JsCreateObject(console)",
        )?;

        let methods: &[(&str, ConsoleMethodKind)] = &[
            ("log",            ConsoleMethodKind::Log),
            ("info",           ConsoleMethodKind::Info),
            ("warn",           ConsoleMethodKind::Warn),
            ("error",          ConsoleMethodKind::Error),
            ("debug",          ConsoleMethodKind::Debug),
            ("dir",            ConsoleMethodKind::Dir),
            ("trace",          ConsoleMethodKind::Trace),
            ("assert",         ConsoleMethodKind::Assert),
            ("table",          ConsoleMethodKind::Table),
            ("count",          ConsoleMethodKind::Count),
            ("countReset",     ConsoleMethodKind::CountReset),
            ("time",           ConsoleMethodKind::Time),
            ("timeEnd",        ConsoleMethodKind::TimeEnd),
            ("timeLog",        ConsoleMethodKind::TimeLog),
            ("group",          ConsoleMethodKind::Group),
            ("groupCollapsed", ConsoleMethodKind::Group),
            ("groupEnd",       ConsoleMethodKind::GroupEnd),
            ("clear",          ConsoleMethodKind::Clear),
        ];

        for &(name, kind) in methods {
            let state = Box::new(ConsoleCallbackStateEx {
                api: api_ptr,
                kind,
                shared: shared_ptr,
                color_enabled: self.color_enabled,
            });
            let cb_ptr = state.as_ref() as *const ConsoleCallbackStateEx as *mut c_void;
            self._console_states.push(state);

            let mut f = ptr::null_mut();
            ensure_js_ok(
                unsafe { (self.api.js_create_function)(CONSOLE_CB, cb_ptr, &mut f) },
                &format!("JsCreateFunction(console.{})", name),
            )?;
            self.set_property_on(obj, name, f)?;
        }

        self.set_property_on(global, "console", obj)
    }

    // ── helpers ──────────────────────────────────────────────────────────────

    fn get_global(&self) -> Result<JsValueRef, String> {
        let mut g = ptr::null_mut();
        ensure_js_ok(unsafe { (self.api.js_get_global_object)(&mut g) }, "JsGetGlobalObject")?;
        Ok(g)
    }

    fn set_property_on(&self, obj: JsValueRef, name: &str, value: JsValueRef) -> Result<(), String> {
        let pid = self.create_property_id(name)?;
        ensure_js_ok(
            unsafe { (self.api.js_set_property)(obj, pid, value, true) },
            &format!("JsSetProperty({})", name),
        )
    }

    fn create_js_string(&self, text: &str) -> Result<JsValueRef, String> {
        let mut v = ptr::null_mut();
        ensure_js_ok(
            unsafe { (self.api.js_create_string)(text.as_ptr(), text.len(), &mut v) },
            "JsCreateString",
        )?;
        Ok(v)
    }

    fn create_property_id(&self, name: &str) -> Result<JsPropertyIdRef, String> {
        let mut pid = ptr::null_mut();
        ensure_js_ok(
            unsafe { (self.api.js_create_property_id)(name.as_ptr(), name.len(), &mut pid) },
            "JsCreatePropertyId",
        )?;
        Ok(pid)
    }

    fn report_script_failure(&self, op: &str, code: JsErrorCode) -> Result<(), String> {
        Err(if let Some(msg) = self.describe_current_exception() {
            format!("{} failed ({})\nJavaScript exception: {}", op, error_name(code), msg)
        } else {
            format_js_error(op, code)
        })
    }

    fn describe_current_exception(&self) -> Option<String> {
        let mut ex = ptr::null_mut();
        let rc = unsafe { (self.api.js_get_and_clear_exception)(&mut ex) };
        if rc != JS_NO_ERROR || ex.is_null() { return None; }
        value_to_string(&self.api, ex).ok()
    }

    // ── REPL ─────────────────────────────────────────────────────────────────

    fn run_repl(&self) -> Result<(), String> {
        let helper = ReplHelper { keywords: JS_KEYWORDS, color_enabled: self.color_enabled };
        let mut editor: Editor<ReplHelper, rustyline::history::DefaultHistory> =
            Editor::new().map_err(|e| format!("Editor init failed: {}", e))?;
        editor.set_helper(Some(helper));
        let _ = editor.load_history(".chakra_history");

        println!("{}", colorize(self.color_enabled, "1;36", "ChakraCore REPL"));
        println!("Type {} for available commands, {} or Ctrl-D to quit.\n",
            colorize(self.color_enabled, "1", ".help"),
            colorize(self.color_enabled, "1", ".exit"));

        // Unique monotonic source context
        static CTX: AtomicUsize = AtomicUsize::new(1);

        let mut buffer = String::new();

        loop {
            let prompt = if buffer.is_empty() { "chakra> " } else { "  ...> " };
            match editor.readline(prompt) {
                Ok(line) => {
                    let trimmed = line.trim();

                    // ── REPL dot-commands ──────────────────────────────────
                    if buffer.is_empty() && trimmed.starts_with('.') {
                        match self.handle_repl_command(trimmed, &mut editor) {
                            ReplCommand::Exit     => break,
                            ReplCommand::Handled  => continue,
                            ReplCommand::Unknown  => {
                                eprintln!("{}  (try .help)", colorize(self.color_enabled, "33", &format!("Unknown command: {}", trimmed)));
                                continue;
                            }
                        }
                    }

                    if buffer.is_empty() && trimmed.is_empty() { continue; }

                    buffer.push_str(&line);
                    buffer.push('\n');

                    if js_source_needs_more_input(&buffer) { continue; }

                    let submitted = buffer.trim_end().to_string();
                    buffer.clear();
                    if submitted.is_empty() { continue; }

                    let _ = editor.add_history_entry(&submitted);

                    let ctx_id = CTX.fetch_add(1, Ordering::Relaxed);
                    let label  = format!("<repl:{}>", ctx_id);

                    match self.run_script_source(&submitted, &label) {
                        Ok(result) => {
                            if !is_undefined_value(&self.api, result) {
                                match value_to_string(&self.api, result) {
                                    Ok(s) if !s.is_empty() => println!("{}", colorize(self.color_enabled, "1;36", &s)),
                                    Ok(_)  => {}
                                    Err(e) => eprintln!("result error: {}", e),
                                }
                            }
                        }
                        Err(e) => eprintln!("{}", colorize(self.color_enabled, "31", &e)),
                    }
                }

                Err(ReadlineError::Interrupted) => {
                    println!("^C");
                    buffer.clear();
                }
                Err(ReadlineError::Eof) => break,
                Err(e) => return Err(format!("Editor error: {}", e)),
            }
        }

        let _ = editor.save_history(".chakra_history");
        println!("{}", colorize(self.color_enabled, "2", "Bye!"));
        Ok(())
    }

    fn handle_repl_command(
        &self,
        line: &str,
        editor: &mut Editor<ReplHelper, rustyline::history::DefaultHistory>,
    ) -> ReplCommand {
        // Split into command word and optional argument
        let (cmd, arg) = line.split_once(char::is_whitespace)
            .map(|(c, a)| (c, a.trim()))
            .unwrap_or((line, ""));

        match cmd {
            ".exit" | ".quit" => ReplCommand::Exit,

            ".help" => {
                println!("{}", colorize(self.color_enabled, "1", "Available REPL commands:"));
                println!("  {}              Show this help", colorize(self.color_enabled, "1", ".help"));
                println!("  {} / {}     Exit the REPL", colorize(self.color_enabled, "1", ".exit"), colorize(self.color_enabled, "1", ".quit"));
                println!("  {}       Load and execute a JavaScript file", colorize(self.color_enabled, "1", ".load <file>"));
                println!("  {}       Show the JS type of an expression", colorize(self.color_enabled, "1", ".type <expr>"));
                println!("  {}             Clear the screen", colorize(self.color_enabled, "1", ".clear"));
                println!("  {}           Show command history", colorize(self.color_enabled, "1", ".history"));
                println!("  {}             Print a reminder (runtime cannot be hot-reset)", colorize(self.color_enabled, "1", ".reset"));
                println!("  {}           Show runtime version info", colorize(self.color_enabled, "1", ".version"));
                ReplCommand::Handled
            }

            ".load" => {
                if arg.is_empty() {
                    eprintln!("{}", colorize(self.color_enabled, "33", "Usage: .load <path/to/file.js>"));
                    return ReplCommand::Handled;
                }
                let path = Path::new(arg);
                match fs::read_to_string(path) {
                    Err(e) => eprintln!("{}", colorize(self.color_enabled, "31", &format!("Failed to read '{}': {}", arg, e))),
                    Ok(source) => {
                        println!("{}", colorize(self.color_enabled, "2", &format!("Loading {}…", arg)));
                        match self.run_script_source(&source, arg) {
                            Ok(result) => {
                                if !is_undefined_value(&self.api, result) {
                                    if let Ok(s) = value_to_string(&self.api, result) {
                                        if !s.is_empty() { println!("{}", colorize(self.color_enabled, "1;36", &s)); }
                                    }
                                }
                            }
                            Err(e) => eprintln!("{}", colorize(self.color_enabled, "31", &e)),
                        }
                    }
                }
                ReplCommand::Handled
            }

            ".type" => {
                if arg.is_empty() {
                    eprintln!("{}", colorize(self.color_enabled, "33", "Usage: .type <expression>"));
                    return ReplCommand::Handled;
                }
                let snippet = format!("typeof ({})", arg);
                match self.run_script_source(&snippet, "<type>") {
                    Ok(v) => {
                        let t = value_to_string(&self.api, v).unwrap_or_else(|_| "?".into());
                        println!("{}", colorize(self.color_enabled, "1;33", &t));
                    }
                    Err(e) => eprintln!("{}", colorize(self.color_enabled, "31", &e)),
                }
                ReplCommand::Handled
            }

            ".clear" => {
                clear_terminal(self.color_enabled);
                ReplCommand::Handled
            }

            ".history" => {
                for (i, entry) in editor.history().iter().enumerate() {
                    println!("  {:>4}  {}", i + 1, entry);
                }
                ReplCommand::Handled
            }

            ".reset" => {
                eprintln!("{}", colorize(self.color_enabled, "33", "Note: live runtime reset is not supported. Restart the process to get a fresh context."));
                ReplCommand::Handled
            }

            ".version" => {
                println!("chakra_runtime (Rust host)  built {}", env!("CARGO_PKG_VERSION"));
                println!("ChakraCore shared library loaded dynamically");
                ReplCommand::Handled
            }

            _ => ReplCommand::Unknown,
        }
    }
}

enum ReplCommand {
    Exit,
    Handled,
    Unknown,
}

impl Drop for HostRuntime {
    fn drop(&mut self) {
        unsafe {
            let _ = (self.api.js_set_current_context)(ptr::null_mut());
            let _ = (self.api.js_dispose_runtime)(self.runtime);
        }
    }
}

// ─── Free helpers ─────────────────────────────────────────────────────────────

fn value_to_string(api: &ChakraApi, value: JsValueRef) -> Result<String, String> {
    let mut sv = ptr::null_mut();
    ensure_js_ok(
        unsafe { (api.js_convert_value_to_string)(value, &mut sv) },
        "JsConvertValueToString",
    )?;

    let mut len: usize = 0;
    ensure_js_ok(
        unsafe { (api.js_copy_string)(sv, ptr::null_mut(), 0, &mut len) },
        "JsCopyString(len)",
    )?;
    if len == 0 { return Ok(String::new()); }

    let mut buf = vec![0u8; len];
    let mut written: usize = 0;
    ensure_js_ok(
        unsafe { (api.js_copy_string)(sv, buf.as_mut_ptr() as *mut i8, buf.len(), &mut written) },
        "JsCopyString(data)",
    )?;
    buf.truncate(written);
    String::from_utf8(buf).map_err(|e| format!("UTF-8 error: {}", e))
}

fn is_undefined_value(api: &ChakraApi, value: JsValueRef) -> bool {
    let mut t = JS_VALUE_TYPE_UNDEFINED;
    let rc = unsafe { (api.js_get_value_type)(value, &mut t) };
    rc == JS_NO_ERROR && t == JS_VALUE_TYPE_UNDEFINED
}

fn ensure_js_ok(code: JsErrorCode, op: &str) -> Result<(), String> {
    if code == JS_NO_ERROR { Ok(()) } else { Err(format_js_error(op, code)) }
}

fn format_js_error(op: &str, code: JsErrorCode) -> String {
    format!("{} failed: {} (0x{:X})", op, error_name(code), code)
}

fn error_name(code: JsErrorCode) -> &'static str {
    match code {
        JS_NO_ERROR              => "JsNoError",
        0x10001                  => "JsErrorInvalidArgument",
        0x10003                  => "JsErrorNoCurrentContext",
        0x10004                  => "JsErrorInExceptionState",
        0x10007                  => "JsErrorRuntimeInUse",
        0x20000                  => "JsErrorCategoryEngine",
        0x20001                  => "JsErrorOutOfMemory",
        JS_ERROR_SCRIPT_EXCEPTION => "JsErrorScriptException",
        JS_ERROR_SCRIPT_COMPILE   => "JsErrorScriptCompile",
        0x30003                  => "JsErrorScriptTerminated",
        0x40001                  => "JsErrorFatal",
        _                        => "JsErrorCode(unknown)",
    }
}

fn chakra_library_candidates(hint: Option<&Path>) -> Vec<PathBuf> {
    let name = default_chakra_library_name();
    let mut v: Vec<PathBuf> = Vec::new();
    if let Some(p) = hint { v.push(p.into()); }
    if let Ok(p) = env::var("CHAKRA_CORE_PATH") { if !p.trim().is_empty() { v.push(p.into()); } }
    if let Ok(exe) = env::current_exe() {
        if let Some(d) = exe.parent() { v.push(d.join(name)); }
    }
    if let Ok(d) = env::current_dir() { v.push(d.join(name)); }
    v.push(name.into());

    // deduplicate
    let mut unique: Vec<PathBuf> = Vec::new();
    for p in v { if !unique.contains(&p) { unique.push(p); } }
    unique
}

fn default_chakra_library_name() -> &'static str {
    if cfg!(target_os = "windows") { "ChakraCore.dll" }
    else if cfg!(target_os = "macos") { "libChakraCore.dylib" }
    else { "libChakraCore.so" }
}

fn should_try_es2021_transform(src: &str) -> bool {
    src.contains("&&=") || src.contains("||=") || src.contains("??=")
}

// ─── Multiline input detection ───────────────────────────────────────────────

fn js_source_needs_more_input(source: &str) -> bool {
    let mut brace = 0i32;
    let mut bracket = 0i32;
    let mut paren = 0i32;
    let mut in_single = false;
    let mut in_double = false;
    let mut in_template = false;
    let mut in_line_comment = false;
    let mut in_block_comment = false;
    let mut prev = '\0';
    let mut escaped = false;

    for ch in source.chars() {
        if in_line_comment {
            if ch == '\n' { in_line_comment = false; }
            prev = ch; continue;
        }
        if in_block_comment {
            if ch == '/' && prev == '*' { in_block_comment = false; }
            prev = ch; continue;
        }
        if in_single {
            if ch == '\\' && !escaped { escaped = true; prev = ch; continue; }
            if ch == '\'' && !escaped { in_single = false; }
            escaped = false; prev = ch; continue;
        }
        if in_double {
            if ch == '\\' && !escaped { escaped = true; prev = ch; continue; }
            if ch == '"' && !escaped { in_double = false; }
            escaped = false; prev = ch; continue;
        }
        if in_template {
            if ch == '\\' && !escaped { escaped = true; prev = ch; continue; }
            if ch == '`' && !escaped { in_template = false; }
            escaped = false; prev = ch; continue;
        }
        match ch {
            '/' if prev == '/' => { in_line_comment = true; }
            '*' if prev == '/' => { in_block_comment = true; }
            '\'' => in_single   = true,
            '"'  => in_double   = true,
            '`'  => in_template = true,
            '{'  => brace    += 1,
            '}'  => brace    -= 1,
            '['  => bracket  += 1,
            ']'  => bracket  -= 1,
            '('  => paren    += 1,
            ')'  => paren    -= 1,
            _ => {}
        }
        prev = ch;
    }

    if in_single || in_double || in_template || in_block_comment { return true; }
    if brace > 0 || bracket > 0 || paren > 0 { return true; }

    let t = source.trim_end();
    if t.is_empty() { return true; }

    matches!(t.chars().last(), Some(
        '\\' | '.' | ',' | ':' | '=' | '+' | '-' | '*' | '/' | '%' |
        '&'  | '|' | '^' | '!' | '?' | '(' | '[' | '{'
    ))
}

// ─── Syntax highlighter ───────────────────────────────────────────────────────

fn highlight_js_line(line: &str, keywords: &[&str], color_enabled: bool) -> String {
    if !color_enabled {
        return line.to_string();
    }

    let mut out = String::with_capacity(line.len() + 64);
    let mut chars = line.chars().peekable();
    let mut in_single = false;
    let mut in_double = false;
    let mut in_template = false;
    let mut in_block_comment = false;

    while let Some(ch) = chars.next() {
        if in_block_comment {
            out.push_str("\x1b[38;5;244m");
            out.push(ch);
            if ch == '*' && matches!(chars.peek(), Some('/')) {
                out.push('/'); chars.next();
                out.push_str("\x1b[0m");
                in_block_comment = false;
            }
            continue;
        }
        macro_rules! string_char {
            ($flag:ident, $close:expr) => {
                if $flag {
                    out.push_str("\x1b[38;5;214m");
                    out.push(ch);
                    if ch == '\\' { if let Some(n) = chars.next() { out.push(n); } }
                    else if ch == $close { out.push_str("\x1b[0m"); $flag = false; }
                    continue;
                }
            };
        }
        string_char!(in_single,   '\'');
        string_char!(in_double,   '"');
        string_char!(in_template, '`');

        if ch == '/' {
            match chars.peek() {
                Some('/') => {
                    out.push_str("\x1b[38;5;244m//");
                    chars.next();
                    for c in &mut chars { out.push(c); }
                    out.push_str("\x1b[0m");
                    break;
                }
                Some('*') => {
                    out.push_str("\x1b[38;5;244m/*");
                    chars.next();
                    in_block_comment = true;
                    continue;
                }
                _ => {}
            }
        }
        if ch == '\'' { out.push_str("\x1b[38;5;214m'"); in_single   = true; continue; }
        if ch == '"'  { out.push_str("\x1b[38;5;214m\""); in_double  = true; continue; }
        if ch == '`'  { out.push_str("\x1b[38;5;214m`"); in_template = true; continue; }

        if ch.is_ascii_digit() {
            out.push_str("\x1b[38;5;81m");
            out.push(ch);
            while let Some(&n) = chars.peek() {
                if n.is_ascii_alphanumeric() || matches!(n, '.' | '_') {
                    out.push(n); chars.next();
                } else { break; }
            }
            out.push_str("\x1b[0m");
            continue;
        }

        if ch.is_ascii_alphabetic() || ch == '_' || ch == '$' {
            let mut word = String::new();
            word.push(ch);
            while let Some(&n) = chars.peek() {
                if n.is_ascii_alphanumeric() || n == '_' || n == '$' { word.push(n); chars.next(); }
                else { break; }
            }
            if keywords.contains(&word.as_str()) {
                out.push_str("\x1b[1;34m"); out.push_str(&word); out.push_str("\x1b[0m");
            } else if matches!(word.as_str(), "console" | "print" | "require" | "module" | "exports") {
                out.push_str("\x1b[1;36m"); out.push_str(&word); out.push_str("\x1b[0m");
            } else if matches!(word.as_str(), "true" | "false" | "null" | "undefined" | "NaN" | "Infinity") {
                out.push_str("\x1b[38;5;208m"); out.push_str(&word); out.push_str("\x1b[0m");
            } else {
                out.push_str(&word);
            }
            continue;
        }

        out.push(ch);
    }

    out.push_str("\x1b[0m");
    out
}

// ─── Keywords ─────────────────────────────────────────────────────────────────

const JS_KEYWORDS: &[&str] = &[
    "await", "async", "break", "case", "catch", "class", "const", "continue", "debugger",
    "default", "delete", "do", "else", "enum", "export", "extends", "finally", "for",
    "function", "if", "import", "in", "instanceof", "let", "new", "return", "super",
    "switch", "this", "throw", "try", "typeof", "var", "void", "while", "with", "yield",
    "of", "from", "static",
];

// ─── CLI ──────────────────────────────────────────────────────────────────────

struct CliArgs {
    chakra_lib:  Option<PathBuf>,
    script_path: Option<PathBuf>,
    repl:        bool,
}

fn parse_cli_args() -> Result<CliArgs, String> {
    let mut args = env::args().skip(1);
    let mut chakra_lib  = None;
    let mut script_path = None;
    let mut repl        = false;

    while let Some(arg) = args.next() {
        match arg.as_str() {
            "-h" | "--help" => { print_usage(); return Err(String::new()); }
            "--chakra-lib"  => {
                chakra_lib = Some(PathBuf::from(
                    args.next().ok_or("--chakra-lib requires a path")?
                ));
            }
            "--repl" | "-i" => repl = true,
            _ => {
                if script_path.is_none() { script_path = Some(PathBuf::from(arg)); }
                else { return Err("Only one script path supported.".into()); }
            }
        }
    }

    if script_path.is_none() && !repl {
        return Err("Provide a script path or pass --repl. Use --help for usage.".into());
    }

    Ok(CliArgs { chakra_lib, script_path, repl })
}

fn print_usage() {
    println!("chakra_runtime — Rust-based ChakraCore host");
    println!();
    println!("Usage:");
    println!("  chakra_runtime [--chakra-lib <lib>] <script.js>");
    println!("  chakra_runtime --repl");
    println!("  chakra_runtime -i");
    println!();
    println!("Environment:");
    println!("  CHAKRA_CORE_PATH   Override the ChakraCore shared library path");
}

// ─── Entry point ─────────────────────────────────────────────────────────────

fn run() -> Result<(), String> {
    let cli = match parse_cli_args() {
        Ok(v)                          => v,
        Err(e) if e.is_empty()         => return Ok(()),
        Err(e)                         => return Err(e),
    };
    let runtime = HostRuntime::create(cli.chakra_lib.as_deref())?;
    if cli.repl || cli.script_path.is_none() {
        return runtime.run_repl();
    }
    runtime.run_script_file(cli.script_path.as_ref().unwrap())
}

fn main() -> ExitCode {
    match run() {
        Ok(())  => ExitCode::SUCCESS,
        Err(e)  => {
            if !e.trim().is_empty() { eprintln!("{}", e); }
            ExitCode::from(1)
        }
    }
}