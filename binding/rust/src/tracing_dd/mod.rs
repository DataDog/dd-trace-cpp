use std::fmt::Debug;

use tracing::{span, Level, Subscriber};
use tracing_subscriber::{layer, registry, Layer};

pub struct DDTraceLayer {
    pub tracer: crate::Tracer,
}

impl DDTraceLayer {
    fn create_root_span<S>(
        &self,
        id: &span::Id,
        attrs: &span::Attributes<'_>,
        ctx: &layer::Context<'_, S>,
    ) where
        S: Subscriber + for<'a> registry::LookupSpan<'a> + Debug,
        for<'a> <S as registry::LookupSpan<'a>>::Data: Debug,
    {
        let Some(new_span) = ctx.span(id) else {
            return
        };
        let dd_span = self.tracer.create_span(attrs.metadata().name());
        if attrs.metadata().level() == &Level::ERROR {
            dd_span.set_error(true);
        }
        new_span.extensions_mut().insert(dd_span);
    }
}

impl<S> Layer<S> for DDTraceLayer
where
    S: Subscriber + for<'a> registry::LookupSpan<'a> + Debug,
    for<'a> <S as registry::LookupSpan<'a>>::Data: Debug,
{
    fn on_new_span(&self, attrs: &span::Attributes<'_>, id: &span::Id, ctx: layer::Context<'_, S>) {
        if attrs.is_root() {
            self.create_root_span(id, attrs, &ctx);
            return;
        }
        dbg!("creating span", attrs);
        let current = if let Some(parent_id) = attrs.parent() {
            ctx.span(parent_id)
        } else {
            ctx.lookup_current()
        };
        let Some(current) = current else {
            self.create_root_span(id, attrs, &ctx);
                return;
            };
        let dd_span = {
            let extensions = current.extensions();
            let dd_current = extensions
                .get::<crate::Span>()
                .expect("every tracing span should have a matching ddtrace span");
            dd_current.create_child(attrs.metadata().name())
        };
        let Some(new_span) = ctx.span(id) else {
            return
        };
        new_span.extensions_mut().insert(dd_span);
    }

    fn on_close(&self, id: span::Id, ctx: layer::Context<'_, S>) {
        dbg!("closing span", ctx.span(&id).map(|s| s.metadata()));
    }
}
