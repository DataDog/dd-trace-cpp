# The Bazel build is primarily for use by Envoy.
#
# Envoy forbids use of std::string_view and std::optional, preferring use of
# Abseil's absl::string_view and absl::optional instead.
#
# In the context of an Envoy build, the Abseil libraries point to whatever
# versions Envoy uses, including some source patches.
#
# To test this library's Bazel build independent of Envoy, we need to specify
# versions of the Abseil libraries, including Envoy's patches. That is what this
# file is for.

# These rules are based on <https://abseil.io/docs/cpp/quickstart>,
# accessed December 6, 2022.
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "com_google_absl",
    sha256 = "1692f77d1739bacf3f94337188b78583cf09bab7e420d2dc6c5605a4f86785a1",
    strip_prefix = "abseil-cpp-20250814.1",
    urls = ["https://github.com/abseil/abseil-cpp/releases/download/20250814.1/abseil-cpp-20250814.1.tar.gz"],
)

http_archive(
    name = "bazel_skylib",
    sha256 = "6e78f0e57de26801f6f564fa7c4a48dc8b36873e416257a92bbb0937eeac8446",
    urls = ["https://github.com/bazelbuild/bazel-skylib/releases/download/1.8.2/bazel-skylib-1.8.2.tar.gz"],
)

http_archive(
    name = "platforms",
    sha256 = "3384eb1c30762704fbe38e440204e114154086c8fc8a8c2e3e28441028c019a8",
    urls = ["https://github.com/bazelbuild/platforms/releases/download/1.0.0/platforms-1.0.0.tar.gz"],
)

http_archive(
    name = "rules_cc",
    sha256 = "a2fdfde2ab9b2176bd6a33afca14458039023edb1dd2e73e6823810809df4027",
    strip_prefix = "rules_cc-0.2.14",
    urls = ["https://github.com/bazelbuild/rules_cc/releases/download/0.2.14/rules_cc-0.2.14.tar.gz"],
)

load("@rules_cc//cc:extensions.bzl", "compatibility_proxy_repo")

compatibility_proxy_repo()
