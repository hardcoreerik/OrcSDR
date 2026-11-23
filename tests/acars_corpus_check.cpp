// Runs the interpreter over a real archive log (JSONL written by the Tab5: /orcsdr/acars/YYYYMMDD.jsonl) and reports how much of it
// is understood. With --strict it also fails on anything that breaks the interpreter's contract: more lines than fit, an unprintable
// byte on screen, a summary that is empty for a decoded block, or a crash (build it with ASan/UBSan).
//   acars_corpus_check <file.jsonl> [--strict] [--examples N] [--min-decoded-percent P]
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "acars_archive.hpp"
#include "acars_interpret.hpp"

using namespace orcsdr::acars_ui;

namespace {

bool printable(const char* s) {
  for (; *s; ++s)
    if (static_cast<unsigned char>(*s) < 32 || static_cast<unsigned char>(*s) > 126) return false;
  return true;
}

struct Tally {
  unsigned total = 0, decoded = 0, recognised = 0, unknown = 0;
  std::vector<std::string> unknown_examples;
};

// A coarse format name for grouping: label, plus the H1 sub-label or the first token of the text.
std::string format_key(const MessageRecord& m) {
  std::string key = m.label;
  if (m.has_text && m.text_length > 13 && m.label[0] == 'H' && m.label[1] == '1' && m.text[10] == '#') {
    key += " #";
    key.append(m.text + 11, 2);
    if (m.text[11] == 'D' && m.text[12] == 'F') {            // flight data unit: group by carrier too
      key += " ";
      key.append(m.text + 4, 2);
    }
  }
  return key;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: acars_corpus_check <file.jsonl> [--strict] [--examples N] [--min-decoded-percent P]\n");
    return 2;
  }
  bool strict = false;
  unsigned examples = 2;
  double min_decoded = 0.0;
  for (int i = 2; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--strict")) strict = true;
    else if (!std::strcmp(argv[i], "--examples") && i + 1 < argc) examples = static_cast<unsigned>(std::atoi(argv[++i]));
    else if (!std::strcmp(argv[i], "--min-decoded-percent") && i + 1 < argc) {
      char* end = nullptr;
      min_decoded = std::strtod(argv[++i], &end);
      if (end == argv[i] || *end != '\0' || min_decoded < 0.0 || min_decoded > 100.0) {
        std::fprintf(stderr, "--min-decoded-percent needs a number from 0 to 100, got '%s'\n", argv[i]);
        return 2;
      }
    }
  }
  FILE* f = std::fopen(argv[1], "rb");
  if (!f) {
    std::fprintf(stderr, "cannot open %s\n", argv[1]);
    return 2;
  }
  std::map<std::string, Tally> by_format;
  Tally all;
  unsigned bad_lines = 0, violations = 0;
  std::string line;
  int c;
  auto handle = [&](const std::string& text) {
    if (text.empty()) return;
    MessageRecord m;
    if (!orcsdr::acars_archive::parse_line(text.data(), text.size(), &m)) {
      ++bad_lines;
      return;
    }
    Interpretation r;
    const bool known = interpret(m, r);
    Tally& t = by_format[format_key(m)];
    for (Tally* x : {&t, &all}) {
      ++x->total;
      if (r.decoded) ++x->decoded;
      else if (known) ++x->recognised;
      else ++x->unknown;
    }
    if (!r.decoded && t.unknown_examples.size() < examples) {
      std::string p = m.preview;
      t.unknown_examples.push_back(p.substr(0, 100));
    }
    if (strict) {
      bool bad = r.line_count > kInterpLines || !printable(r.headline) || !printable(r.summary);
      for (size_t i = 0; i < r.line_count && i < kInterpLines; ++i) bad = bad || !printable(r.lines[i]);
      if (r.decoded && (r.summary[0] == '\0' || r.headline[0] == '\0')) bad = true;
      if (bad) {
        ++violations;
        if (violations <= 5) std::fprintf(stderr, "VIOLATION label=%s preview=%.80s headline=%s\n", m.label, m.preview, r.headline);
      }
    }
  };
  while ((c = std::fgetc(f)) != EOF) {
    if (c == '\n') {
      handle(line);
      line.clear();
    } else {
      line.push_back(static_cast<char>(c));
    }
  }
  handle(line);
  std::fclose(f);

  std::printf("records %u  decoded %u (%.1f%%)  recognised only %u  unknown %u  unreadable lines %u\n", all.total, all.decoded,
              all.total ? 100.0 * all.decoded / all.total : 0.0, all.recognised, all.unknown, bad_lines);
  std::vector<std::pair<std::string, Tally>> rows(by_format.begin(), by_format.end());
  std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second.total > b.second.total; });
  std::printf("%-14s %6s %8s %8s %8s\n", "format", "total", "decoded", "label", "unknown");
  for (const auto& row : rows) {
    std::printf("%-14s %6u %8u %8u %8u\n", row.first.c_str(), row.second.total, row.second.decoded, row.second.recognised, row.second.unknown);
    for (const auto& e : row.second.unknown_examples) std::printf("    e.g. %s\n", e.c_str());
  }
  const double pct = all.total ? 100.0 * all.decoded / all.total : 0.0;
  if (strict && violations) {
    std::fprintf(stderr, "%u contract violations\n", violations);
    return 1;
  }
  if (pct < min_decoded) {
    std::fprintf(stderr, "decoded %.1f%% is below the required %.1f%%\n", pct, min_decoded);
    return 1;
  }
  return 0;
}
