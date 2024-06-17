use std::{convert::Infallible, fmt::Write, future::Future, pin::Pin};

use axum::{
    body::Body,
    extract::Request,
    response::{IntoResponse, Response},
};
use tower_service::Service;
use tracing::{field, Instrument, Level};

use crate::{datadog_sdk, tracing_datadog_sdk};

#[derive(Clone)]
pub struct DatadogTracing<S> {
    inner: S,
    uri_buffer: String,
    tracer: datadog_sdk::Tracer,
}

impl<S> DatadogTracing<S> {
    pub fn new(tracer: datadog_sdk::Tracer, inner: S) -> Self {
        Self {
            inner,
            uri_buffer: String::new(),
            tracer,
        }
    }
}

impl<S> Service<Request> for DatadogTracing<S>
where
    S: Service<Request> + Clone + Send + 'static,
    S::Response: IntoResponse + 'static,
    S::Error: Into<Infallible> + 'static,
    S::Future: Send + 'static,
{
    type Response = Response<Body>;
    type Error = S::Error;
    type Future =
        Pin<Box<dyn Future<Output = Result<Self::Response, Self::Error>> + Send + 'static>>;

    fn call(&mut self, req: Request) -> Self::Future {
        let span = tracing::info_span!("http.server.request", "http.status" = field::Empty,);
        tracing_datadog_sdk::with_current_ddspan(&span, |prev_dd_span| {
            if prev_dd_span.is_some() {
                // TODO this should not be a panic be at least a soft error
                panic!("Current tracing already has a dd span associated. This is unexpected")
            }
            let mut dd_span = self.tracer.create_or_extract_span(
                |key| Some(req.headers().get(key)?.as_bytes()),
                "http.server.request",
                "http.request",
            );
            dd_span.set_type("web");
            self.uri_buffer.truncate(0);
            let _ = write!(&mut self.uri_buffer, "{}", req.uri());
            dd_span.set_tag("http.url", &self.uri_buffer);
            dd_span.set_tag("http.method", req.method().as_str());
            dd_span.set_tag("span.kind", "server");
            dd_span.set_tag("component", "axum");

            span.metadata()
                .map(|m| tracing_datadog_sdk::set_span_source(&mut dd_span, m));

            Some(dd_span)
        });

        let ret = self.inner.call(req);
        Box::pin(
            async {
                let res = match ret.await {
                    Ok(res) => res,
                    Err(e) => match e.into() {},
                };
                let res = res.into_response();
                let status = res.status().as_u16();
                tracing::Span::current().record("http.status", status);

                Ok(res)
            }
            .instrument(span),
        )
    }

    fn poll_ready(
        &mut self,
        cx: &mut std::task::Context<'_>,
    ) -> std::task::Poll<Result<(), Self::Error>> {
        self.inner.poll_ready(cx)
    }
}
