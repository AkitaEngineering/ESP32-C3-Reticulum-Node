# Experimental Amateur-Radio Components

**Document version:** 3.0
**Updated:** 2026-07-19
**Status:** Experimental; excluded from `PRODUCTION_BUILD`

The repository contains AX.25/APRS helpers, a KISS TNC path, a blocking AFSK transmitter, a sample-fed Goertzel receiver, and a small Winlink-style message adapter. These pieces are useful for development, but they are not a production-qualified modem or a complete Winlink implementation.

## What is implemented

- KISS framing over a configured serial interface.
- AX.25 address encode/decode, up to eight digipeaters, control/PID fields, a 330-byte body limit, and CRC-16/X.25 FCS validation.
- APRS helpers for uncompressed/compressed position output, weather, status, message output, and basic position/weather parsing.
- AFSK/Bell-202-style tone generation with NRZI/bit stuffing, plus a receiver that accepts externally sampled audio and returns FCS-valid frames.
- A callback-driven `WL2K|...` UI-frame message adapter. It does not implement Winlink session, authentication, compression, proposal, transfer, or acknowledgement protocols and must not be described as Winlink interoperability.

## What is not integrated or qualified

- No production environment enables `HAM_MODEM_ENABLED`; the build deliberately errors if it is combined with `PRODUCTION_BUILD=1`.
- ADC sampling cadence, audio filtering, signal level, PTT/keying, isolation, watchdog behavior, and radio-specific timing require a board/radio integration layer.
- The audio path has not passed modem interoperability, weak-signal, frequency-error, clock-drift, or long-duration tests.
- APRS message acknowledgements and connected-mode AX.25 state machines are not implemented.
- Callsign, frequency, power, bandwidth, content, station identification, unattended operation, and encryption rules are the operator's legal responsibility and vary by jurisdiction.

## Safest supported radio path

For development, prefer an external, known-good KISS TNC connected to the ESP32 UART. Configure the TNC and radio independently, then validate FCS, KISS escaping, maximum frames, transmit timing, and over-the-air interoperability with test equipment before use.

Default placeholders such as `N0CALL` and `N0BBS` must never be transmitted. Pin assignments in `Config.h` are examples and may conflict with USB, flash, boot straps, ADC availability, or the selected ESP32 variant.

## Promotion gate

Promotion into a production target requires a dedicated supported hardware design, RF/audio schematic review, automated codec tests, real TNC/APRS interoperability, thermal and soak testing, failure recovery, legal/regulatory review for each market, and an explicit decision about which protocol subset is supported.
