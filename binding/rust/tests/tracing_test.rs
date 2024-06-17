use tracing::span;
use tracing_subscriber::layer::SubscriberExt;

mod test_lib;

#[test]
fn test_tracing() {
    #[derive(Debug)]
    struct Foo {
        _a: u32,
    }

    #[tracing::instrument]
    fn hello_there(a: Foo) {}

    let tracer = test_lib::test_tracer();
    let subscriber = tracing_subscriber::Registry::default().with(
        dd_trace_rust::tracing_datadog_sdk::DDTraceLayer::new(tracer),
    );
    tracing::subscriber::with_default(subscriber, || {
        let root = span!(tracing::Level::TRACE, "app_start", val = 1);
        let _root = root.enter();
        hello_there(Foo { _a: 1 });
    });
}
