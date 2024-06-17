pub mod datadog_sdk;
pub mod tracing_datadog_sdk;
pub mod integrations;

pub(crate) mod build_info {
    include!(concat!(env!("OUT_DIR"), "/built.rs"));

    pub fn rust_version() -> &'static str {
        // From something like "rustc 1.71.1 (eb26296b5 2023-08-03)" to "1.71.1"
        RUSTC_VERSION
            .strip_prefix("rustc ")
            .unwrap_or(RUSTC_VERSION)
            .split_once(' ')
            .map(|(v, _)| v)
            .unwrap_or(RUSTC_VERSION)
    }
}
