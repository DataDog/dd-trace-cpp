use dd_trace_rust::{Config, ConfigProperty, Span, Tracer};

fn foo(current_span: &mut Span) {
    let span = current_span.create_child("foo");

    span.inject(|key, value| {
        eprintln!(
            "injected {} : {}",
            String::from_utf8_lossy(key),
            String::from_utf8_lossy(value)
        )
    });
}

#[test]
fn test_smoke() {
    let mut cfg = Config::new();
    cfg.set(ConfigProperty::Service, "ddtrace-rs-test");
    cfg.set(ConfigProperty::Env, "dev");
    cfg.set(ConfigProperty::AgentUrl, "http://localhost:8126");
    cfg.set(ConfigProperty::Version, "0.0.1");

    let tracer: Tracer = Tracer::new(&cfg);

    {
        let mut op1_span: Span = tracer.create_span("op1");
        foo(&mut op1_span);
    }

    {
        let mut op2_span = tracer.create_or_extract_span(
            |key| {
                eprintln!("read key {}", String::from_utf8_lossy(key));
                if key == b"x-datadog-trace-id" {
                    return Some(b"12345679");
                }
                None
            },
            "op2",
            "/drop/the/nukes?all=true",
        );
        eprintln!("{:?}", op2_span);
        foo(&mut op2_span);
    }
}

#[test]
fn test_thread_safe() {
    fn is_send<T: Send>() {}
    fn is_sync<T: Sync>() {}

    is_send::<Span>();
    is_sync::<Span>();

    is_send::<Tracer>();
    is_sync::<Tracer>();
}
