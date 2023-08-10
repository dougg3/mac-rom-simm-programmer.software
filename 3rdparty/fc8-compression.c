// Include the external compression code, but disable some warnings first.

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wsequence-point"
// Clang doesn't have this warning but GCC does...
#if defined(__has_warning)
#if __has_warning("-Wmaybe-uninitialized")
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#else
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include "3rdparty/fc8-compression/compression.c"

#pragma GCC diagnostic pop
