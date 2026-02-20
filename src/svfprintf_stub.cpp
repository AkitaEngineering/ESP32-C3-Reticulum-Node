// Short-term workaround: provide a no-op _svfprintf_r to avoid hitting
// newlib's formatted-IO paths that can invoke floating-point helpers on
// some RISC‑V (ESP32‑C3) toolchain/newlib combinations — which caused
// Illegal instruction panics during early startup on affected boards.
//
// Guard this symbol so it only applies to RISC‑V / ESP32‑C3 builds.
// TODO: remove this stub when upstream toolchain/newlib is fixed (see issue).

#include <stdarg.h>

#if defined(ARDUINO_ARCH_ESP32C3) || defined(__riscv)
extern "C" int _svfprintf_r(void *reent, void *stream, const char *fmt, va_list ap)
{
    (void)reent; (void)stream; (void)fmt; (void)ap;
    // No-op: avoid invoking newlib floating-point formatted IO helpers.
    return 0;
}
#endif

