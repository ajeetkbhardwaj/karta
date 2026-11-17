// Karta — Clipboard integration implementation.

#include "karta/tui/clipboard.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace karta::tui {

static bool has_display() {
  return std::getenv("DISPLAY") != nullptr || std::getenv("WAYLAND_DISPLAY") != nullptr;
}

static std::string exec_cmd(const std::string& cmd) {
  std::string result;
  FILE* f = popen(cmd.c_str(), "r");
  if (!f) return result;
  std::unique_ptr<FILE, int (*)(FILE*)> pipe(f, [](FILE* fp) { return pclose(fp); });
  char buffer[128];
  while (fgets(buffer, sizeof(buffer), pipe.get()) != nullptr) {
    result += buffer;
  }
  return result;
}

static bool run_silent(const std::string& cmd) {
  return system((cmd + " 2>/dev/null").c_str()) == 0;
}

static std::string shell_single_quote(const std::string& text) {
  std::string out = "'";
  for (char c : text) {
    if (c == '\'') out += "'\\''";
    else out += c;
  }
  out += "'";
  return out;
}

bool clipboard_copy(const std::string& text) {
  // Try common clipboard tools regardless of DISPLAY (some work in Wayland/other setups).
  const std::string quoted = shell_single_quote(text);
  const std::string payload = "printf %s " + quoted;

  if (run_silent("command -v wl-copy >/dev/null 2>&1")) {
    return run_silent(payload + " | wl-copy 2>/dev/null");
  }
  if (run_silent("command -v pbcopy >/dev/null 2>&1")) {
    return run_silent(payload + " | pbcopy 2>/dev/null");
  }
  if (run_silent("command -v xclip >/dev/null 2>&1") && has_display()) {
    return run_silent(payload + " | xclip -selection clipboard 2>/dev/null");
  }
  if (run_silent("command -v xsel >/dev/null 2>&1") && has_display()) {
    return run_silent(payload + " | xsel --clipboard --input 2>/dev/null");
  }
  if (run_silent("command -v clip.exe >/dev/null 2>&1")) {
    return run_silent(payload + " | clip.exe 2>/dev/null");
  }
  return false;
}

std::string clipboard_paste() {
  if (!has_display()) return "";
  
  // Try xclip first (Linux X11)
  if (run_silent("which xclip >/dev/null 2>&1")) {
    return exec_cmd("xclip -selection clipboard -o 2>/dev/null");
  }
  // Try xsel (Linux X11 alternative)
  if (run_silent("which xsel >/dev/null 2>&1")) {
    return exec_cmd("xsel --clipboard --output 2>/dev/null");
  }
  // Try pbpaste (macOS)
  if (run_silent("which pbpaste >/dev/null 2>&1")) {
    return exec_cmd("pbpaste 2>/dev/null");
  }
  // Try clip.exe (Windows) - not easily readable
  // Try wl-paste (Wayland)
  if (run_silent("which wl-paste >/dev/null 2>&1")) {
    return exec_cmd("wl-paste 2>/dev/null");
  }
  return "";
}

}  // namespace karta::tui