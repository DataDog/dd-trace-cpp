use dd_trace_rust::{Config, ConfigProperty, Tracer};
use tracing::span;
use tracing_subscriber::{prelude::__tracing_subscriber_SubscriberExt, registry::Registry, Layer};

fn test_tracer() -> Tracer {
    let mut cfg = Config::new();
    cfg.set(ConfigProperty::Service, "ddtrace-rs-test");
    cfg.set(ConfigProperty::Env, "dev");
    cfg.set(ConfigProperty::AgentUrl, "http://localhost:8136");
    cfg.set(ConfigProperty::Version, "0.0.1");

    Tracer::new(&cfg)
}

#[test]
fn test_tracing() {
    #[tracing::instrument]
    fn hello_there(a: i32) {}

    let tracer = test_tracer();
    let subscriber = tracing_subscriber::Registry::default()
        .with(dd_trace_rust::tracing_dd::DDTraceLayer { tracer });
    tracing::subscriber::with_default(subscriber, || {
        let root = span!(tracing::Level::TRACE, "app_start", val = 1);
        let _root = root.enter();
        hello_there(1);
    });
}
