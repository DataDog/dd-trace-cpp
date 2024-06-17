use dd_trace_rust::{integrations, tracing_datadog_sdk::DDTraceLayer};
use tracing_subscriber::{layer::SubscriberExt, registry::Registry};

mod test_lib;

#[tracing::instrument]
async fn handler(req: axum::extract::Request) -> axum::http::StatusCode {
    axum::http::StatusCode::OK
}

#[test]
fn test_axum() {
    let tracer = test_lib::test_tracer();
    let _guard = tracing::subscriber::set_default(
        Registry::default().with(DDTraceLayer::new(tracer.clone())),
    );

    tokio::runtime::Builder::new_current_thread()
        .enable_all()
        .build()
        .unwrap()
        .block_on(async {
            let app: axum::Router = axum::Router::<()>::new()
                .route("/", axum::routing::any(handler))
                .layer(tower_layer::layer_fn(move |inner| {
                    integrations::axum::DatadogTracing::new(tracer.clone(), inner)
                }));

            let (tx, rx) = tokio::sync::oneshot::channel::<()>();

            // run our app with hyper, listening globally on port 3000
            let listener = tokio::net::TcpListener::bind("localhost:15678")
                .await
                .unwrap();

            let server = axum::serve(listener, app).with_graceful_shutdown(async {
                rx.await.unwrap();
            });
            let server_task = tokio::spawn(async { server.await.unwrap() });

            let client: reqwest_middleware::ClientWithMiddleware =
                reqwest_middleware::ClientBuilder::new(reqwest::Client::builder().build().unwrap())
                    .with(integrations::reqwest::DatadoggMiddleware {})
                    .build();

            let req: reqwest::Request = client
                .get("http://localhost:15678/")
                .timeout(std::time::Duration::from_secs(1))
                .build()
                .unwrap();

            client.execute(req).await.unwrap();

            tx.send(()).unwrap();
            server_task.await.unwrap();
        });
}
