use dd_trace_rust::datadog_sdk::{Config, ConfigProperty, Tracer};

pub(crate) fn test_tracer() -> Tracer {
    let mut cfg = Config::new();
    cfg.set(ConfigProperty::Service, "ddtrace-rs-test");
    cfg.set(ConfigProperty::Env, "dev");
    cfg.set(ConfigProperty::AgentUrl, "http://localhost:8136");
    cfg.set(ConfigProperty::Version, "0.0.1");

    Tracer::new(cfg)
}
