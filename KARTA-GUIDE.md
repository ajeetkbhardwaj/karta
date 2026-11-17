# Karta Guide

This guide covers how to clone, install, build, and use Karta in both terminal UI and CLI modes.

## 1. Clone the repository

```bash
git clone <repo-url>
cd karta
```

If you already have the repository checked out, make sure you are in the project root:

```bash
pwd
ls
```

The root should contain files like `CMakeLists.txt`, `README.md`, `include/`, `src/`, and `test/`.

## 2. Install dependencies

### macOS

```bash
xcode-select --install
brew install cmake ncurses openssl pkg-config
```

### Ubuntu / Debian

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config libncursesw5-dev libssl-dev
```

### Windows

Install a C++ toolchain such as Visual Studio/MSVC or MinGW, plus `cmake` and `pdcurses` if building the TUI.

## 3. Build Karta

### Core build (CLI + shared agent code)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

### TUI build

```bash
cmake -S . -B build-tui -DKARTA_BUILD_TUI=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-tui -j4
```

### Run the built binary

```bash
./build/karta --help
```

or the TUI build:

```bash
./build-tui/karta
```

## 4. TUI usage

Run:

```bash
./build-tui/karta
```

This starts the interactive ncurses-based agent window.

### Main commands inside the TUI

- `/login <provider>`: connect a provider and save an API key
- `/logout <provider>`: remove a saved API key
- `/model`: list or search models
- `/provider <name>`: set or show the active provider
- `/thinking <level>`: set reasoning level (`off`, `low`, `medium`, `high` or `0-3`)
- `/clear`: clear transcript
- `/session`: show session path
- `/help`: show commands
- `/exit`: quit

### Useful keys

- Enter: submit prompt
- Esc: cancel current prompt or popup
- Ctrl+C: cancel the active turn or quit from an empty prompt
- PgUp / PgDn: scroll transcript history
- Tab: select completion item when a completion popup is active
- Up / Down: navigate history or completion list

### Typical TUI flow

1. Start Karta:

   ```bash
   ./build-tui/karta
   ```
2. Type `/login openai` or `/login anthropic`
3. Enter your API key when prompted
4. Type `/model` and select a model
5. Start prompting in the main input field
6. Use `/help` if you need command reference

### Provider setup

Cloud providers can use either saved credentials via `/login` or environment variables:

```bash
export OPENAI_API_KEY="..."
export ANTHROPIC_API_KEY="..."
export OPENROUTER_API_KEY="..."
export GROQ_API_KEY="..."
export NVIDIA_API_KEY="..."
```

Local providers such as Ollama and LM Studio usually do not need an API key:

```bash
ollama serve
```

Then in the TUI:

```text
/login ollama
```

and press Enter without entering a key.

## 5. CLI usage

Karta can also be used without the TUI for scripts and one-shot prompting.

### One-shot prompt

```bash
./build/karta -p "Write a short C++ hello world example"
```

### Use a specific provider and model

```bash
./build/karta \
  --provider openai \
  --model gpt-4o-mini \
  -p "Summarize this repository"
```

### Pass an API key directly

```bash
./build/karta --provider openai --api-key "$OPENAI_API_KEY" -p "Explain the project structure"
```

### JSON output mode

```bash
./build/karta --mode json -p "List the top 5 improvements to this codebase"
```

### Show options and help

```bash
./build/karta --help
./build/karta --version
```

### List default provider models

```bash
./build/karta --list-models
```

## 6. Session handling

Karta persists session data and supports resume/continue operations.

Resume the most recent session:

```bash
./build/karta -c
```

Create or open a session by ID:

```bash
./build/karta --session-id my-session
```

Use a custom session directory:

```bash
./build/karta --session-dir ~/.karta/sessions
```

## 7. Common environment variables

```bash
export OPENAI_API_KEY="..."
export ANTHROPIC_API_KEY="..."
export KARTA_BASE_URL="http://localhost:11434/v1"
export KARTA_MODEL="openai/gpt-4o-mini"
export KARTA_THINKING="1"
```

You can also set `KARTA_BASE_URL` to point to a custom OpenAI-compatible endpoint.

## 8. Troubleshooting

### TUI does not open

- Check that ncurses is installed
- Make sure you are running in a real terminal/TTY
- Rebuild with `-DKARTA_BUILD_TUI=ON`

### Provider login fails

- Verify the provider name is correct
- Ensure the API key is present in environment or saved in auth storage
- Use `/login <provider>` and enter the key again

### Model selection fails

- Make sure the provider is connected
- Confirm the base URL is reachable
- Check that the model endpoint responds correctly

### CLI prompt is not producing output

- Use `--provider` and `--model`
- Ensure required environment variables are set
- Try `--mode json` for easier debugging

## 9. Quick start examples

### TUI mode

```bash
cmake -S . -B build-tui -DKARTA_BUILD_TUI=ON
cmake --build build-tui -j4
./build-tui/karta
```

### CLI mode

```bash
./build/karta --provider openai --model gpt-4o-mini -p "Hello there"
```

## 10. Summary

Karta is designed to be easy to run in a terminal:

- use the TUI for interactive work
- use CLI mode for scripts, automation, and one-shot prompts
- use sessions to continue work without losing context

This repository is ready for local development, experimentation, and daily agent workflows in the terminal.
