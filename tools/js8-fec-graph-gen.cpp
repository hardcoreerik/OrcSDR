#include "js8-graph-builder.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool parse_unsigned_after(const std::string& line, const char* key,
                          size_t* value) {
  const std::string token(key);
  const size_t pos = line.find(token);
  if (pos == std::string::npos) return false;
  const char* start = line.c_str() + pos + token.size();
  char* end = nullptr;
  const unsigned long v = std::strtoul(start, &end, 10);
  if (end == start) return false;
  *value = static_cast<size_t>(v);
  return true;
}

bool parse_check(const std::string& line, std::vector<uint16_t>* out) {
  if (out == nullptr || line.empty() || line[0] != 'H') return false;
  const size_t eq = line.find('=');
  if (eq == std::string::npos || eq + 1 >= line.size()) return false;

  std::vector<uint16_t> check;
  std::stringstream stream(line.substr(eq + 1));
  std::string item;
  while (std::getline(stream, item, ',')) {
    if (item.empty()) return false;
    char* end = nullptr;
    const unsigned long value = std::strtoul(item.c_str(), &end, 10);
    if (end == item.c_str() || *end != '\0' || value > 65535u)
      return false;
    check.push_back(static_cast<uint16_t>(value));
  }
  if (check.empty()) return false;
  *out = std::move(check);
  return true;
}

template <typename T>
void print_array(const char* name, const std::vector<T>& values) {
  std::printf("inline constexpr uint16_t %s[]{", name);
  for (size_t i = 0; i < values.size(); ++i)
    std::printf("%s%u", i ? "," : "",
                static_cast<unsigned>(values[i]));
  std::printf("};\n");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::fprintf(
        stderr,
        "usage: %s <sparse-search-output.txt> [--allow-incomplete]\n",
        argv[0]);
    return 2;
  }

  const bool allow_incomplete =
      argc == 3 && std::string(argv[2]) == "--allow-incomplete";
  if (argc == 3 && !allow_incomplete) return 2;

  std::ifstream input(argv[1]);
  if (!input) {
    std::perror(argv[1]);
    return 2;
  }

  std::vector<std::vector<uint16_t>> checks;
  size_t parity_dimension = 0;
  size_t check_rank = 0;
  bool have_summary = false;
  std::string line;

  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();

    if (line.rfind("JS8_SPARSE_PARITY ", 0) == 0) {
      have_summary =
          parse_unsigned_after(
              line, "parity_dimension=", &parity_dimension) &&
          parse_unsigned_after(line, "check_rank=", &check_rank);
      continue;
    }

    if (!line.empty() && line[0] == 'H') {
      std::vector<uint16_t> check;
      if (!parse_check(line, &check)) {
        std::fprintf(stderr, "invalid check line: %s\n", line.c_str());
        return 2;
      }
      checks.push_back(std::move(check));
    }
  }

  if (!have_summary || checks.empty()) {
    std::fprintf(stderr, "missing sparse-search summary or checks\n");
    return 2;
  }

  if (!allow_incomplete && check_rank != parity_dimension) {
    std::fprintf(
        stderr,
        "refusing incomplete graph: recovered check rank %zu of %zu; "
        "rerun reconstruction or use --allow-incomplete for host experiments\n",
        check_rank, parity_dimension);
    return 1;
  }

  orcsdr::js8::reconstruct::GraphData graph{};
  if (!orcsdr::js8::reconstruct::build_graph(checks, 174, &graph)) {
    std::fprintf(stderr, "cannot build graph\n");
    return 1;
  }

  std::printf("#pragma once\n\n");
  std::printf("#include \"js8_fec.hpp\"\n\n");
  std::printf("namespace orcsdr::js8::fec::generated {\n\n");
  print_array("kCheckOffsets", graph.check_offsets);
  print_array("kCheckVariables", graph.check_variables);
  print_array("kVariableOffsets", graph.variable_offsets);
  print_array("kVariableEdges", graph.variable_edges);

  std::printf(
      "\ninline constexpr Graph kGraph{%u,%zu,%zu,"
      "kCheckOffsets,kCheckVariables,kVariableOffsets,kVariableEdges};\n\n",
      static_cast<unsigned>(graph.variable_count), checks.size(),
      graph.check_variables.size());
  std::printf("}  // namespace orcsdr::js8::fec::generated\n");
  return 0;
}
