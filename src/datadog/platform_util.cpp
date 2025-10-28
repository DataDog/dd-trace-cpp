#ifdef _MSC_VER
#include "platform_util_windows.cpp"
#elif defined(__APPLE__) || defined(__FreeBSD__)
#include "platform_util_darwin.cpp"
#else
#include "platform_util_unix.cpp"
#endif
