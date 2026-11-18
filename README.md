# Karta

Karta is a terminal-first coding agent that runs in either a rich ncurses TUI or a command-line interface. It supports provider-based model selection, session persistence, built-in tools, and a fast prompt workflow for local or cloud LLMs.

## Features

- Interactive terminal UI with `/login`, `/model`, `/provider`, and command shortcuts
- CLI mode for one-shot prompts and scripting
- Provider-aware model selection for OpenAI, Anthropic, NVIDIA, OpenRouter, Groq, DeepSeek, xAI, Mistral, Ollama, and LM Studio
- Session persistence and resume support
- Built-in agent tools including bash, read, write, edit, grep, find, ls, and more
- Cross-platform build support for Linux, macOS, and Windows

## Install

Prebuilt binaries are published on the [releases page](https://github.com/ajeetkbhardwaj/karta/releases).

Linux/macOS (verifies the release checksum):

```bash
curl -fsSL https://raw.githubusercontent.com/ajeetkbhardwaj/karta/main/install.sh | sh
```

Homebrew:

```bash
brew tap ajeetkbhardwaj/tap
brew install karta
```

Windows: download the release zip, extract `karta.exe`, and add it to `PATH`.

Unix release binaries include the TUI; Linux requires `libncursesw` at runtime.

## Clone the repository

```bash
git clone https://github.com/ajeetkbhardwaj/karta.git
cd karta
```

## Requirements

- CMake 3.16+
- C++17 compiler
- OpenSSL development libraries
- ncurses development package for the TUI
- pkg-config on Linux/macOS

### macOS

```bash
xcode-select --install
brew install cmake ncurses openssl pkg-config
```

### Ubuntu/Debian

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config libncursesw5-dev libssl-dev
```

### Windows

Use a modern MSVC or MinGW environment and install `pdcurses` if building the TUI.

## Build

Standard build (CLI + core agent):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

TUI-enabled build:

```bash
cmake -S . -B build-tui -DKARTA_BUILD_TUI=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-tui -j4
```

## Run the TUI

```bash
./build-tui/karta
```

Or from the default build if TUI was enabled there:

```bash
./build/karta
```

## TUI basics

Once the app starts:

- `/login <provider>`: connect a provider and save an API key
- `/model`: search or pick a model
- `/provider <name>`: switch provider
- `/thinking <level>`: set thinking level
- `/session`: show current session path
- `/clear`: clear the transcript
- `/help`: show commands
- `/exit`: quit

Useful keys:

- Enter: submit prompt
- Esc: cancel current popup or flow
- Ctrl+C: cancel active turn or quit from empty prompt
- PgUp / PgDn: scroll transcript history

## CLI usage

Run a one-shot prompt:

```bash
./build/karta --provider openai --model gpt-4o-mini -p "Write a short hello world in C++"
```

Use a specific provider with API key via environment variable or `--api-key`:

```bash
export OPENAI_API_KEY="your-key"
./build/karta --provider openai --model gpt-4o-mini -p "Summarize this project"
```

JSON output mode:

```bash
./build/karta --mode json -p "List 3 ways to optimize this codebase"
```

## Environment variables

Common variables:

```bash
export OPENAI_API_KEY="..."
export ANTHROPIC_API_KEY="..."
export KARTA_BASE_URL="http://localhost:11434/v1"
export KARTA_MODEL="openai/gpt-4o-mini"
export KARTA_THINKING="1"
```

The app also supports provider-specific environment names such as `OPENROUTER_API_KEY`, `GROQ_API_KEY`, `DEEPSEEK_API_KEY`, `XAI_API_KEY`, `MISTRAL_API_KEY`, and `NVIDIA_API_KEY`.

## Session persistence

Sessions are stored in the user session directory and can be resumed with `-c` or `--continue`.

```bash
./build/karta -c
```

## More details

See `KARTA-GUIDE.md` for a complete walkthrough covering:

- repository installation
- building on macOS/Linux/Windows
- using the TUI in detail
- CLI examples and common flags
- troubleshooting and session management

## License

See the repository license file for usage terms.
