#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include <cstdint>

namespace vibe::web {

// ═══════════════════════════════════════════════════════════════
//  VibeWeb - Web Framework (Flask-like)
// ═══════════════════════════════════════════════════════════════

// HTTP Methods
enum class Method {
  GET,
  POST,
  PUT,
  DELETE,
  PATCH,
  OPTIONS,
  HEAD,
};

std::string methodToString(Method m);
Method stringToMethod(const std::string& m);

// ═══════════════════════════════════════════════════════════════
//  Request & Response
// ═══════════════════════════════════════════════════════════════

struct Request {
  Method method;
  std::string path;
  std::string query;
  std::map<std::string, std::string> headers;
  std::map<std::string, std::string> params;   // URL parameters
  std::string body;
  std::string contentType;
  std::string remoteAddr;
  uint16_t remotePort;

  // Helper methods
  std::string getHeader(const std::string& name);
  std::string getParam(const std::string& name);
  std::string getQuery(const std::string& name);
  std::map<std::string, std::string> parseFormData();
  std::string parseJSON();  // Returns raw JSON; user parses with own library
};

struct Response {
  int statusCode = 200;
  std::map<std::string, std::string> headers;
  std::string body;

  Response() = default;

  void setStatus(int code) { statusCode = code; }
  void setHeader(const std::string& name, const std::string& value) {
    headers[name] = value;
  }

  void setContentType(const std::string& type) {
    setHeader("Content-Type", type);
  }

  void setBody(const std::string& b) { body = b; }

  void html(const std::string& content) {
    setContentType("text/html; charset=utf-8");
    setBody(content);
  }

  void json(const std::string& content) {
    setContentType("application/json");
    setBody(content);
  }

  void text(const std::string& content) {
    setContentType("text/plain");
    setBody(content);
  }

  void file(const std::string& filePath);
};

// ═══════════════════════════════════════════════════════════════
//  Route Handler
// ═══════════════════════════════════════════════════════════════

using RouteHandler = std::function<void(const Request&, Response&)>;

struct Route {
  Method method;
  std::string path;
  RouteHandler handler;
  
  // Pattern matching for path parameters e.g. "/user/<id>"
  bool matches(Method m, const std::string& p) const;
  std::map<std::string, std::string> extractParams(const std::string& p) const;
};

// ═══════════════════════════════════════════════════════════════
//  Middleware
// ═══════════════════════════════════════════════════════════════

using Middleware = std::function<bool(Request&, Response&)>;

// ═══════════════════════════════════════════════════════════════
//  Main Application Server
// ═══════════════════════════════════════════════════════════════

class App {
 private:
  std::vector<Route> routes_;
  std::vector<Middleware> middlewares_;
  std::string host_ = "localhost";
  uint16_t port_ = 8080;
  std::function<void(const std::string&)> logger_;
  bool running_ = false;

 public:
  App() = default;
  ~App() = default;

  // Route registration
  void get(const std::string& path, RouteHandler handler) {
    routes_.push_back({Method::GET, path, handler});
  }

  void post(const std::string& path, RouteHandler handler) {
    routes_.push_back({Method::POST, path, handler});
  }

  void put(const std::string& path, RouteHandler handler) {
    routes_.push_back({Method::PUT, path, handler});
  }

  void del(const std::string& path, RouteHandler handler) {
    routes_.push_back({Method::DELETE, path, handler});
  }

  void patch(const std::string& path, RouteHandler handler) {
    routes_.push_back({Method::PATCH, path, handler});
  }

  // Middleware
  void use(Middleware m) { middlewares_.push_back(m); }

  // Configuration
  void setHost(const std::string& host) { host_ = host; }
  void setPort(uint16_t port) { port_ = port; }
  void setLogger(std::function<void(const std::string&)> log) {
    logger_ = log;
  }

  // Server control
  void listen();
  void run();
  void stop() { running_ = false; }

  // Route matching
  bool dispatch(const Request& req, Response& res);

 private:
  void log(const std::string& message);
};

// ═══════════════════════════════════════════════════════════════
//  Built-in Middleware
// ═══════════════════════════════════════════════════════════════

namespace middleware {
  // CORS middleware
  Middleware cors(const std::string& allowOrigin = "*");

  // Request logging middleware
  Middleware logging();

  // JSON body parser middleware
  Middleware jsonParser();

  // Form data parser middleware
  Middleware formParser();

  // Static file serving
  Middleware staticFiles(const std::string& directory, const std::string& basePath = "/static");

  // Rate limiting
  Middleware rateLimit(int requestsPerSecond);
}

// ═══════════════════════════════════════════════════════════════
//  Template Engine (Embedded HTML with Vibe variables)
// ═══════════════════════════════════════════════════════════════

class Template {
 private:
  std::string html_;
  std::map<std::string, std::string> variables_;

 public:
  Template(const std::string& html) : html_(html) {}

  void set(const std::string& var, const std::string& value) {
    variables_[var] = value;
  }

  void set(const std::string& var, int value) {
    variables_[var] = std::to_string(value);
  }

  void set(const std::string& var, double value) {
    variables_[var] = std::to_string(value);
  }

  std::string render();
};

// ═══════════════════════════════════════════════════════════════
//  Utilities
// ═══════════════════════════════════════════════════════════════

namespace util {
  // URL encoding/decoding
  std::string urlEncode(const std::string& str);
  std::string urlDecode(const std::string& str);

  // JSON helpers (basic)
  std::string jsonEscape(const std::string& str);

  // HTTP status descriptions
  std::string getStatusDescription(int code);

  // MIME type detection
  std::string getMimeType(const std::string& filePath);
}

}  // namespace vibe::web
