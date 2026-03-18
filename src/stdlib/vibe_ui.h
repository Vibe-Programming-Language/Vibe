#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <cstdint>

namespace vibe::ui {

// ═══════════════════════════════════════════════════════════════
//  Vibe UI Framework - Cross-platform GUI primitives
// ═══════════════════════════════════════════════════════════════

// Color representation (RGBA)
struct Color {
  uint8_t r = 255, g = 255, b = 255, a = 255;
  
  Color() = default;
  Color(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255)
      : r(r_), g(g_), b(b_), a(a_) {}
  
  // Standard colors
  static const Color Black;
  static const Color White;
  static const Color Red;
  static const Color Green;
  static const Color Blue;
  static const Color Yellow;
  static const Color Cyan;
  static const Color Magenta;
  static const Color Gray;
};

// Size/dimension structure
struct Size {
  int width = 0, height = 0;
  Size() = default;
  Size(int w, int h) : width(w), height(h) {}
};

// Position structure
struct Pos {
  int x = 0, y = 0;
  Pos() = default;
  Pos(int x_, int y_) : x(x_), y(y_) {}
};

// Rectangle bounds
struct Rect {
  int x = 0, y = 0, width = 0, height = 0;
  Rect() = default;
  Rect(int x_, int y_, int w, int h) : x(x_), y(y_), width(w), height(h) {}
};

// Event types for user input
enum class EventType {
  Unknown,
  KeyDown,
  KeyUp,
  MouseDown,
  MouseUp,
  MouseMove,
  MouseWheel,
  WindowResize,
  WindowClose,
  Custom,
};

// Keyboard event
struct KeyEvent {
  int keyCode = 0;
  char character = '\0';
  bool shift = false;
  bool ctrl = false;
  bool alt = false;
};

// Mouse event
struct MouseEvent {
  int x = 0, y = 0;
  int button = 0;  // 0=left, 1=middle, 2=right
  int wheelDelta = 0;
};

// Generic event
struct Event {
  EventType type = EventType::Unknown;
  KeyEvent key;
  MouseEvent mouse;
  Size windowSize;
};

// Event handler callback
using EventHandler = std::function<void(const Event&)>;

// ═══════════════════════════════════════════════════════════════
//  Widget Base Class
// ═══════════════════════════════════════════════════════════════

class Widget {
 protected:
  Rect bounds_;
  Color bgColor_ = Color::White;
  Color fgColor_ = Color::Black;
  bool visible_ = true;
  std::vector<EventHandler> handlers_;

 public:
  Widget() = default;
  virtual ~Widget() = default;

  // Position & size
  void setPosition(int x, int y) { bounds_.x = x; bounds_.y = y; }
  void setSize(int w, int h) { bounds_.width = w; bounds_.height = h; }
  void setBounds(const Rect& r) { bounds_ = r; }
  Rect getBounds() const { return bounds_; }

  // Colors
  void setBackgroundColor(const Color& c) { bgColor_ = c; }
  void setForegroundColor(const Color& c) { fgColor_ = c; }

  // Visibility
  void setVisible(bool v) { visible_ = v; }
  bool isVisible() const { return visible_; }

  // Event handling
  void subscribe(EventHandler handler) { handlers_.push_back(handler); }
  virtual void handleEvent(const Event& ev) {
    for (auto& h : handlers_) h(ev);
  }

  // Virtual methods for rendering
  virtual void render() = 0;
  virtual bool hitTest(int x, int y) const {
    return x >= bounds_.x && x < bounds_.x + bounds_.width &&
           y >= bounds_.y && y < bounds_.y + bounds_.height;
  }
};

// ═══════════════════════════════════════════════════════════════
//  Canvas - Raw drawing surface
// ═══════════════════════════════════════════════════════════════

class Canvas : public Widget {
 private:
  std::vector<uint32_t> pixels_;
  
 public:
  Canvas(int width = 800, int height = 600);
  ~Canvas() = default;

  // Drawing primitives
  void clear(const Color& c = Color::White);
  void drawPixel(int x, int y, const Color& c);
  void drawLine(int x1, int y1, int x2, int y2, const Color& c);
  void drawRect(int x, int y, int w, int h, const Color& c, bool fill = false);
  void drawCircle(int cx, int cy, int r, const Color& c, bool fill = false);
  void drawText(int x, int y, const std::string& text, const Color& c);

  // Get pixel data
  const uint32_t* getPixelData() const { return pixels_.data(); }

  void render() override;
};

// ═══════════════════════════════════════════════════════════════
//  Button Widget
// ═══════════════════════════════════════════════════════════════

class Button : public Widget {
 private:
  std::string label_;
  std::function<void()> onClick_;
  bool pressed_ = false;

 public:
  Button(const std::string& label = "Button", int w = 100, int h = 40);
  ~Button() = default;

  void setLabel(const std::string& l) { label_ = l; }
  std::string getLabel() const { return label_; }

  void setOnClick(std::function<void()> handler) { onClick_ = handler; }

  void handleEvent(const Event& ev) override;
  void render() override;
};

// ═══════════════════════════════════════════════════════════════
//  TextBox Widget
// ═══════════════════════════════════════════════════════════════

class TextBox : public Widget {
 private:
  std::string text_;
  int cursorPos_ = 0;
  bool focused_ = false;
  int maxLength_ = 256;

 public:
  TextBox(int w = 200, int h = 30);
  ~TextBox() = default;

  void setText(const std::string& t) { text_ = t; cursorPos_ = t.length(); }
  std::string getText() const { return text_; }

  void setMaxLength(int len) { maxLength_ = len; }
  void setFocused(bool f) { focused_ = f; }
  bool isFocused() const { return focused_; }

  void handleEvent(const Event& ev) override;
  void render() override;
};

// ═══════════════════════════════════════════════════════════════
//  Label Widget (static text)
// ═══════════════════════════════════════════════════════════════

class Label : public Widget {
 private:
  std::string text_;

 public:
  Label(const std::string& text = "", int w = 200, int h = 30);
  ~Label() = default;

  void setText(const std::string& t) { text_ = t; }
  std::string getText() const { return text_; }

  void render() override;
};

// ═══════════════════════════════════════════════════════════════
//  Panel - Container for other widgets
// ═══════════════════════════════════════════════════════════════

class Panel : public Widget {
 private:
  std::vector<std::shared_ptr<Widget>> children_;

 public:
  Panel(int w = 400, int h = 300);
  ~Panel() = default;

  void addChild(std::shared_ptr<Widget> child) {
    children_.push_back(child);
  }

  void handleEvent(const Event& ev) override {
    Widget::handleEvent(ev);
    for (auto& child : children_) {
      if (child->hitTest(ev.mouse.x, ev.mouse.y)) {
        child->handleEvent(ev);
      }
    }
  }

  void render() override;
  std::vector<std::shared_ptr<Widget>>& getChildren() { return children_; }
};

// ═══════════════════════════════════════════════════════════════
//  Main Window/Frame
// ═══════════════════════════════════════════════════════════════

class Window {
 private:
  std::string title_;
  int width_ = 800;
  int height_ = 600;
  std::vector<std::shared_ptr<Widget>> widgets_;
  std::function<void()> updateCallback_;
  std::function<void(const Event&)> eventCallback_;
  bool running_ = true;
  void* platformHandle_ = nullptr;  // Platform-specific window handle

 public:
  Window(const std::string& title = "Vibe Application",
         int width = 800, int height = 600);
  ~Window();

  // Properties
  void setTitle(const std::string& t) { title_ = t; }
  std::string getTitle() const { return title_; }

  void setSize(int w, int h) { width_ = w; height_ = h; }
  Size getSize() const { return Size(width_, height_); }

  // Widget management
  void addWidget(std::shared_ptr<Widget> widget) {
    widgets_.push_back(widget);
  }

  void clearWidgets() { widgets_.clear(); }

  // Callbacks
  void setUpdateCallback(std::function<void()> cb) { updateCallback_ = cb; }
  void setEventCallback(std::function<void(const Event&)> cb) { eventCallback_ = cb; }

  // Main loop
  void show();
  void close() { running_ = false; }
  bool isRunning() const { return running_; }

  // Rendering
  void renderFrame();

  // Direct access
  std::vector<std::shared_ptr<Widget>>& getWidgets() { return widgets_; }
};

// ═══════════════════════════════════════════════════════════════
//  Layout helpers
// ═══════════════════════════════════════════════════════════════

class VBoxLayout {
 public:
  static void layout(Panel& panel, int spacing = 5);
};

class HBoxLayout {
 public:
  static void layout(Panel& panel, int spacing = 5);
};

}  // namespace vibe::ui
