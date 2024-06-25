use std::any::TypeId;
use std::io::Write;
use std::{fmt::Debug, marker::PhantomData};

use tracing::{span, Level, Subscriber};
use tracing_subscriber::{layer, registry, Layer};

use crate::datadog_sdk;

// this function "remembers" the types of the subscriber and the formatter,
// so that we can downcast to something aware of them without knowing those
// types at the callsite.
pub(crate) struct WithContext(
    fn(
        &tracing::Dispatch,
        &span::Id,
        f: &mut dyn FnMut(Option<&mut datadog_sdk::Span>) -> Option<datadog_sdk::Span>,
    ),
);

impl WithContext {
    pub(crate) fn with_context<'a>(
        &self,
        dispatch: &'a tracing::Dispatch,
        id: &span::Id,
        mut f: impl FnMut(Option<&mut datadog_sdk::Span>) -> Option<datadog_sdk::Span>,
    ) {
        (self.0)(dispatch, id, &mut f)
    }
}

pub fn with_current_ddspan<
    F: FnMut(Option<&mut datadog_sdk::Span>) -> Option<datadog_sdk::Span>,
>(
    span: &tracing::Span,
    mut f: F,
) {
    span.with_subscriber(|(id, subscriber)| {
        let with_context = subscriber.downcast_ref::<WithContext>()?;
        with_context.with_context(subscriber, &id, &mut f);
        Some(())
    });
}

pub struct DatadogLayer<S> {
    pub tracer: datadog_sdk::Tracer,
    get_context: WithContext,
    _registry: PhantomData<S>,
}

pub fn set_span_source(dd_span: &mut datadog_sdk::Span, meta: &tracing::Metadata) {
    if let Some(file) = meta.file() {
        dd_span.set_tag("source.file", file);
    }

    if let Some(line) = meta.line() {
        let mut buf = [0; 64];
        dd_span.set_tag("source.line", display(&mut buf, line));
    }
}

impl<S> DatadogLayer<S>
where
    S: Subscriber + for<'a> registry::LookupSpan<'a>,
{
    pub fn new(tracer: datadog_sdk::Tracer) -> Self {
        Self {
            tracer,
            get_context: WithContext(Self::get_context),
            _registry: PhantomData,
        }
    }

    fn get_context(
        dispatch: &tracing::Dispatch,
        id: &span::Id,
        f: &mut dyn FnMut(Option<&mut datadog_sdk::Span>) -> Option<datadog_sdk::Span>,
    ) {
        let subscriber = dispatch
            .downcast_ref::<S>()
            .expect("subscriber should downcast to expected type; this is a bug!");
        let span = subscriber
            .span(id)
            .expect("registry should have a span for the current ID");
        let mut extensions = span.extensions_mut();
        if let Some(new_span) = f(extensions.get_mut::<datadog_sdk::Span>()) {
            extensions.insert(new_span);
        }
    }
}

impl<S> Layer<S> for DatadogLayer<S>
where
    S: Subscriber + for<'a> registry::LookupSpan<'a> + Debug,
    for<'a> <S as registry::LookupSpan<'a>>::Data: Debug,
{
    fn on_new_span(&self, attrs: &span::Attributes<'_>, id: &span::Id, ctx: layer::Context<'_, S>) {
        eprintln!("new span id= {id:?} {:?}", attrs);
        if attrs.metadata().name() == "http.server.request" {
            // Skip because we need to extract the span from the request first
            return;
        }
        let mut dd_span = if attrs.is_root() {
            self.tracer.create_span(attrs.metadata().name())
        } else {
            let current = if let Some(parent_id) = attrs.parent() {
                ctx.span(parent_id)
            } else {
                ctx.lookup_current()
            };

            match current {
                Some(c) => {
                    let extensions = c.extensions();
                    let dd_current = extensions
                        .get::<datadog_sdk::Span>()
                        .expect("every tracing span should have a matching ddtrace span");
                    dd_current.create_child(attrs.metadata().name())
                }
                None => self.tracer.create_span(attrs.metadata().name()),
            }
        };

        if attrs.metadata().level() == &Level::ERROR {
            dd_span.set_error(true);
        }

        attrs
            .values()
            .record(&mut SpanAttributesVisitor { s: &mut dd_span });
    
        set_span_source(&mut dd_span, attrs.metadata());

        let Some(new_span) = ctx.span(id) else {
            // This should never happen I think
            return
        };
        new_span.extensions_mut().insert(dd_span);
    }

    fn on_record(&self, span: &span::Id, values: &span::Record<'_>, ctx: layer::Context<'_, S>) {
        eprintln!("on_record id={span:?} {:?}", values);

        let Some(span) = ctx.span(span) else {
            // This should never happen I think
            return
        };
        let mut extensions = span.extensions_mut();
        let Some(mut dd_span) = extensions.get_mut::<datadog_sdk::Span>() else {
            return
        };
        values.record(&mut SpanAttributesVisitor { s: &mut dd_span });
    }

    // SAFETY: this is safe because the `WithContext` function pointer is valid
    // for the lifetime of `&self`.
    unsafe fn downcast_raw(&self, id: TypeId) -> Option<*const ()> {
        match id {
            id if id == TypeId::of::<Self>() => Some(self as *const _ as *const ()),
            id if id == TypeId::of::<WithContext>() => {
                Some(&self.get_context as *const _ as *const ())
            }
            _ => None,
        }
    }

    fn on_close(&self, id: span::Id, _ctx: layer::Context<'_, S>) {
        eprintln!("on_close {id:?}",);
    }
}

struct SpanAttributesVisitor<'a> {
    s: &'a mut datadog_sdk::Span,
}

impl<'a> tracing::field::Visit for SpanAttributesVisitor<'a> {
    fn record_error(
        &mut self,
        _field: &tracing::field::Field,
        value: &(dyn std::error::Error + 'static),
    ) {
        self.s.set_error(true);
        self.s.set_error_message(&value.to_string());
    }

    fn record_str(&mut self, field: &tracing::field::Field, value: &str) {
        self.s.set_tag(field.name(), value)
    }

    fn record_bool(&mut self, field: &tracing::field::Field, value: bool) {
        let mut buf = [0_u8; 128];
        self.s.set_tag(field.name(), display(&mut buf, value));
    }

    fn record_i64(&mut self, field: &tracing::field::Field, value: i64) {
        let mut buf = [0_u8; 128];
        self.s.set_tag(field.name(), display(&mut buf, value));
    }

    fn record_u64(&mut self, field: &tracing::field::Field, value: u64) {
        let mut buf = [0_u8; 128];
        self.s.set_tag(field.name(), display(&mut buf, value));
    }

    fn record_f64(&mut self, field: &tracing::field::Field, value: f64) {
        let mut buf = [0_u8; 128];
        self.s.set_tag(field.name(), display(&mut buf, value));
    }

    fn record_debug(&mut self, field: &tracing::field::Field, value: &dyn std::fmt::Debug) {
        let v = format!("{:?}", value);
        self.s.set_tag(field.name(), &v);
    }
}

struct CountWriter<T: Write> {
    w: T,
    c: usize,
}

impl<T: Write> Write for CountWriter<T> {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        let written = self.w.write(buf)?;
        self.c += written;
        Ok(written)
    }
    fn flush(&mut self) -> std::io::Result<()> {
        self.w.flush()
    }
}

fn display<T: std::fmt::Display>(buf: &mut [u8], v: T) -> &str {
    let mut writer = CountWriter { w: &mut *buf, c: 0 };
    let _ = write!(&mut writer, "{}", v);
    let written = writer.c;
    unsafe { &*std::str::from_utf8_unchecked_mut(&mut buf[..written]) }
}
