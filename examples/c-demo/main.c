#include <datadog/c/tracer.h>
#include <stdio.h>
#include <unistd.h>

int main() {
  datadog_sdk_conf_t* conf = datadog_sdk_tracer_conf_new();

  datadog_sdk_tracer_conf_set(conf, DATADOG_TRACER_OPT_SERVICE_NAME,
                              (void*)"c-demo");
  datadog_sdk_tracer_conf_set(conf, DATADOG_TRACER_OPT_ENV, (void*)"demo");

  datadog_sdk_tracer_t* tracer = datadog_sdk_tracer_new(conf);
  if (tracer == NULL) {
    printf("Failed to initialze the tracer");
    return 1;
  }

  printf("Tracer correctly initialzed");

  datadog_sdk_span_t* span_a = datadog_sdk_tracer_create_span(tracer, "A");
  datadog_sdk_span_set_tag(span_a, "team", "sdk");
  datadog_sdk_span_set_tag(span_a, "foo", "bar");
  sleep(2);
  datadog_sdk_span_t* span_b = datadog_sdk_span_create_child(span_a, "B");
  sleep(1);
  datadog_sdk_span_free(span_b);
  datadog_sdk_span_free(span_a);

  datadog_sdk_tracer_free(tracer);
  return 0;
}
