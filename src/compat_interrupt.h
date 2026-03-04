#ifndef DIGITAL_PIN_TO_INTERRUPT_COMPAT_H
#define DIGITAL_PIN_TO_INTERRUPT_COMPAT_H

// Provide a minimal digitalPinToInterrupt() fallback for platforms where the
// symbol may be unavailable to third-party libraries during compilation.
#ifndef digitalPinToInterrupt
  #define digitalPinToInterrupt(pin) (pin)
#endif

#endif // DIGITAL_PIN_TO_INTERRUPT_COMPAT_H
