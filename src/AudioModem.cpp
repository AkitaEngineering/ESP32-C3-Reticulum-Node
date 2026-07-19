#include "AudioModem.h"
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// AFSK/Bell 202 implementation (TX blocking; RX Goertzel-based, lightweight).
// Note: RX requires periodic calls to processAudioSample(sample) with real ADC samples
// at the configured sample rate. TX is blocking and uses LEDC tone generation.

// Utility: constrain sample rate and derived values
static uint16_t computeSamplesPerBit(uint32_t sampleRate, uint16_t baud) {
    uint32_t spb = (sampleRate + baud / 2) / baud;
    if (spb < 4) spb = 4;
    if (spb > 1024) spb = 1024;
    return (uint16_t)spb;
}

AudioModem::AudioModem(ModemType type)
: _type(type),
  _rxPin(0),
  _txPin(0),
  _sampleRate(8000),
  _markFreq(1200),
  _spaceFreq(2200),
  _baudRate(1200),
  _samplesPerBit(0),
  _ledcChannel(7),
  _ledcResolution(8),
  _transmitting(false),
  _receiving(false),
  _sampleCount(0),
  _onesCount(0),
  _inFrame(false),
  _rxCurrentByte(0),
  _rxBitPos(0),
  _lastNRZIState(0),
  _nrziInitialized(false)
{}

AudioModem::~AudioModem() = default;

bool AudioModem::begin(uint8_t rxPin, uint8_t txPin, uint32_t sampleRate) {
    _rxPin = rxPin;
    _txPin = txPin;
    _sampleRate = sampleRate;

    switch (_type) {
        case ModemType::BELL_202:
        case ModemType::AFSK_1200:
            _baudRate = 1200;
            _markFreq = 1200;
            _spaceFreq = 2200;
            break;
        case ModemType::AFSK_2400:
            _baudRate = 2400;
            _markFreq = 1200;
            _spaceFreq = 2400;
            break;
    }

    _samplesPerBit = computeSamplesPerBit(_sampleRate, _baudRate);

    // Precompute Goertzel coefficients
    float wMark = 2.0f * PI * (_markFreq / (float)_sampleRate);
    float wSpace = 2.0f * PI * (_spaceFreq / (float)_sampleRate);
    _goertzelMark.coeff = 2.0f * cosf(wMark);
    _goertzelSpace.coeff = 2.0f * cosf(wSpace);

    // Setup LEDC for tone generation (TX)
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    // Arduino Core 3.x: channel-less API, attach directly to pin
    ledcAttach(_txPin, _markFreq, _ledcResolution);
#else
    ledcSetup(_ledcChannel, _markFreq, _ledcResolution);
    ledcAttachPin(_txPin, _ledcChannel);
    ledcWrite(_ledcChannel, 128); // 50% duty
#endif

    return true;
}

// Blocking NRZI + bit-stuffing TX using tone changes
bool AudioModem::transmit(const uint8_t* data, size_t len) {
    if (!data || len == 0) return false;
    _transmitting = true;

    uint8_t lastNRZI = 1; // initial line state
    const uint32_t bitUs = 1000000UL / _baudRate;
    uint8_t ones = 0;
    uint32_t bitsTransmitted = 0;

    for (size_t i = 0; i < len; ++i) {
        uint8_t b = data[i];
        for (uint8_t bitIdx = 0; bitIdx < 8; ++bitIdx) {
            uint8_t bit = (b >> bitIdx) & 0x01; // LSB-first per AX.25
            if (bit == 1) {
                ones++;
            } else {
                ones = 0;
            }
            // NRZI encode: 0 = toggle, 1 = no change
            if (bit == 0) {
                lastNRZI = !lastNRZI;
            }
            uint16_t freq = lastNRZI ? _markFreq : _spaceFreq;
            ledcWriteTone(_ledcChannel, freq);
            delayMicroseconds(bitUs);
            bitsTransmitted++;

            // Bit-stuff after five consecutive ones: insert a 0 (toggle)
            if (ones == 5) {
                ones = 0;
                lastNRZI = !lastNRZI;
                freq = lastNRZI ? _markFreq : _spaceFreq;
                ledcWriteTone(_ledcChannel, freq);
                delayMicroseconds(bitUs);
                bitsTransmitted++;
            }

            // Yield to RTOS every ~100 bits (~83ms at 1200 baud) to prevent watchdog timeout
            if (bitsTransmitted % 100 == 0) {
                vTaskDelay(1);
            }
        }
    }

    _transmitting = false;
    // Stop tone
    ledcWriteTone(_ledcChannel, 0);
    return true;
}

bool AudioModem::receive(std::vector<uint8_t>& output) {
    if (_rxFrames.empty()) {
        output.clear();
        return false;
    }
    output = _rxFrames.front();
    _rxFrames.pop();
    return true;
}

// Call this with each ADC sample at the configured sample rate
void AudioModem::processAudioSample(int16_t sample) {
    // Normalize sample to [-1,1]
    float s = sample / 32768.0f;
    _goertzelMark.feed(s);
    _goertzelSpace.feed(s);
    _sampleCount++;

    if (_sampleCount >= _samplesPerBit) {
        float pm = _goertzelMark.power();
        float ps = _goertzelSpace.power();
        uint8_t lineState = (pm >= ps) ? 1 : 0; // NRZI line state

        // NRZI decode
        if (!_nrziInitialized) {
            _lastNRZIState = lineState;
            _nrziInitialized = true;
        }
        uint8_t decodedBit = (_lastNRZIState == lineState) ? 1 : 0;
        _lastNRZIState = lineState;

        processDecodedBit(decodedBit);

        // Reset Goertzel for next symbol
        _goertzelMark.reset();
        _goertzelSpace.reset();
        _sampleCount = 0;
    }
}

void AudioModem::processDecodedBit(uint8_t bit) {
    // HDLC/AX.25 bit processing with bit-stuff removal and flag detection
    // Track consecutive ones
    if (bit == 1) {
        _onesCount++;
    } else {
        // If we saw exactly 5 ones then this zero is a stuffed bit -> skip
        if (_onesCount == 5) {
            _onesCount = 0;
            return;
        }
        // Flag detection: six ones followed by zero (01111110)
        if (_onesCount == 6) {
            // Flag encountered
            if (_inFrame && _rxBitPos == 0 && !_rxBuffer.empty()) {
                finalizeFrame();
            }
            _inFrame = true;
            _rxBuffer.clear();
            _rxCurrentByte = 0;
            _rxBitPos = 0;
        }
        _onesCount = 0;
    }

    if (!_inFrame) return;

    // Accumulate bits into bytes (LSB-first)
    _rxCurrentByte |= (bit << _rxBitPos);
    _rxBitPos++;
    if (_rxBitPos == 8) {
        _rxBuffer.push_back(_rxCurrentByte);
        _rxCurrentByte = 0;
        _rxBitPos = 0;
    }

    // Abort if too many ones (7 ones indicates abort)
    if (_onesCount >= 7) {
        _inFrame = false;
        _rxBuffer.clear();
        _rxBitPos = 0;
        _onesCount = 0;
    }
}

void AudioModem::finalizeFrame() {
    // Expect at least FCS (2 bytes)
    if (_rxBuffer.size() < 3) { // minimal payload + FCS
        _rxBuffer.clear();
        return;
    }
    // Remove trailing flag byte if present (0x7E)
    if (_rxBuffer.back() == 0x7E) {
        _rxBuffer.pop_back();
    }

    // Validate FCS (last two bytes LSB first)
    if (_rxBuffer.size() < 3) {
        _rxBuffer.clear();
        return;
    }
    size_t frameLen = _rxBuffer.size();
    uint16_t receivedFcs = _rxBuffer[frameLen - 2] | (_rxBuffer[frameLen - 1] << 8);
    std::vector<uint8_t> payload(_rxBuffer.begin(), _rxBuffer.end() - 2);

    uint16_t calcFcs = 0xFFFF;
    // CRC-16-CCITT (reversed) same as AX.25
    for (uint8_t b : payload) {
        calcFcs ^= b;
        for (int i = 0; i < 8; ++i) {
            if (calcFcs & 0x0001) {
                calcFcs = (calcFcs >> 1) ^ 0x8408;
            } else {
                calcFcs >>= 1;
            }
        }
    }
    calcFcs ^= 0xFFFF;

    if (calcFcs == receivedFcs) {
        _rxFrames.push(payload);
    }
    _rxBuffer.clear();
    _inFrame = false;
    _rxBitPos = 0;
    _onesCount = 0;
}
