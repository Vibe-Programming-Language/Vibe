#include "vibe_hardware.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>

#if defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

#if defined(ARDUINO)
#include <Arduino.h>
#endif

namespace vibe::hardware {

namespace {

#if defined(__linux__)
std::string gpioBasePath(int pin) {
  return "/sys/class/gpio/gpio" + std::to_string(pin);
}

bool fileExists(const std::string& path) {
  std::ifstream f(path);
  return f.good();
}

bool writeTextFile(const std::string& path, const std::string& value) {
  std::ofstream out(path);
  if (!out) {
    return false;
  }
  out << value;
  return static_cast<bool>(out);
}

std::string readTextFile(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    return "";
  }
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool exportGpioIfNeeded(int pin) {
  const std::string pinPath = gpioBasePath(pin);
  if (fileExists(pinPath + "/value")) {
    return true;
  }
  if (!writeTextFile("/sys/class/gpio/export", std::to_string(pin))) {
    return false;
  }
  for (int i = 0; i < 10; i++) {
    if (fileExists(pinPath + "/value")) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return false;
}

speed_t baudToTermios(uint32_t baud) {
  switch (baud) {
    case 1200: return B1200;
    case 2400: return B2400;
    case 4800: return B4800;
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    default: return B115200;
  }
}
#endif

} // namespace

// ═══════════════════════════════════════════════════════════════
//  GPIO Implementation
// ═══════════════════════════════════════════════════════════════

void GPIO::pinMode(GPIO_Mode m) {
  mode_ = m;

#if defined(ARDUINO)
  uint8_t arduinoMode = INPUT;
  if (m == GPIO_Mode::OUTPUT || m == GPIO_Mode::PWM) arduinoMode = OUTPUT;
  if (m == GPIO_Mode::INPUT_PULLUP) arduinoMode = INPUT_PULLUP;
  ::pinMode(pin_, arduinoMode);
  return;
#elif defined(__linux__)
  if (!exportGpioIfNeeded(pin_)) {
    std::cerr << "[GPIO] Failed to export pin " << pin_ << "\n";
    return;
  }

  std::string direction = "in";
  if (m == GPIO_Mode::OUTPUT || m == GPIO_Mode::PWM) direction = "out";
  if (!writeTextFile(gpioBasePath(pin_) + "/direction", direction)) {
    std::cerr << "[GPIO] Failed to set direction on pin " << pin_ << "\n";
  }

  if (m == GPIO_Mode::INPUT_PULLUP || m == GPIO_Mode::INPUT_PULLDOWN) {
    std::cerr << "[GPIO] Pull-up/pull-down via sysfs is board-specific; configure with pinctrl/gpio utility if needed\n";
  }
  return;
#else
  std::cout << "[GPIO] Pin " << pin_ << " mode set to "
            << static_cast<int>(m) << "\n";
#endif
}

void GPIO::pinModeAnalog() {
  pinMode(GPIO_Mode::ANALOG);
}

void GPIO::pinModePWM() {
  pinMode(GPIO_Mode::PWM);
}

void GPIO::digitalWrite(PinLevel level) {
  level_ = level;

#if defined(ARDUINO)
  ::digitalWrite(pin_, level == PinLevel::HIGH ? HIGH : LOW);
  return;
#elif defined(__linux__)
  if (!exportGpioIfNeeded(pin_)) {
    std::cerr << "[GPIO] Failed to export pin " << pin_ << "\n";
    return;
  }
  writeTextFile(gpioBasePath(pin_) + "/value", level == PinLevel::HIGH ? "1" : "0");
  return;
#else
  std::cout << "[GPIO] Pin " << pin_ << " <- "
            << (level == PinLevel::HIGH ? "HIGH" : "LOW") << "\n";
#endif
}

PinLevel GPIO::digitalRead() {
#if defined(ARDUINO)
  int val = ::digitalRead(pin_);
  level_ = (val == HIGH) ? PinLevel::HIGH : PinLevel::LOW;
  return level_;
#elif defined(__linux__)
  if (!exportGpioIfNeeded(pin_)) {
    std::cerr << "[GPIO] Failed to export pin " << pin_ << "\n";
    return level_;
  }
  const std::string raw = readTextFile(gpioBasePath(pin_) + "/value");
  level_ = (!raw.empty() && raw[0] == '1') ? PinLevel::HIGH : PinLevel::LOW;
  return level_;
#else
  std::cout << "[GPIO] Pin " << pin_ << " read -> "
            << (level_ == PinLevel::HIGH ? "HIGH" : "LOW") << "\n";
  return level_;
#endif
}

void GPIO::analogWrite(int value) {
#if defined(ARDUINO)
  ::analogWrite(pin_, value);
  return;
#elif defined(__linux__)
  pwm_duty_cycle_ = std::max(0, std::min(value, 255));
  return;
#else
  std::cout << "[GPIO] Pin " << pin_ << " analog write: " << value << "\n";
#endif
}

int GPIO::analogRead() {
#if defined(ARDUINO)
  return ::analogRead(pin_);
#elif defined(__linux__)
  // Generic Linux IIO fallback (board specific channel mapping).
  const std::string path = "/sys/bus/iio/devices/iio:device0/in_voltage" + std::to_string(pin_) + "_raw";
  const std::string raw = readTextFile(path);
  if (!raw.empty()) {
    return std::atoi(raw.c_str());
  }
  return 0;
#else
  std::cout << "[GPIO] Pin " << pin_ << " analog read\n";
  return 512;  // midpoint
#endif
}

void GPIO::pwm(int frequency, int dutyCycle) {
#if defined(ARDUINO)
  (void)frequency; // frequency control is board/timer-specific on classic Arduino
  ::analogWrite(pin_, std::max(0, std::min(dutyCycle, 100)) * 255 / 100);
  return;
#elif defined(__linux__)
  pwm_frequency_ = frequency;
  pwm_duty_cycle_ = std::max(0, std::min(dutyCycle, 100));
  // Full Linux PWM chip control varies by board and PWM channel mapping.
  // Store values so code can run portably; users can bind board-specific channels externally.
  return;
#else
  std::cout << "[GPIO] Pin " << pin_ << " PWM: " << frequency << "Hz, "
            << dutyCycle << "% duty\n";
#endif
}

void GPIO::setPWMDutyCycle(int percent) {
#if defined(ARDUINO)
  ::analogWrite(pin_, std::max(0, std::min(percent, 100)) * 255 / 100);
  return;
#elif defined(__linux__)
  pwm_duty_cycle_ = std::max(0, std::min(percent, 100));
  return;
#else
  std::cout << "[GPIO] Pin " << pin_ << " PWM duty cycle: " << percent << "%\n";
#endif
}

uint32_t GPIO::pulseIn(PinLevel level, uint32_t timeoutMicros) {
#if defined(ARDUINO)
  return ::pulseIn(pin_, level == PinLevel::HIGH ? HIGH : LOW, timeoutMicros);
#elif defined(__linux__)
  const auto expected = (level == PinLevel::HIGH) ? '1' : '0';
  const auto start = std::chrono::steady_clock::now();
  const auto timeout = std::chrono::microseconds(timeoutMicros);

  auto timedOut = [&]() {
    return (std::chrono::steady_clock::now() - start) >= timeout;
  };

  while (!timedOut()) {
    const std::string raw = readTextFile(gpioBasePath(pin_) + "/value");
    if (!raw.empty() && raw[0] != expected) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(50));
  }

  while (!timedOut()) {
    const std::string raw = readTextFile(gpioBasePath(pin_) + "/value");
    if (!raw.empty() && raw[0] == expected) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(50));
  }
  if (timedOut()) {
    return 0;
  }

  const auto pulseStart = std::chrono::steady_clock::now();
  while (!timedOut()) {
    const std::string raw = readTextFile(gpioBasePath(pin_) + "/value");
    if (raw.empty() || raw[0] != expected) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(50));
  }

  return static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - pulseStart)
          .count());
#else
  std::cout << "[GPIO] Pin " << pin_ << " pulse measurement (timeout "
            << timeoutMicros << " us)\n";
  return 0;
#endif
}

// ═══════════════════════════════════════════════════════════════
//  Serial Implementation
// ═══════════════════════════════════════════════════════════════

void Serial::begin(uint32_t bauds) {
  baudrate_ = bauds;
#if defined(ARDUINO)
  ::Serial.begin(bauds);
  initialized_ = true;
  return;
#elif defined(__linux__)
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }

  fd_ = ::open(device_.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
  if (fd_ < 0) {
    initialized_ = false;
    std::cerr << "[Serial] Failed to open " << device_ << ": "
              << std::strerror(errno) << "\n";
    return;
  }

  struct termios tty {};
  if (tcgetattr(fd_, &tty) != 0) {
    std::cerr << "[Serial] tcgetattr failed: " << std::strerror(errno) << "\n";
    ::close(fd_);
    fd_ = -1;
    initialized_ = false;
    return;
  }

  cfsetospeed(&tty, baudToTermios(bauds));
  cfsetispeed(&tty, baudToTermios(bauds));

  tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
  tty.c_iflag &= ~IGNBRK;
  tty.c_lflag = 0;
  tty.c_oflag = 0;
  tty.c_cc[VMIN] = 0;
  tty.c_cc[VTIME] = 1;

  tty.c_iflag &= ~(IXON | IXOFF | IXANY);
  tty.c_cflag |= (CLOCAL | CREAD);
  tty.c_cflag &= ~(PARENB | PARODD);
  tty.c_cflag &= ~CSTOPB;
  tty.c_cflag &= ~CRTSCTS;

  if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
    std::cerr << "[Serial] tcsetattr failed: " << std::strerror(errno) << "\n";
    ::close(fd_);
    fd_ = -1;
    initialized_ = false;
    return;
  }

  initialized_ = true;
  return;
#else
  initialized_ = true;
  std::cout << "[Serial] Started at " << bauds << " baud\n";
#endif
}

void Serial::end() {
#if defined(ARDUINO)
  ::Serial.end();
#elif defined(__linux__)
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
#endif
  initialized_ = false;
}

bool Serial::isAvailable() {
  return initialized_;
}

void Serial::write(uint8_t byte) {
#if defined(ARDUINO)
  ::Serial.write(byte);
#elif defined(__linux__)
  if (!initialized_ || fd_ < 0) {
    return;
  }
  (void)::write(fd_, &byte, 1);
#else
  std::cout << "[Serial] TX: 0x" << std::hex << (int)byte << std::dec << "\n";
#endif
}

void Serial::write(const char* str) {
  if (!str) {
    return;
  }
#if defined(ARDUINO)
  ::Serial.print(str);
#elif defined(__linux__)
  if (!initialized_ || fd_ < 0) {
    return;
  }
  (void)::write(fd_, str, std::strlen(str));
#else
  std::cout << "[Serial] TX: " << str << "\n";
#endif
}

void Serial::write(const std::string& str) {
  write(str.c_str());
}

uint8_t Serial::read() {
#if defined(ARDUINO)
  int v = ::Serial.read();
  return v < 0 ? 0 : static_cast<uint8_t>(v);
#elif defined(__linux__)
  if (!initialized_ || fd_ < 0) {
    return 0;
  }
  uint8_t b = 0;
  const auto n = ::read(fd_, &b, 1);
  return n == 1 ? b : 0;
#else
  std::cout << "[Serial] RX\n";
  return 0;
#endif
}

void Serial::println(const std::string& line) {
  write(line);
  write("\n");
}

void Serial::print(const std::string& text) {
  write(text);
}

std::string Serial::readLine(uint32_t timeoutMs) {
#if defined(ARDUINO)
  const uint32_t start = ::millis();
  std::string out;
  while (timeoutMs == 0 || (::millis() - start) < timeoutMs) {
    if (::Serial.available() > 0) {
      int c = ::Serial.read();
      if (c < 0) {
        continue;
      }
      if (c == '\n') {
        break;
      }
      if (c != '\r') {
        out.push_back(static_cast<char>(c));
      }
    }
  }
  return out;
#elif defined(__linux__)
  if (!initialized_ || fd_ < 0) {
    return "";
  }
  const auto start = std::chrono::steady_clock::now();
  std::string out;
  for (;;) {
    char ch = '\0';
    const auto n = ::read(fd_, &ch, 1);
    if (n == 1) {
      if (ch == '\n') {
        break;
      }
      if (ch != '\r') {
        out.push_back(ch);
      }
      continue;
    }
    if (timeoutMs > 0) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start);
      if (elapsed.count() >= static_cast<long long>(timeoutMs)) {
        break;
      }
    }
  }
  return out;
#else
  std::cout << "[Serial] RX line (timeout " << timeoutMs << " ms)\n";
  return "";
#endif
}

void Serial::printf(const char* format, ...) {
  if (!format) {
    return;
  }
  char buf[1024];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  write(buf);
}

void Serial::flush() {
#if defined(ARDUINO)
  ::Serial.flush();
#elif defined(__linux__)
  if (initialized_ && fd_ >= 0) {
    tcdrain(fd_);
  }
#else
  std::cout << "[Serial] flushed\n";
#endif
}

int Serial::available() {
#if defined(ARDUINO)
  return ::Serial.available();
#elif defined(__linux__)
  if (!initialized_ || fd_ < 0) {
    return 0;
  }
  int bytes = 0;
  if (ioctl(fd_, FIONREAD, &bytes) == -1) {
    return 0;
  }
  return bytes;
#else
  return 0;
#endif
}

// ═══════════════════════════════════════════════════════════════
//  I2C Implementation
// ═══════════════════════════════════════════════════════════════

void I2C::begin(uint32_t freq) {
  frequency_ = freq;
  initialized_ = true;
  std::cout << "[I2C] Initialized at " << freq << " Hz (SDA=" << sda_pin_
            << ", SCL=" << scl_pin_ << ")\n";
}

void I2C::end() {
  initialized_ = false;
  std::cout << "[I2C] Closed\n";
}

void I2C::beginTransmission(uint8_t address) {
  std::cout << "[I2C] Begin transmission to 0x" << std::hex << (int)address
            << std::dec << "\n";
}

uint8_t I2C::write(uint8_t byte) {
  std::cout << "[I2C] Write byte 0x" << std::hex << (int)byte << std::dec
            << "\n";
  return 1;
}

uint8_t I2C::write(const uint8_t* data, size_t length) {
  std::cout << "[I2C] Write " << length << " bytes\n";
  return length;
}

uint8_t I2C::endTransmission(bool sendStop) {
  std::cout << "[I2C] End transmission\n";
  return 0;
}

void I2C::requestFrom(uint8_t address, uint8_t quantity) {
  std::cout << "[I2C] Request " << (int)quantity << " bytes from 0x" << std::hex
            << (int)address << std::dec << "\n";
}

int I2C::available() {
  return 0;
}

uint8_t I2C::read() {
  return 0;
}

int I2C::writeRead(uint8_t address, const uint8_t* writeData, size_t writeLen,
                   uint8_t* readData, size_t readLen) {
  std::cout << "[I2C] WriteRead to 0x" << std::hex << (int)address << std::dec
            << ": write " << writeLen << " bytes, read " << readLen << " bytes\n";
  return readLen;
}

bool I2C::isDevicePresent(uint8_t address) {
  std::cout << "[I2C] Scanning for device at 0x" << std::hex << (int)address
            << std::dec << "\n";
  return false;
}

std::vector<uint8_t> I2C::scanBus() {
  std::cout << "[I2C] Scanning bus...\n";
  return {};
}

// ═══════════════════════════════════════════════════════════════
//  SPI Implementation
// ═══════════════════════════════════════════════════════════════

void SPI::begin(uint32_t freq) {
  frequency_ = freq;
  initialized_ = true;
  std::cout << "[SPI] Initialized at " << freq << " Hz (MOSI=" << mosi_pin_
            << ", MISO=" << miso_pin_ << ", CLK=" << clk_pin_
            << ", CS=" << cs_pin_ << ")\n";
}

void SPI::end() {
  initialized_ = false;
  std::cout << "[SPI] Closed\n";
}

void SPI::beginTransaction() {
  std::cout << "[SPI] Begin transaction\n";
}

void SPI::endTransaction() {
  std::cout << "[SPI] End transaction\n";
}

uint8_t SPI::transfer(uint8_t byte) {
  std::cout << "[SPI] Transfer 0x" << std::hex << (int)byte << std::dec << "\n";
  return byte;
}

void SPI::transfer(uint8_t* data, size_t length) {
  std::cout << "[SPI] Transfer " << length << " bytes\n";
}

void SPI::setMode(Mode m) {
  std::cout << "[SPI] Mode set to " << static_cast<int>(m) << "\n";
}

void SPI::chipSelect(bool active) {
  std::cout << "[SPI] Chip select: " << (active ? "active" : "inactive")
            << "\n";
}

// ═══════════════════════════════════════════════════════════════
//  Timing & Delay Functions
// ═══════════════════════════════════════════════════════════════

namespace time {
  uint32_t micros() {
    auto now = std::chrono::high_resolution_clock::now();
    static auto start = now;
    return std::chrono::duration_cast<std::chrono::microseconds>(now - start)
        .count();
  }

  uint32_t millis() {
    return micros() / 1000;
  }

  void delayMicros(uint32_t us) {
    std::this_thread::sleep_for(std::chrono::microseconds(us));
  }

  void delay(uint32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  }
}

// ═══════════════════════════════════════════════════════════════
//  ADC Implementation
// ═══════════════════════════════════════════════════════════════

int ADC::read() {
  std::cout << "[ADC] Pin " << pin_ << " read\n";
  return 512;  // midpoint
}

float ADC::readVoltage(float referenceVoltage) {
  return (float)read() / (1 << resolution_) * referenceVoltage;
}

// ═══════════════════════════════════════════════════════════════
//  PWM Implementation
// ═══════════════════════════════════════════════════════════════

void PWM::set(uint32_t freq, uint8_t duty) {
  frequency_ = freq;
  dutyCycle_ = duty;
  std::cout << "[PWM] Pin " << pin_ << " set to " << freq << "Hz, " << (int)duty
            << "% duty\n";
}

void PWM::setDutyCycle(uint8_t percent) {
  dutyCycle_ = percent;
  std::cout << "[PWM] Pin " << pin_ << " duty cycle: " << (int)percent << "%\n";
}

// ═══════════════════════════════════════════════════════════════
//  Platform Detection
// ═══════════════════════════════════════════════════════════════

namespace platform {
  PlatformType detect() {
#if defined(__AVR__)
    return PlatformType::ARDUINO_UNO;
#elif defined(RASPBERRY_PI)
    return PlatformType::RASPBERRY_PI;
#elif defined(ESP32)
    return PlatformType::ESP32;
#else
    return PlatformType::UNKNOWN;
#endif
  }

  std::string getPlatformName() {
    switch (detect()) {
      case PlatformType::ARDUINO_UNO:
        return "Arduino UNO";
      case PlatformType::RASPBERRY_PI:
        return "Raspberry Pi";
      case PlatformType::ESP32:
        return "ESP32";
      default:
        return "Unknown";
    }
  }

  int getGPIOCount() {
    return 20;  // varies by platform
  }

  int getADCChannels() {
    return 6;
  }

  int getPWMChannels() {
    return 6;
  }

  uint32_t getMaxFrequency() {
    return 16000000;  // 16 MHz typical for Arduino
  }
}

}  // namespace vibe::hardware
