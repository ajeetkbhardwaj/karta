cmake_minimum_required(VERSION 3.16)
project(karta LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(KARTA_BUILD_TUI "Build the terminal UI (ncurses)" OFF)

# Categorized subproject sources (canonical). Public headers: include/karta/<category>/.
add_executable(karta
  src/main.cpp
  src/agent/agent.cpp
  src/agent/prompt.cpp
  src/ai/http.cpp
  src/ai/provider.cpp
  src/cli/cli.cpp
  src/core/auth.cpp
  src/core/id.cpp
  src/core/json.cpp
  src/core/platform.cpp
  src/core/telemetry.cpp
  src/session/session.cpp
  src/tools/tools.cpp
  # MCP: src/mcp/mcp.cpp, src/mcp/stdio_transport.cpp, src/mcp/http_transport.cpp (TODO: full implementation)
)

target_compile_options(karta PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_compile_definitions(karta PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
# Headers from include/ (karta/...). src/ retained for local/private resolution only.
target_include_directories(karta PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cpp-httplib
)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
target_link_libraries(karta PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

if(APPLE)
  find_library(SECURITY Security)
  find_library(COREFOUNDATION CoreFoundation)
  if(SECURITY AND COREFOUNDATION)
    target_link_libraries(karta PRIVATE ${SECURITY} ${COREFOUNDATION})
  endif()
endif()

if(KARTA_BUILD_TUI)
  if(WIN32)
    # Windows: use PDCurses (Windows port of ncurses)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(PDCURSES pdcurses QUIET)
    endif()
    if(NOT PDCURSES_FOUND)
      # Try to find PDCurses manually
      find_library(PDCURSES_LIB pdcurses)
      find_path(PDCURSES_INCLUDE_DIR curses.h)
      if(PDCURSES_LIB AND PDCURSES_INCLUDE_DIR)
        set(PDCURSES_FOUND TRUE)
        set(PDCURSES_LIBRARIES ${PDCURSES_LIB})
        set(PDCURSES_INCLUDE_DIRS ${PDCURSES_INCLUDE_DIR})
      endif()
    endif()
    if(NOT PDCURSES_FOUND)
      message(WARNING "PDCurses not found. TUI requires PDCurses on Windows. Install via: vcpkg install pdcurses or choco install pdcurses")
    else()
      set(NCURSES_FOUND TRUE)
      set(NCURSES_LIBRARIES ${PDCURSES_LIBRARIES})
      set(NCURSES_INCLUDE_DIRS ${PDCURSES_INCLUDE_DIRS})
    endif()
  else()
    # Linux/macOS: use ncurses via pkg-config
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(NCURSES ncursesw QUIET)
    if(NOT NCURSES_FOUND)
      pkg_check_modules(NCURSES ncurses REQUIRED)
    endif()
  endif()

  if(NCURSES_FOUND)
    target_compile_definitions(karta PRIVATE KARTA_TUI)
    target_include_directories(karta PRIVATE ${NCURSES_INCLUDE_DIRS})
    target_link_libraries(karta PRIVATE ${NCURSES_LIBRARIES})
  else()
    message(WARNING "ncurses/PDCurses not found. TUI disabled.")
  endif()
  target_sources(karta PRIVATE
    src/tui/screen.cpp
    src/tui/app.cpp
    src/tui/model-picker.cpp
    src/tui/select-list.cpp
    src/tui/text.cpp
    src/tui/loader.cpp
    src/tui/scroll-view.cpp
    src/tui/editor.cpp
    src/tui/markdown.cpp
    src/tui/clipboard.cpp
  )
  message(STATUS "TUI enabled - `karta` opens the terminal UI when attached to a TTY")
else()
  message(STATUS "TUI disabled - pass -DKARTA_BUILD_TUI=ON to build the terminal UI")
endif()

install(TARGETS karta RUNTIME DESTINATION bin)

# Unit tests
option(KARTA_BUILD_TESTS "Build unit tests" OFF)

if(KARTA_BUILD_TESTS)
  add_executable(karta_tests
    test/unit_tests.cpp
    src/agent/agent.cpp
    src/agent/prompt.cpp
    src/ai/http.cpp
    src/ai/provider.cpp
    src/cli/cli.cpp
    src/core/auth.cpp
    src/core/id.cpp
    src/core/json.cpp
    src/core/platform.cpp
    src/core/telemetry.cpp
    src/session/session.cpp
    src/tools/tools.cpp
    # MCP: src/mcp/mcp.cpp, src/mcp/stdio_transport.cpp, src/mcp/http_transport.cpp (TODO: full implementation)
  )
  target_compile_options(karta_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
  target_compile_definitions(karta_tests PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
  target_include_directories(karta_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cpp-httplib
  )
  find_package(OpenSSL REQUIRED)
  find_package(Threads REQUIRED)
  target_link_libraries(karta_tests PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

  if(APPLE)
    find_library(SECURITY Security)
    find_library(COREFOUNDATION CoreFoundation)
    if(SECURITY AND COREFOUNDATION)
      target_link_libraries(karta_tests PRIVATE ${SECURITY} ${COREFOUNDATION})
    endif()
  endif()
  message(STATUS "Unit tests enabled - run with: ctest or ./karta_tests")
endif()
