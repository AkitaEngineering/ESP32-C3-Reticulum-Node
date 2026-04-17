#include <Arduino.h>
#include "ReticulumNode.h"
#include "Utils.h"

#ifndef PIO_UNIT_TESTING

// USB PID matches the bootloader so Windows can use the same installed
// CDC driver.  The previous strategy of flipping the PID caused the host
// to treat the device as unknown, which made COM disappear entirely (beep
// sound) unless the driver was manually reinstalled.
//
// To work around Windows caching behavior we rely on a short wait/blink in
// setup and allow the USB port to drop briefly; the user can simply re-open
// the serial monitor after reset, or the system will re‑enumerate within a
// second or two.  This keeps the user experience simple without driver
// hassles.

// Global instance of the main node application class
ReticulumNode reticulumNode;

// Application Data Handler function
// This function is called by ReticulumNode when data arrives over a Link
void myAppDataReceiver(const uint8_t *source_address, const std::vector<uint8_t> &data)
{
  DebugSerial.print("<<<< App Layer Received ");
  DebugSerial.print(data.size());
  DebugSerial.print(" bytes from ");
  Utils::printBytes(source_address, RNS_ADDRESS_SIZE, DebugSerial);
  DebugSerial.print(": \"");
  for (uint8_t byte : data)
  {
    if (isprint(byte))
      DebugSerial.print((char)byte);
    else
      DebugSerial.print('.');
  }
  DebugSerial.println("\"");
}

void setup()
{
  // initialize built-in LED for status
  initStatusLed();
  setStatusLed(false);

  // Initialize Serial (USB/UART0) for debug output
  DebugSerial.begin(115200);

#if defined(KISS_OVER_USB)
  // When KISS_OVER_USB is active, KissSerial == Serial (USB CDC).
  // The build flags include ARDUINO_USB_MODE=1 which DISABLES the
  // auto-Serial.begin() in the Arduino framework's app_main()
  // (guarded by !ARDUINO_USB_MODE).  We MUST call Serial.begin()
  // explicitly so HWCDC::begin() runs — it creates the TX/RX ring
  // buffers, enables the USB D+ pullup, and allocates the ISR.
  // Without this call the USB device never enumerates on the host.
  Serial.begin();            // baud rate is ignored for HWCDC
#else
  // Start KISS serial on hardware UART (not USB)
  KissSerial.begin(KISS_SERIAL_SPEED, SERIAL_8N1, KISS_UART_RX, KISS_UART_TX);
#endif

  // wait up to 10 seconds for USB CDC to enumerate; blink LED while waiting
  unsigned long start = millis();
  while (!Serial && millis() - start < 10000) {
    // blink LED at 2Hz during enumeration
    setStatusLed(((millis() / 250) & 1));
    delay(10);
  }
  // leave LED on if enumeration succeeded, off otherwise
  setStatusLed(Serial);
  // small delay to let host settle
  delay(100);

  // Diagnostic early print to confirm USB is up (if it is)
  if (Serial) {
    DebugSerial.println("[BOOT] DebugSerial initialized (early)");
  } else {
    DebugSerial.println("[BOOT] Serial not available after initial wait");
    // We can't force the USB reattach from software on ESP32-C3; rely on
    // the blink indicator and PID change instead.  If enumeration fails the
    // LED will remain off and the host should be unplugged/re-plugged.
  }

  DebugSerial.println("\n\n===================================");
  DebugSerial.println(" ESP32 Reticulum Gateway - Booting ");
  DebugSerial.println("===================================");

  // Initialize the Reticulum node subsystems
  reticulumNode.setup();

  // Register the application data handler
  reticulumNode.setAppDataHandler(myAppDataReceiver);

  DebugSerial.println("-----------------------------------");
  DebugSerial.println(" Setup Complete. Entering main loop.");
  DebugSerial.println("-----------------------------------");
}

void loop()
{
  // Monitor USB connection state for debugging
  static bool prevConnected = false;
  bool curConnected = Serial;
  if (curConnected && !prevConnected) {
    DebugSerial.println("[USB] Host opened CDC");
  } else if (!curConnected && prevConnected) {
    DebugSerial.println("[USB] Host closed CDC");
  }
  prevConnected = curConnected;

  // Run the main node loop function
  reticulumNode.loop();

#if DEMO_TRAFFIC_ENABLED
  // SEND MESSAGE EVERY 10 SECONDS (demo mode)
  static uint32_t last_send = 0;
  if (millis() - last_send >= 10000) {
    last_send = millis();

    // Full 16-byte destination hash for PLAIN destination ["esp32", "node"]
    uint8_t dest_hash[16] = {0xB6, 0x01, 0x0E, 0xA1, 0x1F, 0xDF, 0xC0, 0x4E,
                             0x01, 0x88, 0x3B, 0xD6, 0x06, 0xC5, 0x42, 0xD7};

    // Prepare message payload
    const char* msg = "Hello from ESP32";
    std::vector<uint8_t> payload((uint8_t*)msg, (uint8_t*)msg + strlen(msg));

    // Serialize packet using official Reticulum wire format
    uint8_t buffer[MAX_PACKET_SIZE];
    size_t packet_len = 0;

    bool success = ReticulumPacket::serialize(
      buffer, packet_len,
      dest_hash,
      RNS_PACKET_DATA,
      RNS_DEST_PLAIN,
      RNS_PROPAGATION_BROADCAST,
      RNS_CONTEXT_NONE,
      0,
      payload
    );

    if (success) {
      DebugSerial.println("\n==== SENDING PACKET ====");
      DebugSerial.print("Packet size: ");
      DebugSerial.println(packet_len);
      DebugSerial.print("Destination hash: ");
      Utils::printBytes(dest_hash, 16, DebugSerial);
      DebugSerial.println();
      DebugSerial.print("Message: ");
      DebugSerial.println(msg);

      // Send via all available interfaces (ESP-NOW, WiFi UDP, Serial KISS, etc.)
      reticulumNode.getInterfaceManager().sendPacket(buffer, packet_len, dest_hash);
    } else {
      DebugSerial.println("ERROR: Failed to serialize packet!");
    }
  }
#endif
}

#endif
