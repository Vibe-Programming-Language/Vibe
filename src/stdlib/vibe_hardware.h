#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>

namespace vibe::hardware {

// ═══════════════════════════════════════════════════════════════
//  Vibe Hardware Module - Microcontroller Support
//  Targets: Arduino, Raspberry Pi, STM32, ESP32, etc.
// ═══════════════════════════════════════════════════════════════

// GPIO Modes
enum class GPIO_Mode {
  INPUT,
  OUTPUT,
  INPUT_PULLUP,
  INPUT_PULLDOWN,
  ANALOG,
  PWM,
  SPI,
  I2C,
  UART,
};

// Pin levels
enum class PinLevel {
  LOW = 0,
  HIGH = 1,
};

// ═══════════════════════════════════════════════════════════════
//  GPIO - General Purpose Input/Output
// ═══════════════════════════════════════════════════════════════

class GPIO {
 private:
  int pin_;
  GPIO_Mode mode_;
  PinLevel level_ = PinLevel::LOW;
  int pwm_duty_cycle_ = 0;
  int pwm_frequency_ = 1000;

 public:
  GPIO(int pin) : pin_(pin), mode_(GPIO_Mode::INPUT) {}

  // Configuration
  void mode(GPIO_Mode m) { mode_ = m; }
  GPIO_Mode getMode() const { return mode_; }

  void pinMode(GPIO_Mode m);
  void pinModeAnalog();
  void pinModePWM();

  // Digital I/O
  void digitalWrite(PinLevel level);
  void digitalWrite(bool high) { digitalWrite(high ? PinLevel::HIGH : PinLevel::LOW); }
  PinLevel digitalRead();
  bool isHigh() { return digitalRead() == PinLevel::HIGH; }
  bool isLow() { return digitalRead() == PinLevel::LOW; }

  // Analog I/O (0-1023)
  void analogWrite(int value);   // PWM: 0-255
  int analogRead();               // ADC: 0-1023

  // PWM
  void pwm(int frequency, int dutyCycle);
  void setPWMDutyCycle(int percent);

  // Pin info
  int getPin() const { return pin_; }

  // Pulse measurement
  uint32_t pulseIn(PinLevel level, uint32_t timeoutMicros);
};

// ═══════════════════════════════════════════════════════════════
//  Serial Communication (UART)
// ═══════════════════════════════════════════════════════════════

class Serial {
 private:
  uint32_t baudrate_;
  bool initialized_ = false;
  std::string device_ = "/dev/ttyS0";
  int fd_ = -1;

 public:
  Serial(uint32_t bauds = 9600) : baudrate_(bauds) {}

  void begin(uint32_t bauds);
  void end();
  bool isAvailable();
  
  // Byte-level I/O
  void write(uint8_t byte);
  void write(const char* str);
  void write(const std::string& str);
  uint8_t read();

  // Line-level I/O
  void println(const std::string& line);
  void print(const std::string& text);
  std::string readLine(uint32_t timeoutMs = 0);

  // Formatted I/O
  void printf(const char* format, ...);

  // Buffering
  void flush();
  int available();

  // Baud rate
  void setBaudRate(uint32_t bauds) { baudrate_ = bauds; }
  uint32_t getBaudRate() const { return baudrate_; }

  // Linux serial device selection (e.g. /dev/ttyAMA0, /dev/serial0, /dev/ttyUSB0)
  void setDevice(const std::string& device) { device_ = device; }
  const std::string& getDevice() const { return device_; }
};

// ═══════════════════════════════════════════════════════════════
//  I2C Communication
// ═══════════════════════════════════════════════════════════════

class I2C {
 private:
  int sda_pin_;
  int scl_pin_;
  uint32_t frequency_ = 100000;  // 100 kHz
  bool initialized_ = false;

 public:
  I2C(int sda, int scl) : sda_pin_(sda), scl_pin_(scl) {}

  void begin(uint32_t freq = 100000);
  void end();

  // Master mode
  void beginTransmission(uint8_t address);
  uint8_t write(uint8_t byte);
  uint8_t write(const uint8_t* data, size_t length);
  uint8_t endTransmission(bool sendStop = true);

  void requestFrom(uint8_t address, uint8_t quantity);
  int available();
  uint8_t read();

  // Raw transfer
  int writeRead(uint8_t address, const uint8_t* writeData, size_t writeLen,
                uint8_t* readData, size_t readLen);

  // Scanning
  bool isDevicePresent(uint8_t address);
  std::vector<uint8_t> scanBus();

  uint32_t getFrequency() const { return frequency_; }
};

// ═══════════════════════════════════════════════════════════════
//  SPI Communication
// ═══════════════════════════════════════════════════════════════

class SPI {
 private:
  int mosi_pin_;
  int miso_pin_;
  int clk_pin_;
  int cs_pin_;
  uint32_t frequency_ = 1000000;  // 1 MHz
  bool initialized_ = false;

 public:
  enum class Mode {
    MODE0,  // CPOL=0, CPHA=0
    MODE1,  // CPOL=0, CPHA=1
    MODE2,  // CPOL=1, CPHA=0
    MODE3,  // CPOL=1, CPHA=1
  };

  SPI(int mosi, int miso, int clk, int cs)
      : mosi_pin_(mosi), miso_pin_(miso), clk_pin_(clk), cs_pin_(cs) {}

  void begin(uint32_t freq = 1000000);
  void end();
  void beginTransaction();
  void endTransaction();

  uint8_t transfer(uint8_t byte);
  void transfer(uint8_t* data, size_t length);

  void setMode(Mode m);
  void setFrequency(uint32_t freq) { frequency_ = freq; }
  uint32_t getFrequency() const { return frequency_; }

  void chipSelect(bool active);
};

// ═══════════════════════════════════════════════════════════════
//  Timing & Delays
// ═══════════════════════════════════════════════════════════════

namespace time {
  // Get current time in microseconds
  uint32_t micros();
  
  // Get current time in milliseconds
  uint32_t millis();
  
  // Sleep for specified microseconds
  void delayMicros(uint32_t us);
  
  // Sleep for specified milliseconds
  void delay(uint32_t ms);

  // Timer abstraction
  class Timer {
   private:
    uint32_t interval_;
    uint32_t last_trigger_;
    std::function<void()> callback_;
    bool enabled_ = false;

   public:
    Timer(uint32_t intervalMs) : interval_(intervalMs), last_trigger_(millis()) {}
    
    void setInterval(uint32_t ms) { interval_ = ms; }
    void setCallback(std::function<void()> cb) { callback_ = cb; }
    void start() { enabled_ = true; last_trigger_ = millis(); }
    void stop() { enabled_ = false; }
    
    bool tick() {
      if (!enabled_) return false;
      if (millis() - last_trigger_ >= interval_) {
        last_trigger_ = millis();
        if (callback_) callback_();
        return true;
      }
      return false;
    }
  };
}

// ═══════════════════════════════════════════════════════════════
//  Analog-Digital Converter (ADC)
// ═══════════════════════════════════════════════════════════════

class ADC {
 private:
  int pin_;
  uint16_t resolution_ = 10;  // bits

 public:
  ADC(int pin) : pin_(pin) {}

  void setResolution(uint16_t bits) { resolution_ = bits; }
  uint16_t getResolution() const { return resolution_; }

  int read();
  float readVoltage(float referenceVoltage = 5.0f);
};

// ═══════════════════════════════════════════════════════════════
//  PWM Output
// ═══════════════════════════════════════════════════════════════

class PWM {
 private:
  int pin_;
  uint32_t frequency_;
  uint8_t dutyCycle_;

 public:
  PWM(int pin, uint32_t freq = 1000, uint8_t duty = 0)
      : pin_(pin), frequency_(freq), dutyCycle_(duty) {}

  void set(uint32_t freq, uint8_t duty);
  void setDutyCycle(uint8_t percent);
  uint8_t getDutyCycle() const { return dutyCycle_; }
  
  void setFrequency(uint32_t freq) { frequency_ = freq; }
  uint32_t getFrequency() const { return frequency_; }
};

// ═══════════════════════════════════════════════════════════════
//  Platform Detection & Configuration
// ═══════════════════════════════════════════════════════════════

enum class PlatformType {
  UNKNOWN,
  ARDUINO,
  ARDUINO_UNO,
  ARDUINO_MEGA,
  ARDUINO_NANO,
  RASPBERRY_PI,
  RASPBERRY_PI_PICO,
  ESP32,
  ESP8266,
  STM32,
};

namespace platform {
  PlatformType detect();
  std::string getPlatformName();
  
  // Platform capabilities
  int getGPIOCount();
  int getADCChannels();
  int getPWMChannels();
  uint32_t getMaxFrequency();
}

}  // namespace vibe::hardware
