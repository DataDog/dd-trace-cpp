use reqwest::{Request, Response};
use reqwest_middleware::{Middleware, Next};
use tracing::{Instrument, field};

use crate::tracing_datadog_sdk;

pub struct DatadoggMiddleware {}

#[async_trait::async_trait]
impl Middleware for DatadoggMiddleware {
    async fn handle(
        &self,
        mut req: Request,
        extensions: &mut http::Extensions,
        next: Next<'_>,
    ) -> reqwest_middleware::Result<Response> {
        
        let span = tracing::info_span!(
            "http.client.request",
            "span.kind" = "client",
            "http.method" = req.method().as_str(),
            "http.url" = req.url().as_str(),
            "http.host" = req.url().host_str().unwrap_or(""),
            "http.status_code" = field::Empty,
            "component" = "reqwest",
        );
        tracing_datadog_sdk::with_current_ddspan(&span, |dd_span| {
            let dd_span = dd_span?;
            dd_span.set_type("web");
            dd_span.inject(|key, value| {
                let Ok(key) = http::header::HeaderName::from_bytes(key) else {
                    return
                } ;
                let Ok(header) = http::header::HeaderValue::from_bytes(value) else {
                    return
                };
                req.headers_mut().append(key, header);
            });
            None
        });
        next.run(req, extensions).instrument(span).await
    }
}
