cmake_minimum_required(VERSION 3.20)
project(vibe VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(vibe
  src/main.cpp
  src/lexer/lexer.cpp
  src/parser/parser.cpp
  src/codegen/codegen.cpp
  src/runtime/runtime.cpp
)

target_include_directories(vibe PRIVATE src)
target_compile_options(vibe PRIVATE -Wall -Wextra -Wpedantic)

find_package(SDL2 REQUIRED)
target_include_directories(vibe PRIVATE ${SDL2_INCLUDE_DIRS})

# Link filesystem (required on some older g++ versions)
target_link_libraries(vibe PRIVATE stdc++fs ${SDL2_LIBRARIES})

# ── Install rules ─────────────────────────────────
install(TARGETS vibe RUNTIME DESTINATION bin)

# Install example files alongside the binary
install(DIRECTORY examples/ DESTINATION share/vibe/examples
  FILES_MATCHING PATTERN "*.vibe")

# ── CPack / .deb packaging ────────────────────────
set(CPACK_PACKAGE_NAME "vibe")
set(CPACK_PACKAGE_VENDOR "Vibe Programming Language")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Vibe — a modern, expressive programming language")
set(CPACK_PACKAGE_DESCRIPTION "Vibe is a modern, expressive programming language that compiles and interprets .vibe source files. It features a tree-walking interpreter, C++ transpiler, interactive REPL, pattern matching, classes, lambdas, and a rich standard library.")
set(CPACK_PACKAGE_CONTACT "codeplanetlabs@gmail.com")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Vibe-Programming-Language/Vibe")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

# Debian-specific
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Vibe Language Team <codeplanetlabs@gmail.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.17), libstdc++6 (>= 7)")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/Vibe-Programming-Language/Vibe")
# Allow dpkg -i to upgrade over previous versions
set(CPACK_DEBIAN_PACKAGE_REPLACES "vibe")
set(CPACK_DEBIAN_PACKAGE_CONFLICTS "")

set(CPACK_GENERATOR "DEB")
include(CPack)

