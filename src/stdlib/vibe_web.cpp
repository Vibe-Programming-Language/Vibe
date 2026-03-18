#include "vibe_web.h"
#include <iostream>
#include <algorithm>
#include <sstream>
#include <fstream>

namespace vibe::web {

// ═══════════════════════════════════════════════════════════════
//  HTTP Method Utilities
// ═══════════════════════════════════════════════════════════════

std::string methodToString(Method m) {
  switch (m) {
    case Method::GET:
      return "GET";
    case Method::POST:
      return "POST";
    case Method::PUT:
      return "PUT";
    case Method::DELETE:
      return "DELETE";
    case Method::PATCH:
      return "PATCH";
    case Method::OPTIONS:
      return "OPTIONS";
    case Method::HEAD:
      return "HEAD";
    default:
      return "UNKNOWN";
  }
}

Method stringToMethod(const std::string& m) {
  if (m == "GET")
    return Method::GET;
  if (m == "POST")
    return Method::POST;
  if (m == "PUT")
    return Method::PUT;
  if (m == "DELETE")
    return Method::DELETE;
  if (m == "PATCH")
    return Method::PATCH;
  if (m == "OPTIONS")
    return Method::OPTIONS;
  if (m == "HEAD")
    return Method::HEAD;
  return Method::GET;
}

// ═══════════════════════════════════════════════════════════════
//  Request Implementation
// ═══════════════════════════════════════════════════════════════

std::string Request::getHeader(const std::string& name) {
  auto it = headers.find(name);
  return it != headers.end() ? it->second : "";
}

std::string Request::getParam(const std::string& name) {
  auto it = params.find(name);
  return it != params.end() ? it->second : "";
}

std::string Request::getQuery(const std::string& name) {
  // Simple query string parsing
  size_t pos = query.find(name + "=");
  if (pos == std::string::npos) return "";
  
  pos += name.length() + 1;
  size_t end = query.find("&", pos);
  if (end == std::string::npos) end = query.length();
  
  return query.substr(pos, end - pos);
}

std::map<std::string, std::string> Request::parseFormData() {
  std::map<std::string, std::string> data;
  std::istringstream iss(body);
  std::string pair;
  
  while (std::getline(iss, pair, '&')) {
    size_t eq = pair.find('=');
    if (eq != std::string::npos) {
      std::string key = pair.substr(0, eq);
      std::string val = pair.substr(eq + 1);
      data[key] = val;
    }
  }
  
  return data;
}

std::string Request::parseJSON() {
  return body;  // User must parse with their own JSON parser
}

// ═══════════════════════════════════════════════════════════════
//  Response Implementation
// ═══════════════════════════════════════════════════════════════

void Response::file(const std::string& filePath) {
  std::ifstream file(filePath, std::ios::binary);
  if (file) {
    std::stringstream buffer;
    buffer << file.rdbuf();
    body = buffer.str();
    setHeader("Content-Length", std::to_string(body.length()));
  } else {
    statusCode = 404;
    text("File not found");
  }
}

// ═══════════════════════════════════════════════════════════════
//  Route Implementation
// ═══════════════════════════════════════════════════════════════

bool Route::matches(Method m, const std::string& p) const {
  if (method != m) return false;
  
  // Simple wildcard matching: /user/<id> matches /user/123
  if (path.find("<") == std::string::npos) {
    return path == p;
  }
  
  // Pattern matching
  std::string pattern = path;
  size_t pos = 0;
  while ((pos = pattern.find("<")) != std::string::npos) {
    size_t end = pattern.find(">", pos);
    pattern.replace(pos, end - pos + 1, "[^/]+");
  }
  
  // Very simple: just check path length compatibility
  return p.length() >= path.length() - 2;
}

std::map<std::string, std::string> Route::extractParams(const std::string& p) const {
  std::map<std::string, std::string> extracted;
  
  // Simple extraction: find values between < >
  std::string pattern = path;
  std::string pathCopy = p;
  
  size_t patPos = 0, pathPos = 0;
  while (patPos < pattern.length()) {
    size_t startTag = pattern.find("<", patPos);
    if (startTag == std::string::npos) break;
    
    size_t endTag = pattern.find(">", startTag);
    std::string varName = pattern.substr(startTag + 1, endTag - startTag - 1);
    
    // Find corresponding value in path
    size_t nextSlash = pathCopy.find("/", pathPos);
    if (nextSlash == std::string::npos) nextSlash = pathCopy.length();
    
    extracted[varName] = pathCopy.substr(pathPos, nextSlash - pathPos);
    
    pathPos = nextSlash + 1;
    patPos = endTag + 1;
  }
  
  return extracted;
}

// ═══════════════════════════════════════════════════════════════
//  App Implementation
// ═══════════════════════════════════════════════════════════════

void App::log(const std::string& message) {
  if (logger_) {
    logger_(message);
  } else {
    std::cout << "[VibeWeb] " << message << "\n";
  }
}

bool App::dispatch(const Request& req, Response& res) {
  // Run middlewares
  Request mutable_req = req;
  for (auto& mw : middlewares_) {
    if (!mw(mutable_req, res)) {
      return false;
    }
  }

  // Find matching route
  for (auto& route : routes_) {
    if (route.matches(req.method, req.path)) {
      std::map<std::string, std::string> params = route.extractParams(req.path);
      
      // Create modified request with parameters
      Request final_req = mutable_req;
      final_req.params = params;
      
      // Call handler
      route.handler(final_req, res);
      return true;
    }
  }

  // No route found
  res.statusCode = 404;
  res.text("Not Found");
  return false;
}

void App::listen() {
  log("Server listening on " + host_ + ":" + std::to_string(port_));
  running_ = true;
}

void App::run() {
  listen();
  while (running_) {
    // Event loop would go here
    // In real implementation: accept connections, parse HTTP, dispatch, send response
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

// ═══════════════════════════════════════════════════════════════
//  Middleware Implementation
// ═══════════════════════════════════════════════════════════════

namespace middleware {
  Middleware cors(const std::string& allowOrigin) {
    return [allowOrigin](Request& req, Response& res) {
      res.setHeader("Access-Control-Allow-Origin", allowOrigin);
      res.setHeader("Access-Control-Allow-Methods",
                    "GET, POST, PUT, DELETE, PATCH, OPTIONS");
      res.setHeader("Access-Control-Allow-Headers", "Content-Type");
      return true;
    };
  }

  Middleware logging() {
    return [](Request& req, Response& res) {
      std::cout << "[HTTP] " << methodToString(req.method) << " " << req.path
                << " from " << req.remoteAddr << "\n";
      return true;
    };
  }

  Middleware jsonParser() {
    return [](Request& req, Response& res) {
      if (req.getHeader("Content-Type").find("application/json") !=
          std::string::npos) {
        req.contentType = "application/json";
      }
      return true;
    };
  }

  Middleware formParser() {
    return [](Request& req, Response& res) {
      if (req.getHeader("Content-Type").find("application/x-www-form-urlencoded") !=
          std::string::npos) {
        req.contentType = "form";
      }
      return true;
    };
  }

  Middleware staticFiles(const std::string& directory,
                        const std::string& basePath) {
    return [directory, basePath](Request& req, Response& res) {
      if (req.path.substr(0, basePath.length()) == basePath) {
        res.file(directory + req.path.substr(basePath.length()));
        return true;
      }
      return true;
    };
  }

  Middleware rateLimit(int requestsPerSecond) {
    return [requestsPerSecond](Request& req, Response& res) {
      // Simple rate limiting placeholder
      return true;
    };
  }
}

// ═══════════════════════════════════════════════════════════════
//  Template Engine Implementation
// ═══════════════════════════════════════════════════════════════

std::string Template::render() {
  std::string result = html_;
  
  for (const auto& [var, value] : variables_) {
    std::string placeholder = "{{" + var + "}}";
    size_t pos = 0;
    while ((pos = result.find(placeholder, pos)) != std::string::npos) {
      result.replace(pos, placeholder.length(), value);
      pos += value.length();
    }
  }
  
  return result;
}

// ═══════════════════════════════════════════════════════════════
//  Utility Functions
// ═══════════════════════════════════════════════════════════════

namespace util {
  std::string urlEncode(const std::string& str) {
    std::string result;
    for (char c : str) {
      if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
          (c >= 'a' && c <= 'z') || c == '-' || c == '_' || c == '.' ||
          c == '~') {
        result += c;
      } else {
        result += '%';
        char hex[3];
        sprintf(hex, "%02X", (unsigned char)c);
        result += hex;
      }
    }
    return result;
  }

  std::string urlDecode(const std::string& str) {
    std::string result;
    for (size_t i = 0; i < str.length(); ++i) {
      if (str[i] == '%' && i + 2 < str.length()) {
        int hex;
        sscanf(str.substr(i + 1, 2).c_str(), "%x", &hex);
        result += static_cast<char>(hex);
        i += 2;
      } else if (str[i] == '+') {
        result += ' ';
      } else {
        result += str[i];
      }
    }
    return result;
  }

  std::string jsonEscape(const std::string& str) {
    std::string result;
    for (char c : str) {
      switch (c) {
        case '"':
          result += "\\\"";
          break;
        case '\\':
          result += "\\\\";
          break;
        case '\n':
          result += "\\n";
          break;
        case '\r':
          result += "\\r";
          break;
        case '\t':
          result += "\\t";
          break;
        default:
          result += c;
      }
    }
    return result;
  }

  std::string getStatusDescription(int code) {
    switch (code) {
      case 200:
        return "OK";
      case 201:
        return "Created";
      case 400:
        return "Bad Request";
      case 401:
        return "Unauthorized";
      case 403:
        return "Forbidden";
      case 404:
        return "Not Found";
      case 500:
        return "Internal Server Error";
      case 503:
        return "Service Unavailable";
      default:
        return "Unknown";
    }
  }

  std::string getMimeType(const std::string& filePath) {
    if (filePath.ends_with(".html"))
      return "text/html";
    if (filePath.ends_with(".json"))
      return "application/json";
    if (filePath.ends_with(".css"))
      return "text/css";
    if (filePath.ends_with(".js"))
      return "application/javascript";
    if (filePath.ends_with(".png"))
      return "image/png";
    if (filePath.ends_with(".jpg") || filePath.ends_with(".jpeg"))
      return "image/jpeg";
    if (filePath.ends_with(".gif"))
      return "image/gif";
    return "application/octet-stream";
  }
}

}  // namespace vibe::web
