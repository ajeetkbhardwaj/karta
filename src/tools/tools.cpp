#include "karta/tools/tools.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <system_error>

#include "karta/ai/http.h"
#include "karta/core/platform.h"
#include <arpa/inet.h>
#include <netdb.h>

namespace fs = std::filesystem;

namespace karta {
namespace {

std::string lower_copy(std::string value) {
  for (char& c : value)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return value;
}

std::string trim_right(std::string value) {
  while (!value.empty() && (value.back() == '\r' || value.back() == ' ' || value.back() == '\t')) {
    value.pop_back();
  }
  return value;
}

std::string resolve_path(const std::string& input, const std::string& cwd) {
  if (input.empty()) return cwd;
  fs::path path(input);
  if (input == "~") return home_directory();
  if (input.rfind("~/", 0) == 0 || input.rfind("~\\", 0) == 0) {
    path = fs::path(home_directory()) / input.substr(2);
  } else if (path.is_relative()) {
    path = fs::path(cwd) / path;
  }
  std::error_code error;
  const fs::path normalized = path.lexically_normal();
  return normalized.string();
}

std::string display_path(const fs::path& path, const std::string& cwd) {
  std::error_code error;
  const fs::path relative = fs::relative(path, cwd, error);
  if (!error && !relative.empty() && relative.native().rfind("..", 0) != 0) {
    return relative.generic_string();
  }
  return path.generic_string();
}

bool read_file(const std::string& path, std::string& out, std::string& error) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    error = "cannot read file: " + path;
    return false;
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  out = buffer.str();
  return true;
}

bool write_file(const std::string& path, const std::string& content, std::string& error) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    error = "cannot write file: " + path;
    return false;
  }
  stream.write(content.data(), static_cast<std::streamsize>(content.size()));
  if (!stream) {
    error = "write failed: " + path;
    return false;
  }
  return true;
}

bool looks_binary(const std::string& data) {
  const std::size_t limit = std::min<std::size_t>(data.size(), 8192);
  for (std::size_t i = 0; i < limit; ++i) {
    if (data[i] == '\0') return true;
  }
  return false;
}

std::vector<std::string> split_lines(const std::string& text) {
  std::vector<std::string> lines;
  std::string current;
  for (const char c : text) {
    if (c == '\n') {
      if (!current.empty() && current.back() == '\r') current.pop_back();
      lines.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty()) {
    if (current.back() == '\r') current.pop_back();
    lines.push_back(current);
  }
  return lines;
}

// --------------------------------------------------------------- glob matching

std::string regex_escape(const std::string& text) {
  std::string out;
  for (const char c : text) {
    if (std::string(".^$+{}()|\\[]*?").find(c) != std::string::npos) out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

std::string glob_body(const std::string& glob) {
  std::string out;
  for (std::size_t i = 0; i < glob.size(); ++i) {
    const char c = glob[i];
    if (c == '*') {
      if (i + 1 < glob.size() && glob[i + 1] == '*') {
        ++i;
        if (i + 1 < glob.size() && glob[i + 1] == '/') {
          ++i;
          out += "(?:.*/)?";
        } else {
          out += ".*";
        }
      } else {
        out += "[^/]*";
      }
    } else if (c == '?') {
      out += "[^/]";
    } else if (c == '[') {
      const std::size_t end = glob.find(']', i + 1);
      if (end != std::string::npos) {
        out += glob.substr(i, end - i + 1);
        i = end;
      } else {
        out += "\\[";
      }
    } else if (std::string(".^$+{}()|\\").find(c) != std::string::npos) {
      out.push_back('\\');
      out.push_back(c);
    } else {
      out.push_back(c);
    }
  }
  return out;
}

bool glob_match(const std::string& glob, const std::string& text, bool ignore_case) {
  try {
    const auto flags =
        std::regex::ECMAScript | (ignore_case ? std::regex::icase : std::regex::ECMAScript);
    const std::regex pattern("^" + glob_body(glob) + "$", flags);
    return std::regex_match(text, pattern);
  } catch (const std::regex_error&) {
    return false;
  }
}

std::string glob_to_regex_string(const std::string& glob, bool anchored, bool dir_only) {
  std::string regex = anchored ? "^" : "(?:^|/)";
  regex += glob_body(glob);
  if (dir_only) regex += "(?:/.*)?";
  regex += "$";
  return regex;
}

// -------------------------------------------------------------- ignore handling

struct IgnoreRules {
  std::vector<std::regex> patterns;
  bool has_negation = false;

  bool ignored(const std::string& relative, bool is_dir) const {
    if (relative.empty()) return false;
    bool result = false;
    for (const auto& pattern : patterns) {
      if (std::regex_search(relative, pattern)) result = true;
    }
    return result;
  }
};

IgnoreRules load_ignore_rules(const fs::path& root) {
  IgnoreRules rules;
  std::string content;
  std::string error;
  if (!read_file((root / ".gitignore").string(), content, error)) return rules;
  std::istringstream stream(content);
  std::string line;
  const bool ignore_case = os_description() == "windows";
  while (std::getline(stream, line)) {
    line = trim_right(line);
    if (line.empty() || line[0] == '#') continue;
    bool negate = false;
    if (line[0] == '!') {
      negate = true;
      line.erase(line.begin());
    }
    if (line.empty()) continue;
    if (negate) {
      rules.has_negation = true;
      continue; // Karta prunes conservatively; negations only disable pruning.
    }
    bool dir_only = false;
    if (line.back() == '/') {
      dir_only = true;
      line.pop_back();
    }
    if (!line.empty() && line[0] == '/') line.erase(line.begin());
    if (line.empty()) continue;
    const bool anchored = line.find('/') != std::string::npos;
    try {
      rules.patterns.emplace_back(glob_to_regex_string(line, anchored, dir_only),
                                  ignore_case ? (std::regex::ECMAScript | std::regex::icase)
                                              : std::regex::ECMAScript);
    } catch (const std::regex_error&) {
      // A malformed ignore line must not break a search.
    }
  }
  return rules;
}

const std::set<std::string>& always_skip_directories() {
  static const std::set<std::string> names = {".git", "node_modules", ".venv", "__pycache__",
                                              ".hg",  ".svn",         ".cache"};
  return names;
}

using FileVisitor = std::function<void(const fs::directory_entry&)>;

void walk_files(const fs::path& root, const IgnoreRules& rules, const FileVisitor& visitor) {
  std::error_code error;
  fs::recursive_directory_iterator iterator(root, fs::directory_options::skip_permission_denied,
                                            error);
  const fs::recursive_directory_iterator end;
  while (!error && iterator != end) {
    const fs::directory_entry entry = *iterator;
    const std::string relative = fs::relative(entry.path(), root, error).generic_string();
    error.clear();
    const bool is_dir = entry.is_directory(error);
    if (error) {
      error.clear();
      iterator.increment(error);
      continue;
    }
    const std::string name = entry.path().filename().string();
    const bool skip = is_dir && (always_skip_directories().count(name) > 0 ||
                                 (rules.ignored(relative, true) && !rules.has_negation));
    if (skip) {
      iterator.disable_recursion_pending();
    } else if (is_dir) {
      // Descend.
    } else if (rules.ignored(relative, false)) {
      // Ignored file.
    } else {
      visitor(entry);
    }
    iterator.increment(error);
  }
}

// ------------------------------------------------------------------- truncation

struct Truncated {
  std::string text;
  bool truncated = false;
  std::size_t kept_lines = 0;
  std::size_t total_lines = 0;
};

Truncated truncate_head(const std::string& text, std::size_t max_lines, std::size_t max_bytes) {
  Truncated result;
  std::size_t bytes = 0;
  std::size_t lines = 0;
  std::size_t offset = 0;
  while (offset < text.size()) {
    const std::size_t newline = text.find('\n', offset);
    const std::size_t end = newline == std::string::npos ? text.size() : newline + 1;
    const std::size_t length = end - offset;
    result.total_lines++;
    if (lines + 1 > max_lines || bytes + length > max_bytes) {
      result.truncated = true;
      // Keep counting remaining lines for an accurate notice.
      std::size_t rest = end;
      while (true) {
        const std::size_t next = text.find('\n', rest);
        if (next == std::string::npos) break;
        result.total_lines++;
        rest = next + 1;
      }
      break;
    }
    bytes += length;
    lines++;
    result.text.append(text, offset, length);
    offset = end;
  }
  result.kept_lines = lines;
  if (!result.truncated) {
    result.text = text;
    result.total_lines = lines;
  }
  return result;
}

std::string size_label(std::size_t bytes) {
  std::ostringstream out;
  if (bytes >= 1024 * 1024) {
    out << (bytes / (1024.0 * 1024.0)) << "MB";
  } else if (bytes >= 1024) {
    out << (bytes / 1024.0) << "KB";
  } else {
    out << bytes << "B";
  }
  return out.str();
}

// ---------------------------------------------------------------------- schemas

Json schema_object(const Json& properties, const std::vector<std::string>& required) {
  Json schema = Json::object();
  schema.set("type", "object");
  schema.set("properties", properties);
  Json required_array = Json::array();
  for (const std::string& name : required)
    required_array.push_back(name);
  schema.set("required", required_array);
  schema.set("additionalProperties", false);
  return schema;
}

Json string_property(const std::string& description) {
  Json property = Json::object();
  property.set("type", "string");
  property.set("description", description);
  return property;
}

Json number_property(const std::string& description) {
  Json property = Json::object();
  property.set("type", "number");
  property.set("description", description);
  return property;
}

Json boolean_property(const std::string& description) {
  Json property = Json::object();
  property.set("type", "boolean");
  property.set("description", description);
  return property;
}

// ------------------------------------------------------------------------ tools

ToolOutcome bash_tool(const Json& args, ToolContext& context) {
  ToolOutcome outcome;
  const std::string command = args["command"].as_string();
  if (command.empty()) {
    outcome.text = "command is required";
    outcome.is_error = true;
    return outcome;
  }
  int timeout = static_cast<int>(args["timeout"].as_int(0));
  if (timeout < 0) timeout = 0;
  const int kMaxTimeout = 2147483;
  if (timeout > kMaxTimeout) {
    outcome.text = "Invalid timeout: maximum is " + std::to_string(kMaxTimeout) + " seconds";
    outcome.is_error = true;
    return outcome;
  }

  const ProcessResult result = run_shell(command, context.cwd, timeout, kMaxBytes);
  outcome.text = result.output;
  if (result.timed_out) {
    outcome.is_error = true;
    outcome.text += "\n\n[timeout: " + std::to_string(timeout) + "s]";
  }
  if (result.truncated && !result.full_output_path.empty()) {
    outcome.text += "\n\n[" + size_label(kMaxBytes) +
                    " limit reached; full output: " + result.full_output_path + "]";
  }
  if (result.output.empty() && !result.timed_out) outcome.text = "(no output)";
  if (result.exit_code != 0 && !result.timed_out) {
    outcome.text += "\n\n[exit code: " + std::to_string(result.exit_code) + "]";
    outcome.is_error = true;
  }
  return outcome;
}

ToolOutcome read_tool(const Json& args, ToolContext& context) {
  ToolOutcome outcome;
  const std::string requested = args["path"].as_string();
  if (requested.empty()) {
    outcome.text = "path is required";
    outcome.is_error = true;
    return outcome;
  }
  const std::string path = resolve_path(requested, context.cwd);
  std::string data;
  std::string error;
  if (!read_file(path, data, error)) {
    outcome.text = error;
    outcome.is_error = true;
    return outcome;
  }

  const std::string extension = lower_copy(fs::path(path).extension().string());
  if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".gif" ||
      extension == ".webp" || extension == ".bmp") {
    outcome.text = "[Image attachments are not supported by this port; inspect with bash instead.]";
    outcome.is_error = true;
    return outcome;
  }
  if (looks_binary(data)) {
    outcome.text = "[Binary file; use bash to inspect: " + display_path(path, context.cwd) + "]";
    outcome.is_error = true;
    return outcome;
  }
  if (data.rfind("\xEF\xBB\xBF", 0) == 0) data.erase(0, 3);

  const std::vector<std::string> lines = split_lines(data);
  const std::size_t total = lines.size();
  long long offset = args["offset"].as_int(1);
  if (offset < 1) offset = 1;
  const long long requested_limit = args["limit"].as_int(0);
  const std::size_t start = static_cast<std::size_t>(offset - 1);
  if (start >= total && !(start == 0 && total == 0)) {
    outcome.text = "[offset " + std::to_string(offset) + " is past end of file (" +
                   std::to_string(total) + " lines)]";
    outcome.is_error = true;
    return outcome;
  }
  std::size_t count = total - start;
  if (requested_limit > 0)
    count = std::min<std::size_t>(count, static_cast<std::size_t>(requested_limit));

  std::string selected;
  for (std::size_t i = 0; i < count; ++i) {
    if (i > 0) selected.push_back('\n');
    selected += lines[start + i];
  }
  const Truncated truncated = truncate_head(selected, kMaxLines, kMaxBytes);
  outcome.text = truncated.text;
  if (truncated.truncated) {
    const std::size_t shown = start + truncated.kept_lines + 1;
    outcome.text += "\n\n[" + std::to_string(truncated.kept_lines) + " lines shown; " +
                    size_label(kMaxBytes) + " limit. Use offset=" + std::to_string(shown) +
                    " to continue.]";
  } else if (count < total) {
    outcome.text += "\n\n[Showing lines " + std::to_string(offset) + "-" +
                    std::to_string(offset + static_cast<long long>(count) - 1) + " of " +
                    std::to_string(total) +
                    ". Use offset=" + std::to_string(offset + static_cast<long long>(count)) +
                    " to continue.]";
  }
  if (total == 0) outcome.text = "(empty file)";
  return outcome;
}

ToolOutcome write_tool(const Json& args, ToolContext& context) {
  ToolOutcome outcome;
  const std::string requested = args["path"].as_string();
  if (requested.empty()) {
    outcome.text = "path is required";
    outcome.is_error = true;
    return outcome;
  }
  const std::string path = resolve_path(requested, context.cwd);
  std::error_code error;
  const fs::path parent = fs::path(path).parent_path();
  if (!parent.empty()) fs::create_directories(parent, error);
  const std::string content = args["content"].as_string();
  std::string write_error;
  if (!write_file(path, content, write_error)) {
    outcome.text = write_error;
    outcome.is_error = true;
    return outcome;
  }
  outcome.text =
      "Wrote " + std::to_string(content.size()) + " bytes to " + display_path(path, context.cwd);
  return outcome;
}

ToolOutcome edit_tool(const Json& args, ToolContext& context) {
  ToolOutcome outcome;
  const std::string requested = args["path"].as_string();
  const Json& edits = args["edits"];
  if (requested.empty()) {
    outcome.text = "path is required";
    outcome.is_error = true;
    return outcome;
  }
  if (!edits.is_array() || edits.items().empty()) {
    outcome.text = "Edit tool input is invalid. edits must contain at least one replacement.";
    outcome.is_error = true;
    return outcome;
  }

  const std::string path = resolve_path(requested, context.cwd);
  std::string original;
  std::string error;
  if (!read_file(path, original, error)) {
    outcome.text = error;
    outcome.is_error = true;
    return outcome;
  }

  struct Replacement {
    std::size_t position;
    std::size_t length;
    std::string text;
  };
  std::vector<Replacement> replacements;

  for (std::size_t index = 0; index < edits.items().size(); ++index) {
    const Json& edit = edits.items()[index];
    const std::string old_text = edit["oldText"].as_string();
    const std::string new_text = edit["newText"].as_string();
    if (old_text.empty()) {
      outcome.text = "edits[" + std::to_string(index) + "].oldText must not be empty";
      outcome.is_error = true;
      return outcome;
    }
    const std::size_t position = original.find(old_text);
    if (position == std::string::npos) {
      outcome.text = "edits[" + std::to_string(index) + "].oldText was not found in " +
                     display_path(path, context.cwd);
      outcome.is_error = true;
      return outcome;
    }
    if (original.find(old_text, position + 1) != std::string::npos) {
      outcome.text = "edits[" + std::to_string(index) +
                     "].oldText matches more than once; include more context to make it unique";
      outcome.is_error = true;
      return outcome;
    }
    replacements.push_back({position, old_text.size(), new_text});
  }

  std::sort(replacements.begin(), replacements.end(),
            [](const Replacement& a, const Replacement& b) { return a.position < b.position; });
  for (std::size_t i = 1; i < replacements.size(); ++i) {
    const Replacement& previous = replacements[i - 1];
    if (previous.position + previous.length > replacements[i].position) {
      outcome.text = "edits overlap; merge them into one edit";
      outcome.is_error = true;
      return outcome;
    }
  }

  std::string updated = original;
  for (auto it = replacements.rbegin(); it != replacements.rend(); ++it) {
    updated.replace(it->position, it->length, it->text);
  }
  std::string write_error;
  if (!write_file(path, updated, write_error)) {
    outcome.text = write_error;
    outcome.is_error = true;
    return outcome;
  }
  outcome.text = "Applied " + std::to_string(replacements.size()) + " edit" +
                 (replacements.size() == 1 ? "" : "s") + " to " + display_path(path, context.cwd);
  return outcome;
}

ToolOutcome grep_tool(const Json& args, ToolContext& context) {
  ToolOutcome outcome;
  const std::string requested_pattern = args["pattern"].as_string();
  if (requested_pattern.empty()) {
    outcome.text = "pattern is required";
    outcome.is_error = true;
    return outcome;
  }
  const std::string search_path = resolve_path(args["path"].as_string(), context.cwd);
  const std::string glob = args["glob"].as_string();
  const bool ignore_case = args["ignoreCase"].as_bool(false);
  const bool literal = args["literal"].as_bool(false);
  long long context_lines = args["context"].as_int(0);
  if (context_lines < 0) context_lines = 0;
  long long limit = args["limit"].as_int(100);
  if (limit < 1) limit = 1;

  std::regex pattern;
  try {
    auto flags =
        std::regex::ECMAScript | (ignore_case ? std::regex::icase : std::regex::ECMAScript);
    pattern = std::regex(literal ? regex_escape(requested_pattern) : requested_pattern, flags);
  } catch (const std::regex_error&) {
    outcome.text = "invalid regex: " + requested_pattern;
    outcome.is_error = true;
    return outcome;
  }

  const IgnoreRules rules = load_ignore_rules(search_path);
  std::vector<std::pair<std::string, std::string>> files; // display name, absolute path
  std::error_code error;
  if (fs::is_regular_file(search_path, error)) {
    files.emplace_back(display_path(search_path, context.cwd), search_path);
  } else if (fs::is_directory(search_path, error)) {
    walk_files(search_path, rules, [&](const fs::directory_entry& entry) {
      const std::string relative = fs::relative(entry.path(), search_path).generic_string();
      if (!glob.empty() && !glob_match(glob, relative, ignore_case) &&
          !glob_match(glob, entry.path().filename().string(), ignore_case)) {
        return;
      }
      files.emplace_back(display_path(entry.path(), context.cwd), entry.path().string());
    });
  } else {
    outcome.text = "path not found: " + args["path"].as_string();
    outcome.is_error = true;
    return outcome;
  }
  std::sort(files.begin(), files.end());

  std::ostringstream output;
  long long matches = 0;
  bool limit_hit = false;
  long long printed_line = -1;
  std::string last_file;

  for (const auto& file : files) {
    std::string content;
    std::string read_error;
    if (!read_file(file.second, content, read_error) || looks_binary(content)) continue;
    const std::vector<std::string> lines = split_lines(content);
    for (std::size_t index = 0; index < lines.size(); ++index) {
      if (!std::regex_search(lines[index], pattern)) continue;
      if (matches >= limit) {
        limit_hit = true;
        break;
      }
      matches++;
      const long long first = std::max<long long>(0, static_cast<long long>(index) - context_lines);
      const long long last = std::min<long long>(static_cast<long long>(lines.size()) - 1,
                                                 static_cast<long long>(index) + context_lines);
      for (long long line = first; line <= last; ++line) {
        const bool is_match = line == static_cast<long long>(index);
        if (file.first != last_file) {
          last_file = file.first;
          printed_line = -1;
        }
        if (line <= printed_line && is_match) continue;
        if (line <= printed_line) continue;
        std::string text = lines[static_cast<std::size_t>(line)];
        if (text.size() > 500) text = text.substr(0, 500) + "…";
        output << file.first << (is_match ? ":" : "-") << (line + 1) << (is_match ? ":" : "-")
               << text << "\n";
        printed_line = line;
      }
    }
    if (limit_hit) break;
  }

  if (matches == 0) {
    outcome.text = "No matches found";
    return outcome;
  }
  outcome.text = output.str();
  while (!outcome.text.empty() && outcome.text.back() == '\n')
    outcome.text.pop_back();
  const Truncated truncated = truncate_head(outcome.text, static_cast<std::size_t>(-1), kMaxBytes);
  outcome.text = truncated.text;
  if (limit_hit) {
    outcome.text += "\n\n[" + std::to_string(limit) +
                    " matches limit reached. Use limit=" + std::to_string(limit * 2) +
                    " for more, or refine pattern]";
  }
  if (truncated.truncated) {
    outcome.text += "\n\n[" + size_label(kMaxBytes) + " limit reached]";
  }
  return outcome;
}

ToolOutcome glob_tool(const Json& args, ToolContext& context) {
  ToolOutcome outcome;
  const std::string pattern = args["pattern"].as_string();
  if (pattern.empty()) {
    outcome.text = "pattern is required";
    outcome.is_error = true;
    return outcome;
  }
  const std::string search_path = resolve_path(args["path"].as_string(), context.cwd);
  long long limit = args["limit"].as_int(1000);
  if (limit < 1) limit = 1;
  long long timeout_ms = args["timeout"].as_int(30000); // Default 30s timeout
  if (timeout_ms < 0) timeout_ms = 0;

  std::error_code error;
  if (!fs::is_directory(search_path, error)) {
    outcome.text = "not a directory: " + args["path"].as_string();
    outcome.is_error = true;
    return outcome;
  }
  const IgnoreRules rules = load_ignore_rules(search_path);
  const bool ignore_case = os_description() == "windows";

  std::vector<std::string> results;
  bool timed_out = false;

  auto search_task = [&]() {
    walk_files(search_path, rules, [&](const fs::directory_entry& entry) {
      const std::string relative = fs::relative(entry.path(), search_path).generic_string();
      if (glob_match(pattern, relative, ignore_case)) results.push_back(relative);
    });
  };

  if (timeout_ms > 0) {
    std::future<void> fut = std::async(std::launch::async, search_task);
    if (fut.wait_for(std::chrono::milliseconds(timeout_ms)) == std::future_status::timeout) {
      timed_out = true;
    }
  } else {
    search_task();
  }

  if (timed_out) {
    outcome.text = "Search timed out after " + std::to_string(timeout_ms) + "ms";
    outcome.is_error = true;
    return outcome;
  }

  std::sort(results.begin(), results.end());

  const bool limit_hit = static_cast<long long>(results.size()) > limit;
  if (limit_hit) results.resize(static_cast<std::size_t>(limit));

  if (results.empty()) {
    outcome.text = "No files found";
    return outcome;
  }
  std::string joined;
  for (const std::string& result : results)
    joined += result + "\n";
  while (!joined.empty() && joined.back() == '\n')
    joined.pop_back();
  const Truncated truncated = truncate_head(joined, static_cast<std::size_t>(-1), kMaxBytes);
  outcome.text = truncated.text;
  if (limit_hit) {
    outcome.text += "\n\n[" + std::to_string(limit) +
                    " results limit reached. Use limit=" + std::to_string(limit * 2) +
                    " for more, or refine pattern]";
  }
  if (truncated.truncated) outcome.text += "\n\n[" + size_label(kMaxBytes) + " limit reached]";
  return outcome;
}

ToolOutcome find_tool_impl(const Json& args, ToolContext& context) {
  ToolOutcome outcome;
  const std::string pattern = args["pattern"].as_string();
  if (pattern.empty()) {
    outcome.text = "pattern is required";
    outcome.is_error = true;
    return outcome;
  }
  const std::string search_path = resolve_path(args["path"].as_string(), context.cwd);
  long long limit = args["limit"].as_int(1000);
  if (limit < 1) limit = 1;
  long long timeout_ms = args["timeout"].as_int(30000); // Default 30s timeout
  if (timeout_ms < 0) timeout_ms = 0;

  std::error_code error;
  if (!fs::is_directory(search_path, error)) {
    outcome.text = "not a directory: " + args["path"].as_string();
    outcome.is_error = true;
    return outcome;
  }
  const IgnoreRules rules = load_ignore_rules(search_path);
  const bool ignore_case = os_description() == "windows";
  const bool pattern_has_slash = pattern.find('/') != std::string::npos;

  std::vector<std::string> results;
  bool timed_out = false;

  auto search_task = [&]() {
    walk_files(search_path, rules, [&](const fs::directory_entry& entry) {
      const std::string relative = fs::relative(entry.path(), search_path).generic_string();
      const std::string candidate = pattern_has_slash ? relative : entry.path().filename().string();
      if (glob_match(pattern, candidate, ignore_case)) results.push_back(relative);
    });
  };

  if (timeout_ms > 0) {
    std::future<void> fut = std::async(std::launch::async, search_task);
    if (fut.wait_for(std::chrono::milliseconds(timeout_ms)) == std::future_status::timeout) {
      timed_out = true;
    }
  } else {
    search_task();
  }

  if (timed_out) {
    outcome.text = "Search timed out after " + std::to_string(timeout_ms) + "ms";
    outcome.is_error = true;
    return outcome;
  }

  std::sort(results.begin(), results.end());

  const bool limit_hit = static_cast<long long>(results.size()) > limit;
  if (limit_hit) results.resize(static_cast<std::size_t>(limit));

  if (results.empty()) {
    outcome.text = "No files found";
    return outcome;
  }
  std::string joined;
  for (const std::string& result : results)
    joined += result + "\n";
  while (!joined.empty() && joined.back() == '\n')
    joined.pop_back();
  const Truncated truncated = truncate_head(joined, static_cast<std::size_t>(-1), kMaxBytes);
  outcome.text = truncated.text;
  if (limit_hit) {
    outcome.text += "\n\n[" + std::to_string(limit) +
                    " results limit reached. Use limit=" + std::to_string(limit * 2) +
                    " for more, or refine pattern]";
  }
  if (truncated.truncated) outcome.text += "\n\n[" + size_label(kMaxBytes) + " limit reached]";
  return outcome;
}

ToolOutcome ls_tool(const Json& args, ToolContext& context) {
  ToolOutcome outcome;
  const std::string path = resolve_path(args["path"].as_string(), context.cwd);
  long long limit = args["limit"].as_int(500);
  if (limit < 1) limit = 1;

  std::error_code error;
  if (!fs::is_directory(path, error)) {
    outcome.text = "not a directory: " + args["path"].as_string();
    outcome.is_error = true;
    return outcome;
  }
  std::vector<std::pair<std::string, bool>> entries;
  fs::directory_iterator iterator(path, fs::directory_options::skip_permission_denied, error);
  const fs::directory_iterator end;
  while (!error && iterator != end) {
    const bool is_dir = iterator->is_directory(error);
    error.clear();
    std::string name = iterator->path().filename().string();
    if (is_dir) name.push_back('/');
    entries.emplace_back(name, is_dir);
    iterator.increment(error);
  }
  std::sort(entries.begin(), entries.end());

  const bool limit_hit = static_cast<long long>(entries.size()) > limit;
  if (limit_hit) entries.resize(static_cast<std::size_t>(limit));
  if (entries.empty()) {
    outcome.text = "(empty directory)";
    return outcome;
  }
  std::string joined;
  for (const auto& entry : entries)
    joined += entry.first + "\n";
  while (!joined.empty() && joined.back() == '\n')
    joined.pop_back();
  const Truncated truncated = truncate_head(joined, static_cast<std::size_t>(-1), kMaxBytes);
  outcome.text = truncated.text;
  if (limit_hit) {
    outcome.text += "\n\n[" + std::to_string(limit) +
                    " entries limit reached. Use limit=" + std::to_string(limit * 2) + " for more]";
  }
  if (truncated.truncated) outcome.text += "\n\n[" + size_label(kMaxBytes) + " limit reached]";
  return outcome;
}

#ifdef _WIN32
ToolOutcome powershell_tool(const Json& args, ToolContext& context) {
  Json wrapped = args;
  wrapped.set("command", "powershell.exe -NoProfile -NonInteractive -Command \"" +
                             args["command"].as_string() + "\"");
  return bash_tool(wrapped, context);
}
#endif

// ---------------------------------------------------------------------- patch tool

struct PatchHunk {
  int old_start;
  int old_lines;
  int new_start;
  int new_lines;
  std::vector<std::string> lines;
};

std::vector<PatchHunk> parse_unified_diff(const std::string& diff) {
  std::vector<PatchHunk> hunks;
  std::istringstream stream(diff);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.rfind("@@", 0) == 0) {
      PatchHunk hunk;
      std::regex hunk_regex(R"(@@ -(\d+),?(\d*) \+(\d+),?(\d*) @@)");
      std::smatch match;
      if (std::regex_search(line, match, hunk_regex)) {
        hunk.old_start = std::stoi(match[1]);
        hunk.old_lines = match[2].matched ? std::stoi(match[2]) : 1;
        hunk.new_start = std::stoi(match[3]);
        hunk.new_lines = match[4].matched ? std::stoi(match[4]) : 1;
        hunks.push_back(std::move(hunk));
      }
    } else if (!hunks.empty()) {
      hunks.back().lines.push_back(line);
    }
  }
  return hunks;
}

ToolOutcome patch_tool(const Json& args, ToolContext& context) {
  ToolOutcome outcome;
  const std::string requested = args["path"].as_string();
  if (requested.empty()) {
    outcome.text = "path is required";
    outcome.is_error = true;
    return outcome;
  }
  const std::string diff = args["diff"].as_string();
  if (diff.empty()) {
    outcome.text = "diff is required";
    outcome.is_error = true;
    return outcome;
  }
  const std::string path = resolve_path(requested, context.cwd);
  std::string original;
  std::string error;
  if (!read_file(path, original, error)) {
    outcome.text = error;
    outcome.is_error = true;
    return outcome;
  }
  if (looks_binary(original)) {
    outcome.text = "Cannot patch binary file";
    outcome.is_error = true;
    return outcome;
  }

  std::vector<PatchHunk> hunks = parse_unified_diff(diff);
  if (hunks.empty()) {
    outcome.text = "No valid hunks found in diff";
    outcome.is_error = true;
    return outcome;
  }

  // Check for overlapping hunks in original file coordinates
  for (std::size_t i = 0; i < hunks.size(); ++i) {
    int start_i = hunks[i].old_start - 1;
    int end_i = start_i + hunks[i].old_lines - 1;
    for (std::size_t j = i + 1; j < hunks.size(); ++j) {
      int start_j = hunks[j].old_start - 1;
      int end_j = start_j + hunks[j].old_lines - 1;
      if (start_i <= end_j && start_j <= end_i) {
        outcome.text = "Hunks overlap in original file: hunk " + std::to_string(i + 1) +
                       " (lines " + std::to_string(start_i + 1) + "-" + std::to_string(end_i + 1) +
                       ") overlaps with hunk " + std::to_string(j + 1) + " (lines " +
                       std::to_string(start_j + 1) + "-" + std::to_string(end_j + 1) + ")";
        outcome.is_error = true;
        return outcome;
      }
    }
  }

  std::vector<std::string> lines = split_lines(original);
  int offset = 0;
  bool any_applied = false;

  for (const auto& hunk : hunks) {
    int start = hunk.old_start - 1 + offset;
    if (start < 0 || start > static_cast<int>(lines.size())) {
      outcome.text = "Hunk out of bounds at line " + std::to_string(hunk.old_start);
      outcome.is_error = true;
      return outcome;
    }
    int context_lines = 0;
    for (const std::string& hunk_line : hunk.lines) {
      if (hunk_line.empty()) continue;
      char type = hunk_line[0];
      if (type == ' ') {
        if (start + context_lines >= static_cast<int>(lines.size()) ||
            lines[start + context_lines] != hunk_line.substr(1)) {
          outcome.text = "Context mismatch at line " + std::to_string(start + context_lines + 1);
          outcome.is_error = true;
          return outcome;
        }
        context_lines++;
      } else if (type == '-') {
        if (start + context_lines >= static_cast<int>(lines.size()) ||
            lines[start + context_lines] != hunk_line.substr(1)) {
          outcome.text = "Removal mismatch at line " + std::to_string(start + context_lines + 1);
          outcome.is_error = true;
          return outcome;
        }
        lines.erase(lines.begin() + start + context_lines);
        offset--;
      } else if (type == '+') {
        lines.insert(lines.begin() + start + context_lines, hunk_line.substr(1));
        context_lines++;
        offset++;
        any_applied = true;
      }
    }
  }

  std::string updated;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) updated.push_back('\n');
    updated += lines[i];
  }
  if (!original.empty() && original.back() == '\n' && (updated.empty() || updated.back() != '\n')) {
    updated.push_back('\n');
  }

  std::string write_error;
  if (!write_file(path, updated, write_error)) {
    outcome.text = write_error;
    outcome.is_error = true;
    return outcome;
  }
  outcome.text = std::string(any_applied ? "Applied " : "Patched ") + std::to_string(hunks.size()) +
                 " hunk" + (hunks.size() == 1 ? "" : "s") + " to " +
                 display_path(path, context.cwd);
  return outcome;
}

// ---------------------------------------------------------------------- todo tool

static std::map<std::string, std::vector<std::pair<std::string, bool>>>
    g_todo_lists; // cwd -> {content, completed}

std::vector<std::pair<std::string, bool>>& get_todo_list(const std::string& cwd) {
  return g_todo_lists[cwd];
}

ToolOutcome todo_tool(const Json& args, ToolContext& context) {
  ToolOutcome outcome;
  auto& todo_list = get_todo_list(context.cwd);
  const std::string action = args["action"].as_string();
  if (action == "add") {
    const std::string content = args["content"].as_string();
    if (content.empty()) {
      outcome.text = "content is required for add";
      outcome.is_error = true;
      return outcome;
    }
    todo_list.emplace_back(content, false);
    outcome.text = "Added todo: " + content;
    outcome.structured_content = Json::object();
    outcome.structured_content.set("action", "add");
    outcome.structured_content.set("index", static_cast<int>(todo_list.size()) - 1);
    outcome.structured_content.set("content", content);
    outcome.structured_content.set("completed", false);
  } else if (action == "complete") {
    long long index = args["index"].as_int(-1);
    if (index < 0 || index >= static_cast<long long>(todo_list.size())) {
      outcome.text = "invalid index: " + std::to_string(index);
      outcome.is_error = true;
      return outcome;
    }
    todo_list[index].second = true;
    outcome.text = "Completed todo: " + todo_list[index].first;
    outcome.structured_content = Json::object();
    outcome.structured_content.set("action", "complete");
    outcome.structured_content.set("index", index);
    outcome.structured_content.set("content", todo_list[index].first);
    outcome.structured_content.set("completed", true);
  } else if (action == "update") {
    long long index = args["index"].as_int(-1);
    if (index < 0 || index >= static_cast<long long>(todo_list.size())) {
      outcome.text = "invalid index: " + std::to_string(index);
      outcome.is_error = true;
      return outcome;
    }
    const std::string content = args["content"].as_string();
    if (!content.empty()) todo_list[index].first = content;
    outcome.text = "Updated todo at index " + std::to_string(index);
    outcome.structured_content = Json::object();
    outcome.structured_content.set("action", "update");
    outcome.structured_content.set("index", index);
    outcome.structured_content.set("content", todo_list[index].first);
    outcome.structured_content.set("completed", todo_list[index].second);
  } else if (action == "list" || action.empty()) {
    Json result = Json::array();
    for (std::size_t i = 0; i < todo_list.size(); ++i) {
      Json item = Json::object();
      item.set("index", static_cast<int>(i));
      item.set("content", todo_list[i].first);
      item.set("completed", todo_list[i].second);
      result.push_back(item);
    }
    std::ostringstream out;
    for (std::size_t i = 0; i < todo_list.size(); ++i) {
      out << (todo_list[i].second ? "[x] " : "[ ] ") << todo_list[i].first;
      if (i + 1 < todo_list.size()) out << "\n";
    }
    outcome.text = out.str().empty() ? "(no todos)" : out.str();
    outcome.structured_content = result;
  } else if (action == "clear") {
    todo_list.clear();
    outcome.text = "Cleared all todos";
    outcome.structured_content = Json::object();
    outcome.structured_content.set("action", "clear");
  } else {
    outcome.text = "unknown action: " + action + " (use add, complete, update, list, clear)";
    outcome.is_error = true;
  }
  return outcome;
}

// --------------------------------------------------------------------- SSRF protection

bool is_private_ip(const std::string& ip) {
  struct in_addr addr4;
  struct in6_addr addr6;
  if (inet_pton(AF_INET, ip.c_str(), &addr4) == 1) {
    uint32_t ip_num = ntohl(addr4.s_addr);
    // Loopback: 127.0.0.0/8
    if ((ip_num & 0xFF000000) == 0x7F000000) return true;
    // RFC1918: 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16
    if ((ip_num & 0xFF000000) == 0x0A000000) return true;
    if ((ip_num & 0xFFF00000) == 0xAC100000) return true;
    if ((ip_num & 0xFFFF0000) == 0xC0A80000) return true;
    // Link-local: 169.254.0.0/16
    if ((ip_num & 0xFFFF0000) == 0xA9FE0000) return true;
    // Cloud metadata: 169.254.169.254
    if (ip_num == 0xA9FEADFE) return true;
    return false;
  }
  if (inet_pton(AF_INET6, ip.c_str(), &addr6) == 1) {
    // Loopback: ::1
    if (IN6_IS_ADDR_LOOPBACK(&addr6)) return true;
    // Link-local: fe80::/10
    if ((addr6.s6_addr[0] == 0xFE) && ((addr6.s6_addr[1] & 0xC0) == 0x80)) return true;
    // Unique local: fc00::/7
    if ((addr6.s6_addr[0] & 0xFE) == 0xFC) return true;
    return false;
  }
  return false;
}

std::vector<std::string> resolve_hostname(const std::string& hostname) {
  std::vector<std::string> ips;
  struct addrinfo hints = {}, *result = nullptr;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  int error = getaddrinfo(hostname.c_str(), nullptr, &hints, &result);
  if (error != 0) return ips;
  for (struct addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
    char ip[INET6_ADDRSTRLEN];
    if (rp->ai_family == AF_INET) {
      struct sockaddr_in* addr = reinterpret_cast<struct sockaddr_in*>(rp->ai_addr);
      if (inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip))) ips.push_back(ip);
    } else if (rp->ai_family == AF_INET6) {
      struct sockaddr_in6* addr = reinterpret_cast<struct sockaddr_in6*>(rp->ai_addr);
      if (inet_ntop(AF_INET6, &addr->sin6_addr, ip, sizeof(ip))) ips.push_back(ip);
    }
  }
  freeaddrinfo(result);
  return ips;
}

bool is_url_allowed(const std::string& url, bool allow_private_ips) {
  if (allow_private_ips) return true;

  // Parse URL to extract hostname
  std::string hostname;
  size_t scheme_end = url.find("://");
  if (scheme_end == std::string::npos) return false;
  size_t host_start = scheme_end + 3;
  size_t host_end = url.find('/', host_start);
  if (host_end == std::string::npos) host_end = url.size();
  size_t port_start = url.find(':', host_start);
  if (port_start != std::string::npos && port_start < host_end) {
    hostname = url.substr(host_start, port_start - host_start);
  } else {
    hostname = url.substr(host_start, host_end - host_start);
  }

  // Resolve hostname and check all IPs
  std::vector<std::string> ips = resolve_hostname(hostname);
  for (const std::string& ip : ips) {
    if (is_private_ip(ip)) return false;
  }
  return true;
}

// --------------------------------------------------------------------- webfetch tool

std::string strip_html_tags(const std::string& html) {
  std::string result;
  bool in_tag = false;
  for (char c : html) {
    if (c == '<') {
      in_tag = true;
    } else if (c == '>') {
      in_tag = false;
    } else if (!in_tag) {
      result.push_back(c);
    }
  }
  return result;
}

std::string html_to_markdown(const std::string& html) {
  std::string text = strip_html_tags(html);
  std::string result;
  std::istringstream stream(text);
  std::string line;
  bool in_code = false;
  while (std::getline(stream, line)) {
    if (line.size() > 2000) line = line.substr(0, 2000) + "…";
    if (in_code && line.rfind("```", 0) == 0) {
      in_code = false;
    }
    if (!in_code && line.rfind("```", 0) == 0) {
      in_code = true;
    }
    result += line + "\n";
  }
  return result;
}

ToolOutcome webfetch_tool(const Json& args, ToolContext& context) {
  ToolOutcome outcome;
  const std::string url = args["url"].as_string();
  if (url.empty()) {
    outcome.text = "url is required";
    outcome.is_error = true;
    return outcome;
  }
  if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
    outcome.text = "url must start with http:// or https://";
    outcome.is_error = true;
    return outcome;
  }

  // SSRF protection - check if URL is allowed
  bool allow_private_ips = args["allowPrivateIps"].as_bool(false);
  if (!is_url_allowed(url, allow_private_ips)) {
    outcome.text = "URL blocked by SSRF protection: resolves to private/internal IP address. "
                   "Set allowPrivateIps=true to override (not recommended).";
    outcome.is_error = true;
    return outcome;
  }

  const std::string format = args["format"].as_string();
  std::vector<std::string> headers = {
      "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8"};
  const std::atomic<bool> cancel{false};

  // SSRF protection on redirects: validate each redirect target
  RedirectCallback redirect_validator = [&](const std::string& new_url) -> bool {
    return is_url_allowed(new_url, allow_private_ips);
  };

  HttpResponse response = http_get(url, headers, &cancel, redirect_validator);

  if (!response.ok()) {
    std::string error_class;
    if (response.status >= 400 && response.status < 500) {
      error_class = " (client error - URL may be invalid or blocked)";
      if (response.status == 404)
        error_class = " (not found - URL may be wrong)";
      else if (response.status == 403)
        error_class = " (forbidden - site may block scrapers)";
      else if (response.status == 429)
        error_class = " (rate limited - try later)";
    } else if (response.status >= 500 && response.status < 600) {
      error_class = " (server error - may be temporary)";
    }
    outcome.text = "HTTP " + std::to_string(response.status) + error_class + ": " + response.error;
    outcome.is_error = true;
    return outcome;
  }

  // Response size limit is enforced during transfer via CURLOPT_MAXFILESIZE_LARGE equivalent (1MB)

  if (format == "text" || format == "markdown") {
    outcome.text = html_to_markdown(response.body);
  } else if (format == "html") {
    outcome.text = response.body;
  } else {
    outcome.text = html_to_markdown(response.body);
  }

  outcome.structured_content = Json::object();
  outcome.structured_content.set("url", url);
  outcome.structured_content.set("status", response.status);
  outcome.structured_content.set("contentType", "text/html");
  outcome.structured_content.set("size", static_cast<int>(response.body.size()));
  return outcome;
}

} // namespace

static std::string url_encode(const std::string& value) {
  std::ostringstream escaped;
  escaped.fill('0');
  escaped << std::hex;
  for (char c : value) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' ||
        c == '~') {
      escaped << c;
    } else {
      escaped << '%' << std::uppercase << std::setw(2)
              << static_cast<int>(static_cast<unsigned char>(c));
    }
  }
  return escaped.str();
}

// DuckDuckGo instant answer API search (no API key required, like ddgs Python lib)
static Json perform_duckduckgo_search(const std::string& query, int max_results,
                                      const std::atomic<bool>* cancel) {
  std::string url = "https://api.duckduckgo.com/?q=" + url_encode(query) +
                    "&format=json&no_html=1&skip_disambig=1";
  std::vector<std::string> headers = {"Accept: application/json"};
  HttpResponse response = http_get(url, headers, cancel);
  Json results = Json::array();
  if (response.ok()) {
    Json data = Json::parse(response.body);
    std::string abstract = data["AbstractText"].as_string();
    std::string abstract_url = data["AbstractURL"].as_string();
    std::string heading = data["Heading"].as_string();
    if (!abstract.empty()) {
      Json item = Json::object();
      item.set("title", heading.empty() ? "Instant Answer" : heading);
      item.set("snippet", abstract);
      item.set("url", abstract_url.empty() ? "https://duckduckgo.com/?q=" + url_encode(query)
                                           : abstract_url);
      results.push_back(item);
    }
    std::string definition = data["Definition"].as_string();
    std::string definition_url = data["DefinitionURL"].as_string();
    if (!definition.empty() && results.items().size() < static_cast<size_t>(max_results)) {
      Json item = Json::object();
      item.set("title", "Definition");
      item.set("snippet", definition);
      item.set("url", definition_url.empty() ? "https://duckduckgo.com/?q=" + url_encode(query)
                                             : definition_url);
      results.push_back(item);
    }
    std::string answer = data["Answer"].as_string();
    std::string answer_type = data["AnswerType"].as_string();
    if (!answer.empty() && results.items().size() < static_cast<size_t>(max_results)) {
      Json item = Json::object();
      item.set("title", answer_type.empty() ? "Answer" : answer_type);
      item.set("snippet", answer);
      item.set("url", "https://duckduckgo.com/?q=" + url_encode(query));
      results.push_back(item);
    }
    for (const Json& topic : data["RelatedTopics"].items()) {
      if (results.items().size() >= static_cast<size_t>(max_results)) break;
      if (topic["Text"].is_string()) {
        Json item = Json::object();
        item.set("title", "Related Topic");
        item.set("snippet", topic["Text"].as_string());
        item.set("url", topic["FirstURL"].as_string());
        results.push_back(item);
      } else if (topic["Topics"].is_array()) {
        for (const Json& sub : topic["Topics"].items()) {
          if (results.items().size() >= static_cast<size_t>(max_results)) break;
          if (sub["Text"].is_string()) {
            Json item = Json::object();
            item.set("title", "Related Topic");
            item.set("snippet", sub["Text"].as_string());
            item.set("url", sub["FirstURL"].as_string());
            results.push_back(item);
          }
        }
      }
    }
    for (const Json& result : data["Results"].items()) {
      if (results.items().size() >= static_cast<size_t>(max_results)) break;
      if (result["Text"].is_string() && result["FirstURL"].is_string()) {
        Json item = Json::object();
        item.set("title", result["Title"].as_string());
        item.set("snippet", result["Text"].as_string());
        item.set("url", result["FirstURL"].as_string());
        results.push_back(item);
      }
    }
  }
  return results;
}

ToolOutcome websearch(const Json& args, ToolContext& context) {
  ToolOutcome outcome;
  const std::string query = args["query"].as_string();
  if (query.empty()) {
    outcome.text = "query is required";
    outcome.is_error = true;
    return outcome;
  }
  int max_results = args["maxResults"].as_int(10);
  if (max_results < 1) max_results = 1;
  if (max_results > 50) max_results = 50;
  const std::atomic<bool> cancel{false};
  Json results = perform_duckduckgo_search(query, max_results, &cancel);
  std::ostringstream out;
  out << "Search results for: " << query << "\n\n";
  int count = 0;
  for (const Json& item : results.items()) {
    count++;
    out << count << ". " << item["title"].as_string() << "\n";
    out << "   " << item["snippet"].as_string() << "\n";
    out << "   URL: " << item["url"].as_string() << "\n\n";
  }
  if (count == 0) out << "No results found.\n";
  outcome.text = out.str();
  outcome.structured_content = Json::object();
  outcome.structured_content.set("query", query);
  outcome.structured_content.set("provider", "duckduckgo");
  outcome.structured_content.set("resultCount", count);
  outcome.structured_content.set("results", results);
  return outcome;
}

std::vector<ToolDefinition> builtin_tools() {
  std::vector<ToolDefinition> tools;

  {
    Json properties = Json::object();
    properties.set("command", string_property("Shell command to execute"));
    properties.set("timeout", number_property("Timeout in seconds (optional, no default timeout)"));
    ToolDefinition tool;
    tool.name = "bash";
    tool.description =
        "Execute a shell command in the working directory. Returns combined stdout and stderr, "
        "truncated to 2000 lines or 50KB (whichever is hit first). Use for builds, tests, git, "
        "and any command not covered by the dedicated tools.";
    tool.parameters = schema_object(properties, {"command"});
    tool.execute = bash_tool;
    tools.push_back(std::move(tool));
  }

  {
    Json properties = Json::object();
    properties.set("path", string_property("Path to the file to read (relative or absolute)"));
    properties.set("offset", number_property("Line number to start reading from (1-indexed)"));
    properties.set("limit", number_property("Maximum number of lines to read"));
    ToolDefinition tool;
    tool.name = "read";
    tool.description =
        "Read the contents of a text file. Output is truncated to 2000 lines or 50KB (whichever "
        "is hit first). Use offset/limit for large files and continue with offset until complete.";
    tool.parameters = schema_object(properties, {"path"});
    tool.execute = read_tool;
    tools.push_back(std::move(tool));
  }

  {
    Json properties = Json::object();
    properties.set("path", string_property("Path to the file to write (relative or absolute)"));
    properties.set("content", string_property("Content to write to the file"));
    ToolDefinition tool;
    tool.name = "write";
    tool.description =
        "Write content to a file, creating parent directories as needed. Overwrites the whole "
        "file; prefer edit for targeted changes.";
    tool.parameters = schema_object(properties, {"path", "content"});
    tool.execute = write_tool;
    tools.push_back(std::move(tool));
  }

  {
    Json edit_properties = Json::object();
    edit_properties.set("oldText", string_property("Exact text to replace; must be unique."));
    edit_properties.set("newText", string_property("Replacement text for this targeted edit."));
    Json edit_schema = Json::object();
    edit_schema.set("type", "object");
    edit_schema.set("properties", edit_properties);
    Json edit_required = Json::array();
    edit_required.push_back("oldText");
    edit_required.push_back("newText");
    edit_schema.set("required", edit_required);

    Json properties = Json::object();
    properties.set("path", string_property("Path to the file to edit (relative or absolute)"));
    Json edits = Json::object();
    edits.set("type", "array");
    edits.set("description", "One or more targeted replacements.");
    edits.set("items", edit_schema);
    properties.set("edits", edits);

    ToolDefinition tool;
    tool.name = "edit";
    tool.description =
        "Edit a single file using exact text replacement. Every edits[].oldText must match a "
        "unique, non-overlapping region of the original file. Merge nearby changes into one edit.";
    tool.parameters = schema_object(properties, {"path", "edits"});
    tool.execute = edit_tool;
    tools.push_back(std::move(tool));
  }

  {
    Json properties = Json::object();
    properties.set("pattern", string_property("Search pattern (regex or literal string)"));
    properties.set("path",
                   string_property("Directory or file to search (default: current directory)"));
    properties.set("glob", string_property("Filter files by glob pattern, e.g. '*.ts'"));
    properties.set("ignoreCase", boolean_property("Case-insensitive search (default: false)"));
    properties.set("literal",
                   boolean_property("Treat pattern as a literal string (default: false)"));
    properties.set("context",
                   number_property("Lines to show before and after each match (default: 0)"));
    properties.set("limit", number_property("Maximum number of matches to return (default: 100)"));

    ToolDefinition tool;
    tool.name = "grep";
    tool.description =
        "Search file contents for a pattern. Returns matching lines with file paths and line "
        "numbers. Skips .git, node_modules and .gitignore-matched paths. Output is truncated to "
        "100 matches or 50KB.";
    tool.parameters = schema_object(properties, {"pattern"});
    tool.execute = grep_tool;
    tools.push_back(std::move(tool));
  }

  {
    Json properties = Json::object();
    properties.set("pattern",
                   string_property("Glob pattern to match files, e.g. '*.ts' or 'src/**/*.json'"));
    properties.set("path", string_property("Directory to search in (default: current directory)"));
    properties.set("limit", number_property("Maximum number of results (default: 1000)"));
    properties.set("timeout",
                   number_property("Timeout in milliseconds (default: 30000, 0 = no timeout)"));
    ToolDefinition tool;
    tool.name = "glob";
    tool.description =
        "Find files matching a glob pattern. Returns matching file paths relative to the search "
        "directory. Skips .git, node_modules and .gitignore-matched paths. Unlike 'find', this "
        "matches the full relative path against the pattern. Has 30s default timeout.";
    tool.parameters = schema_object(properties, {"pattern"});
    tool.execute = glob_tool;
    tools.push_back(std::move(tool));
  }

  {
    Json properties = Json::object();
    properties.set("pattern",
                   string_property("Glob pattern to match files, e.g. '*.ts' or 'src/**/*.json'"));
    properties.set("path", string_property("Directory to search in (default: current directory)"));
    properties.set("limit", number_property("Maximum number of results (default: 1000)"));
    properties.set("timeout",
                   number_property("Timeout in milliseconds (default: 30000, 0 = no timeout)"));
    ToolDefinition tool;
    tool.name = "find";
    tool.description =
        "Search for files by glob pattern. Returns matching file paths relative to the search "
        "directory. Skips .git, node_modules and .gitignore-matched paths. Has 30s default "
        "timeout.";
    tool.parameters = schema_object(properties, {"pattern"});
    tool.execute = find_tool_impl;
    tools.push_back(std::move(tool));
  }

  {
    Json properties = Json::object();
    properties.set("path", string_property("Directory to list (default: current directory)"));
    properties.set("limit", number_property("Maximum number of entries to return (default: 500)"));
    ToolDefinition tool;
    tool.name = "ls";
    tool.description =
        "List directory contents, sorted alphabetically, with '/' for directories. Includes "
        "dotfiles. Output is truncated to 500 entries.";
    tool.parameters = schema_object(properties, {"path"});
    tool.execute = ls_tool;
    tools.push_back(std::move(tool));
  }

#ifdef _WIN32
  {
    Json properties = Json::object();
    properties.set("command", string_property("PowerShell command to execute"));
    properties.set("timeout", number_property("Timeout in seconds (optional)"));
    ToolDefinition tool;
    tool.name = "powershell";
    tool.description = "Execute a PowerShell command on Windows. Returns combined output.";
    tool.parameters = schema_object(properties, {"command"});
    tool.execute = powershell_tool;
    tools.push_back(std::move(tool));
  }
#endif

  {
    Json properties = Json::object();
    properties.set("path", string_property("Path to the file to patch (relative or absolute)"));
    properties.set("diff", string_property("Unified diff to apply"));
    ToolDefinition tool;
    tool.name = "patch";
    tool.description =
        "Apply a unified diff patch to a file. The diff must be in standard unified format "
        "with @@ headers. Supports multiple hunks. Use for targeted changes when edit is not "
        "suitable.";
    tool.parameters = schema_object(properties, {"path", "diff"});
    tool.execute = patch_tool;
    tools.push_back(std::move(tool));
  }

  {
    Json properties = Json::object();
    properties.set("action", string_property("Action: add, complete, update, list, clear"));
    properties.set("content",
                   string_property("Todo content (required for add, optional for update)"));
    properties.set("index", number_property("Todo index (required for complete, update)"));
    ToolDefinition tool;
    tool.name = "todo";
    tool.description =
        "Manage an internal todo list for the current session. Use add, complete, update, list, "
        "or clear. Todos persist for the session and help track multi-step tasks.";
    tool.parameters = schema_object(properties, {"action"});
    tool.execute = todo_tool;
    tools.push_back(std::move(tool));
  }

  {
    Json properties = Json::object();
    properties.set("url", string_property("URL to fetch (http:// or https://)"));
    properties.set("format",
                   string_property("Output format: text, markdown, or html (default: markdown)"));
    properties.set(
        "allowPrivateIps",
        boolean_property("Allow fetching private/internal IPs (default: false, SSRF-protected)"));
    ToolDefinition tool;
    tool.name = "webfetch";
    tool.description = "Fetch a URL and return its content. Supports HTML-to-markdown conversion. "
                       "Use for reading web pages, API responses, or documentation. SSRF-protected "
                       "(blocks loopback, "
                       "RFC1918 private, link-local, and cloud metadata IPs).";
    tool.parameters = schema_object(properties, {"url"});
    tool.execute = webfetch_tool;
    tools.push_back(std::move(tool));
  }

  {
    Json properties = Json::object();
    properties.set("query", string_property("Search query"));
    properties.set("maxResults",
                   number_property("Maximum number of results (default: 10, max: 50)"));
    ToolDefinition tool;
    tool.name = "websearch";
    tool.description =
        "Search the web using DuckDuckGo instant answer API (no API key required). "
        "Returns structured results from Wikipedia, definitions, answers, and related topics. "
        "Like the ddgs Python library - zero-config, fair-use ~30 req/min.";
    tool.parameters = schema_object(properties, {"query"});
    tool.execute = websearch;
    tools.push_back(std::move(tool));
  }

  return tools;
}

const ToolDefinition* find_tool(const std::vector<ToolDefinition>& tools, const std::string& name) {
  for (const ToolDefinition& tool : tools) {
    if (tool.name == name) return &tool;
  }
  return nullptr;
}

Json tool_declaration(const ToolDefinition& tool) {
  Json declaration = Json::object();
  declaration.set("name", tool.name);
  declaration.set("description", tool.description);
  declaration.set("parameters", tool.parameters);
  return declaration;
}

} // namespace karta
