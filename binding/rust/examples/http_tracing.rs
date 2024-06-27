use axum::extract::Path;
use dd_trace_rust::{
    datadog_sdk::{self, ConfigProperty},
    integrations,
    tracing_datadog_sdk::DatadogLayer,
};
use tokio::net::ToSocketAddrs;
use tracing_subscriber::{layer::SubscriberExt, registry};

fn new_tracer() -> datadog_sdk::Tracer {
    let mut cfg: datadog_sdk::Config = datadog_sdk::Config::new();
    cfg.set(ConfigProperty::Service, "ddtrace-rs-test");
    cfg.set(ConfigProperty::Env, "dev");
    cfg.set(ConfigProperty::AgentUrl, "http://localhost:8136");
    cfg.set(ConfigProperty::Version, "0.0.1");

    datadog_sdk::Tracer::new(cfg)
}

#[tracing::instrument]
async fn good_handler(Path(id): Path<u32>) -> axum::http::StatusCode {
    axum::http::StatusCode::OK
}

#[tracing::instrument]
async fn bad_handler() -> axum::http::StatusCode {
    tracing::error!("something bad happened");
    axum::http::StatusCode::NOT_FOUND
}

#[tokio::main]
async fn main() {
    let tracer = new_tracer();
    tracing::subscriber::set_global_default(registry().with(DatadogLayer::new(tracer.clone())))
        .unwrap();

    let app: axum::Router = axum::Router::<()>::new()
        .route("/bad", axum::routing::any(bad_handler))
        .route("/good/:id", axum::routing::any(good_handler))
        .layer(integrations::axum::datadog_layer(tracer.clone()));
    let (app_task, app_shutdown) = start_app(app, "localhost:15678").await;

    let client: reqwest_middleware::ClientWithMiddleware =
        reqwest_middleware::ClientBuilder::new(reqwest::Client::builder().build().unwrap())
            .with(integrations::reqwest::datadog_middleware())
            .build();

    {
        let _span: tracing::span::EnteredSpan = tracing::info_span!("main").entered();
        let req: reqwest::Request = client
            .get("http://localhost:15678/bad")
            .timeout(std::time::Duration::from_secs(1))
            .build()
            .unwrap();
        client.execute(req).await.unwrap();

        let req: reqwest::Request = client
            .get("http://localhost:15678/good/123")
            .timeout(std::time::Duration::from_secs(1))
            .build()
            .unwrap();
        client.execute(req).await.unwrap();
    }

    app_shutdown.send(()).unwrap();
    app_task.await.unwrap();
    tracer.flush();
}

async fn start_app<A: ToSocketAddrs>(
    app: axum::Router,
    addr: A,
) -> (
    tokio::task::JoinHandle<()>,
    tokio::sync::oneshot::Sender<()>,
) {
    let listener = tokio::net::TcpListener::bind(addr).await.unwrap();
    let (shutdown_sender, shutdown_receiver) = tokio::sync::oneshot::channel::<()>();
    let server = axum::serve(listener, app).with_graceful_shutdown(async {
        shutdown_receiver.await.unwrap();
    });
    let app_task = tokio::spawn(async { server.await.unwrap() });

    (app_task, shutdown_sender)
}
