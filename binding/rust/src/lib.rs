mod dd_trace_cpp;
use std::{ffi, ptr, slice};

use dd_trace_cpp::bindings;

#[repr(u32)]
pub enum ConfigProperty {
    Service = bindings::datadog_sdk_tracer_option_DATADOG_TRACER_OPT_SERVICE_NAME,
    Env = bindings::datadog_sdk_tracer_option_DATADOG_TRACER_OPT_ENV,
    Version = bindings::datadog_sdk_tracer_option_DATADOG_TRACER_OPT_VERSION,
    AgentUrl = bindings::datadog_sdk_tracer_option_DATADOG_TRACER_OPT_AGENT_URL,
}

pub struct Config {
    inner: *mut bindings::datadog_sdk_conf_t,
}

impl Config {
    pub fn new() -> Self {
        Self {
            inner: unsafe { bindings::datadog_sdk_tracer_conf_new() },
        }
    }

    pub fn set(&mut self, property: ConfigProperty, value: &str) {
        let mut value = str_to_ffi_view(value);
        unsafe {
            dd_trace_cpp::bindings::datadog_sdk_tracer_conf_set(
                self.inner,
                property as bindings::datadog_sdk_tracer_option,
                &mut value as *mut bindings::str_view as *mut ffi::c_void,
            )
        }
    }
}

impl Drop for Config {
    fn drop(&mut self) {
        unsafe { bindings::datadog_sdk_tracer_conf_free(self.inner) }
    }
}

pub struct Tracer {
    inner: *mut ffi::c_void,
}

impl Tracer {
    pub fn new(cfg: &Config) -> Self {
        Tracer {
            inner: unsafe { dd_trace_cpp::bindings::datadog_sdk_tracer_new(cfg.inner) },
        }
    }

    pub fn create_span(&self, name: &str) -> Span {
        let name = str_to_ffi_view(name);
        Span {
            inner: unsafe {
                dd_trace_cpp::bindings::datadog_sdk_tracer_create_span(self.inner, name)
            },
        }
    }

    pub fn create_or_extract_span<'a, F: FnMut(&[u8]) -> Option<&'a [u8]> + 'a>(
        &self,
        mut reader: F,
        name: &str,
        ressource: &str,
    ) -> Span {
        let name = str_to_ffi_view(name);
        let resource = str_to_ffi_view(ressource);
        unsafe extern "C" fn reader_fn_trampoline<'a, F: FnMut(&[u8]) -> Option<&'a [u8]> + 'a>(
            ctx: *mut ffi::c_void,
            key: bindings::str_view,
        ) -> bindings::str_view {
            let Some(f) = (ctx as *mut F ).as_mut() else {
                return bindings::str_view {
                    buf: std::ptr::null_mut(),
                    len: 0,
                };
            };
            let key = ffi_view_to_slice(&key);
            match dbg!(f(key)) {
                Some(value) => str_to_ffi_view(value),
                None => bindings::str_view {
                    buf: ptr::null_mut(),
                    len: 0,
                },
            }
        }

        let ctx: *mut ffi::c_void = &mut reader as *mut F as *mut ffi::c_void;
        Span {
            inner: unsafe {
                dd_trace_cpp::bindings::datadog_sdk_tracer_extract_or_create_span(
                    self.inner,
                    ctx,
                    Some(reader_fn_trampoline::<F>),
                    name,
                    resource,
                )
            },
        }
    }
}

impl Drop for Tracer {
    fn drop(&mut self) {
        unsafe { dd_trace_cpp::bindings::datadog_sdk_tracer_free(self.inner) };
    }
}

#[derive(Debug)]
pub struct Span {
    inner: *mut bindings::datadog_sdk_span_t,
}

impl Drop for Span {
    fn drop(&mut self) {
        unsafe {
            dd_trace_cpp::bindings::datadog_sdk_span_finish(self.inner);
            dd_trace_cpp::bindings::datadog_sdk_span_free(self.inner);
        };
    }
}

impl Span {
    pub fn set_tag(&mut self, tag: &str, value: &str) {
        let tag = str_to_ffi_view(tag);
        let value = str_to_ffi_view(value);
        unsafe { dd_trace_cpp::bindings::datadog_sdk_span_set_tag(self.inner, tag, value) }
    }

    pub fn set_error(&mut self, is_err: bool) {
        unsafe {
            dd_trace_cpp::bindings::datadog_sdk_span_set_error(self.inner, is_err as ffi::c_int)
        }
    }

    pub fn set_error_message(&mut self, message: &str) {
        let message = str_to_ffi_view(message);
        unsafe { dd_trace_cpp::bindings::datadog_sdk_span_set_error_message(self.inner, message) }
    }

    pub fn create_child(&mut self, name: &str) -> Self {
        let name = str_to_ffi_view(name);
        Self {
            inner: unsafe {
                dd_trace_cpp::bindings::datadog_sdk_span_create_child(self.inner, name)
            },
        }
    }

    pub fn inject<F: FnMut(&[u8], &[u8])>(&self, mut writer: F) {
        unsafe extern "C" fn writer_trampoline<F: FnMut(&[u8], &[u8])>(
            ctx: *mut ffi::c_void,
            key: bindings::str_view,
            value: bindings::str_view,
        ) {
            let Some(f) = (ctx as *mut F).as_mut() else {
                return
            };
            f(ffi_view_to_slice(&key), ffi_view_to_slice(&value));
        }
        let ctx = &mut writer as *mut F as *mut ffi::c_void;
        unsafe {
            dd_trace_cpp::bindings::datadog_sdk_span_inject(
                self.inner,
                ctx,
                Some(writer_trampoline::<F>),
            )
        }
    }
}

fn str_to_ffi_view<T: AsRef<[u8]>>(s: T) -> bindings::str_view {
    bindings::str_view {
        buf: s.as_ref().as_ptr().cast::<ffi::c_char>(),
        len: s.as_ref().len(),
    }
}
unsafe fn ffi_view_to_slice(s: &bindings::str_view) -> &[u8] {
    if s.buf.is_null() {
        slice::from_raw_parts(std::ptr::NonNull::dangling().as_ptr(), s.len)
    } else {
        slice::from_raw_parts(s.buf.cast::<u8>(), s.len)
    }
}
