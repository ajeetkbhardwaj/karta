# Contributing to Karta

Thanks for your interest in improving Karta. This guide covers building,
testing, and submitting changes.

## Prerequisites

- CMake 3.16+
- A C++17 compiler (GCC, Clang, or MSVC)
- OpenSSL development libraries
- ncurses (Linux/macOS) or PDCurses (Windows) only if building the TUI
- `pkg-config` on Linux/macOS

Setup commands are in [`README.md`](README.md).

## Build

Core build (CLI + agent):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

TUI build:

```bash
cmake -S . -B build-tui -DKARTA_BUILD_TUI=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-tui -j4
```

## Tests

Build and run the unit tests before opening a pull request:

```bash
cmake -S . -B build -DKARTA_BUILD_TESTS=ON
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

Add or update tests in `test/unit_tests.cpp` for behavior changes.

## Code style

- Format with `clang-format` before committing:

  ```bash
  find . -name '*.cpp' -o -name '*.h' -o -name '*.hpp' | xargs clang-format -i
  ```

- Build with the project warning set (`-Wall -Wextra` or MSVC `/W4`).
- Keep changes cross-platform: guard OS-specific code and prefer the
  compatibility headers in `include/` for POSIX APIs.
- Follow existing file layout:
  - `src/core/` — IDs, JSON, auth, platform, telemetry
  - `src/ai/` — HTTP and provider integrations
  - `src/agent/` — agent loop and prompts
  - `src/session/` — session persistence
  - `src/tools/` — built-in tools
  - `src/cli/` — command-line interface
  - `src/tui/` — terminal UI
  - `include/karta/<category>/` — public headers

## Adding a provider or tool

- Provider definitions live in `src/ai/provider.cpp`; include the default base
  URL, model list, and environment variable name.
- Built-in tools are registered in `src/tools/tools.cpp`; provide a JSON schema
  and implement execution through the tool definition.
- Update `README.md`, `KARTA-GUIDE.md`, and `CHANGELOG.md` when user-facing
  behavior changes.

## Pull requests

1. Fork the repository and create a topic branch.
2. Keep commits focused and write clear messages describing the change.
3. Ensure the build and tests pass locally.
4. Open a pull request against `main` with:
   - what changed and why
   - how it was tested
   - any platform-specific notes
5. Update `CHANGELOG.md` under `[Unreleased]` for notable changes.

## Reporting issues

Include the Karta version (`karta --version`), operating system, install method,
and steps to reproduce. For provider or model issues, mention the provider and
model name (never paste API keys or session contents).

## Security

Do not open public issues for security problems. Report them privately to the
maintainers instead.
