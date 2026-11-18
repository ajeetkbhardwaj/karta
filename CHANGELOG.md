# Changelog

All notable changes to Karta are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] - 2026-11-18

First public release of Karta, a terminal-first coding agent.

### Added

- Interactive ncurses terminal UI with streaming transcript, editor, markdown
  rendering, scrolling, and completion popups.
- Command-line mode for one-shot prompts, scripts, and automation.
- Provider-aware model selection for Anthropic, OpenAI, OpenRouter, Groq,
  DeepSeek, xAI, Mistral, NVIDIA, Ollama, and LM Studio.
- Provider login flow with API keys stored in the auth file (mode `0600`).
- `/login`, `/logout`, `/provider`, `/model`, `/thinking`, `/session`, `/clear`,
  `/help`, and `/exit` commands in the TUI.
- Session persistence with create, load, resume (`-c`/`--continue`), fork, and
  custom session directories.
- Session model/provider change tracking and message history.
- Built-in agent tools: `bash`, `read`, `write`, `edit`, `grep`, `glob`, `find`,
  and `ls`.
- Tool allowlist/denylist flags and built-in tool disabling.
- JSON output mode for structured CLI results.
- Thinking-level control and configurable max turns and auto-compaction
  threshold.
- Environment-based configuration via `KARTA_BASE_URL`, `KARTA_MODEL`,
  `KARTA_MAX_TOKENS`, `KARTA_THINKING`, and provider API key variables.
- Unit test suite covering IDs, JSON, sessions, tool schemas, providers, and
  tool execution.
- CI matrix building Linux, macOS, and Windows with TUI on/off, plus
  clang-format lint and format checks.
- One-line `install.sh` installer for Linux/macOS with OS/arch detection and
  SHA256 verification.
- GitHub release workflow on `v*` tags publishing Linux x86_64, macOS
  x86_64/arm64, and Windows x64 binaries with `SHA256SUMS`.
- Homebrew tap formula template in `packaging/homebrew/karta.rb`.
- MIT license and contributing guide.

### Changed

- Cross-platform build improvements: Windows `ws2_32` linking and
  `karta_enable_warnings` helper (MSVC `/W4`, GCC/Clang `-Wall -Wextra`).
- CI now builds with `--config Release` and runs tests through `ctest`.
- Added POSIX compatibility headers (`arpa/inet.h`, `netdb.h`, `unistd.h`) for
  Windows builds.
- `cmake --install` now installs documentation and CPack metadata; project
  version is declared in CMake.
- Hidden the experimental `--mcp` flag from `--help` until the MCP client is
  re-enabled.

[Unreleased]: https://github.com/ajeetkbhardwaj/karta/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/ajeetkbhardwaj/karta/releases/tag/v0.1.0
