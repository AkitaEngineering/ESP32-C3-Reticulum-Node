// Short-term workaround: provide a no-op _svfprintf_r to avoid hitting
// newlib's floating-point paths (which have caused Illegal instruction
// panics on some ESP32-C3 toolchain/newlib combinations).
//
// This prevents formatted stdio from executing complex FP helpers at
// startup. It's intentionally minimal — it discards output and returns 0.
// Remove this once the underlying toolchain/newlib issue is fixed.

#include <stdarg.h>

extern "C" int _svfprintf_r(void *reent, void *stream, const char *fmt, va_list ap)
{
    (void)reent; (void)stream; (void)fmt; (void)ap;
    // Pretend nothing was written — avoids invoking newlib FP code paths.
    return 0;
}

