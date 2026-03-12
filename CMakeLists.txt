cmake_minimum_required(VERSION 3.20)
project(vibe VERSION 1.0.0 LANGUAGES CXX)

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

# Link filesystem (required on some older g++ versions)
target_link_libraries(vibe PRIVATE stdc++fs)

install(TARGETS vibe RUNTIME DESTINATION bin)

# ── CPack / .deb packaging ────────────────────────
set(CPACK_PACKAGE_NAME "vibe")
set(CPACK_PACKAGE_VENDOR "Vibe Language")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Vibe — a modern, expressive programming language")
set(CPACK_PACKAGE_CONTACT "vibe-lang@example.com")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Vibe Language Team")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.17), libstdc++6 (>= 7)")
set(CPACK_GENERATOR "DEB")
include(CPack)

