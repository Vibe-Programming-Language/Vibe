#include "vibe_ui.h"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace vibe::ui {

// ═══════════════════════════════════════════════════════════════
//  Color definitions
// ═══════════════════════════════════════════════════════════════

const Color Color::Black{0, 0, 0, 255};
const Color Color::White{255, 255, 255, 255};
const Color Color::Red{255, 0, 0, 255};
const Color Color::Green{0, 255, 0, 255};
const Color Color::Blue{0, 0, 255, 255};
const Color Color::Yellow{255, 255, 0, 255};
const Color Color::Cyan{0, 255, 255, 255};
const Color Color::Magenta{255, 0, 255, 255};
const Color Color::Gray{128, 128, 128, 255};

// ═══════════════════════════════════════════════════════════════
//  Canvas implementation
// ═══════════════════════════════════════════════════════════════

Canvas::Canvas(int width, int height) {
  bounds_.width = width;
  bounds_.height = height;
  pixels_.resize(width * height, 0xFFFFFFFF);
}

void Canvas::clear(const Color& c) {
  uint32_t rgba = ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) |
                  ((uint32_t)c.b) | ((uint32_t)c.a << 24);
  std::fill(pixels_.begin(), pixels_.end(), rgba);
}

void Canvas::drawPixel(int x, int y, const Color& c) {
  if (x >= 0 && x < bounds_.width && y >= 0 && y < bounds_.height) {
    uint32_t rgba = ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) |
                    ((uint32_t)c.b) | ((uint32_t)c.a << 24);
    pixels_[y * bounds_.width + x] = rgba;
  }
}

void Canvas::drawLine(int x1, int y1, int x2, int y2, const Color& c) {
  // Bresenham's line algorithm
  int dx = abs(x2 - x1);
  int dy = abs(y2 - y1);
  int sx = (x1 < x2) ? 1 : -1;
  int sy = (y1 < y2) ? 1 : -1;
  int err = dx - dy;

  int x = x1, y = y1;
  while (true) {
    drawPixel(x, y, c);
    if (x == x2 && y == y2) break;
    int e2 = 2 * err;
    if (e2 > -dy) { err -= dy; x += sx; }
    if (e2 < dx) { err += dx; y += sy; }
  }
}

void Canvas::drawRect(int x, int y, int w, int h, const Color& c, bool fill) {
  if (fill) {
    for (int yy = y; yy < y + h; ++yy) {
      for (int xx = x; xx < x + w; ++xx) {
        drawPixel(xx, yy, c);
      }
    }
  } else {
    drawLine(x, y, x + w, y, c);
    drawLine(x + w, y, x + w, y + h, c);
    drawLine(x + w, y + h, x, y + h, c);
    drawLine(x, y + h, x, y, c);
  }
}

void Canvas::drawCircle(int cx, int cy, int r, const Color& c, bool fill) {
  // Midpoint circle algorithm
  if (fill) {
    for (int y = -r; y <= r; ++y) {
      for (int x = -r; x <= r; ++x) {
        if (x * x + y * y <= r * r) {
          drawPixel(cx + x, cy + y, c);
        }
      }
    }
  } else {
    int x = r, y = 0;
    int d = 3 - 2 * r;
    while (x >= y) {
      drawPixel(cx + x, cy + y, c);
      drawPixel(cx - x, cy + y, c);
      drawPixel(cx + x, cy - y, c);
      drawPixel(cx - x, cy - y, c);
      drawPixel(cx + y, cy + x, c);
      drawPixel(cx - y, cy + x, c);
      drawPixel(cx + y, cy - x, c);
      drawPixel(cx - y, cy - x, c);
      if (d < 0) {
        d = d + 4 * y + 6;
      } else {
        d = d + 4 * (y - x) + 10;
        x--;
      }
      y++;
    }
  }
}

void Canvas::drawText(int x, int y, const std::string& text, const Color& c) {
  // Simple text rendering (placeholder - in real implementation, use a font library)
  // For now, just draw a small rectangle for each character
  for (size_t i = 0; i < text.length(); ++i) {
    drawRect(x + i * 6, y, 4, 8, c, true);
  }
}

void Canvas::render() {
  if (!visible_) return;
  // Rendering handled by Window class
}

// ═══════════════════════════════════════════════════════════════
//  Button implementation
// ═══════════════════════════════════════════════════════════════

Button::Button(const std::string& label, int w, int h) : label_(label) {
  bounds_.width = w;
  bounds_.height = h;
  bgColor_ = Color::Gray;
  fgColor_ = Color::White;
}

void Button::handleEvent(const Event& ev) {
  Widget::handleEvent(ev);
  
  if (ev.type == EventType::MouseDown && hitTest(ev.mouse.x, ev.mouse.y)) {
    pressed_ = true;
  } else if (ev.type == EventType::MouseUp && pressed_) {
    pressed_ = false;
    if (onClick_) onClick_();
  }
}

void Button::render() {
  if (!visible_) return;
  // Rendering handled by Window class
}

// ═══════════════════════════════════════════════════════════════
//  TextBox implementation
// ═══════════════════════════════════════════════════════════════

TextBox::TextBox(int w, int h) {
  bounds_.width = w;
  bounds_.height = h;
  bgColor_ = Color::White;
  fgColor_ = Color::Black;
}

void TextBox::handleEvent(const Event& ev) {
  Widget::handleEvent(ev);
  
  if (ev.type == EventType::MouseDown && hitTest(ev.mouse.x, ev.mouse.y)) {
    focused_ = true;
  } else if (ev.type == EventType::MouseDown) {
    focused_ = false;
  }

  if (focused_ && ev.type == EventType::KeyDown) {
    if (ev.key.keyCode == 8) {  // Backspace
      if (cursorPos_ > 0) {
        text_.erase(cursorPos_ - 1, 1);
        cursorPos_--;
      }
    } else if (ev.key.keyCode == 46) {  // Delete
      if (cursorPos_ < (int)text_.length()) {
        text_.erase(cursorPos_, 1);
      }
    } else if (ev.key.character >= 32 && text_.length() < (size_t)maxLength_) {
      text_.insert(cursorPos_, 1, ev.key.character);
      cursorPos_++;
    }
  }
}

void TextBox::render() {
  if (!visible_) return;
  // Rendering handled by Window class
}

// ═══════════════════════════════════════════════════════════════
//  Label implementation
// ═══════════════════════════════════════════════════════════════

Label::Label(const std::string& text, int w, int h) : text_(text) {
  bounds_.width = w;
  bounds_.height = h;
}

void Label::render() {
  if (!visible_) return;
  // Rendering handled by Window class
}

// ═══════════════════════════════════════════════════════════════
//  Panel implementation
// ═══════════════════════════════════════════════════════════════

Panel::Panel(int w, int h) {
  bounds_.width = w;
  bounds_.height = h;
  bgColor_ = Color::White;
}

void Panel::render() {
  if (!visible_) return;
  for (auto& child : children_) {
    child->render();
  }
}

// ═══════════════════════════════════════════════════════════════
//  Window implementation
// ═══════════════════════════════════════════════════════════════

Window::Window(const std::string& title, int width, int height)
    : title_(title), width_(width), height_(height) {
  std::cout << "[UI] Created window: " << title_ << " (" << width_ << "x"
            << height_ << ")\n";
}

Window::~Window() {
  std::cout << "[UI] Destroyed window: " << title_ << "\n";
}

void Window::show() {
  std::cout << "[UI] Window::show() called - running main loop\n";
  running_ = true;
  while (running_) {
    renderFrame();
    if (updateCallback_) updateCallback_();
  }
}

void Window::renderFrame() {
  // Render all widgets
  for (auto& widget : widgets_) {
    widget->render();
  }
}

// ═══════════════════════════════════════════════════════════════
//  Layout helpers
// ═══════════════════════════════════════════════════════════════

void VBoxLayout::layout(Panel& panel, int spacing) {
  auto& children = panel.getChildren();
  int y = panel.getBounds().y + spacing;
  for (auto& child : children) {
    child->setPosition(panel.getBounds().x + spacing, y);
    y += child->getBounds().height + spacing;
  }
}

void HBoxLayout::layout(Panel& panel, int spacing) {
  auto& children = panel.getChildren();
  int x = panel.getBounds().x + spacing;
  for (auto& child : children) {
    child->setPosition(x, panel.getBounds().y + spacing);
    x += child->getBounds().width + spacing;
  }
}

}  // namespace vibe::ui
