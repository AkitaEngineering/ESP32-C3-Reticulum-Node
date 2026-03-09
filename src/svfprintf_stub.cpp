// Short-term workaround: provide a guarded _svfprintf_r to avoid hitting
// newlib's formatted-IO paths that can invoke floating-point helpers on
// some RISC‑V (ESP32‑C3) toolchain/newlib combinations — which caused
// Illegal instruction panics during early startup on affected boards.
//
// Instead of a blanket no-op (which kills ALL printf/snprintf output),
// we forward to vfprintf and only suppress the call if it appears to
// involve a float format specifier (%f, %e, %g, etc.).  This approach
// keeps integer/string printf working while still avoiding the crash.
//
// Guard this symbol so it only applies to RISC‑V / ESP32‑C3 builds.
// TODO: remove this stub when upstream toolchain/newlib is fixed (see issue).

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if defined(ARDUINO_ARCH_ESP32C3) || defined(__riscv)

// Quick scan for float-format specifiers: %f %e %g %a and their uppercase variants.
static bool hasFloatSpec(const char *fmt) {
    if (!fmt) return false;
    for (const char *p = fmt; *p; ++p) {
        if (*p != '%') continue;
        ++p;
        if (!*p) break;
        // Skip flags, width, precision, length modifiers
        while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') ++p;
        while (*p >= '0' && *p <= '9') ++p;
        if (*p == '.') { ++p; while (*p >= '0' && *p <= '9') ++p; }
        while (*p == 'l' || *p == 'h' || *p == 'L' || *p == 'z' || *p == 'j' || *p == 't') ++p;
        if (*p == 'f' || *p == 'F' || *p == 'e' || *p == 'E' ||
            *p == 'g' || *p == 'G' || *p == 'a' || *p == 'A') {
            return true;
        }
        if (*p == '%') continue; // literal %%
        // other specifier, keep scanning
    }
    return false;
}

extern "C" int _svfprintf_r(void *reent, void *stream, const char *fmt, va_list ap)
{
    if (hasFloatSpec(fmt)) {
        // Suppress: this path triggers the RISC-V illegal instruction.
        (void)reent; (void)stream; (void)ap;
        return 0;
    }
    // Forward non-float formats to the real vfprintf so printf/snprintf work.
    return vfprintf((FILE*)stream, fmt, ap);
}
#endif

