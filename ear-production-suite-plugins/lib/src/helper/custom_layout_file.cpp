#include "helper/custom_layout_file.hpp"

#include <algorithm>
#include <boost/optional.hpp>
#include <cctype>
#include <fmt/format.h>
#include <fstream>
#include <set>
#include <sstream>

namespace {

std::string trim(const std::string& s) {
  auto start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  auto end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

std::vector<std::string> tokenize(const std::string& line) {
  std::vector<std::string> tokens;
  std::string current;
  for (char c : line) {
    if (c == ',' || std::isspace(static_cast<unsigned char>(c))) {
      if (!current.empty()) {
        tokens.push_back(current);
        current.clear();
      }
    } else {
      current += c;
    }
  }
  if (!current.empty()) tokens.push_back(current);
  return tokens;
}

double parseDouble(const std::string& token, int lineNumber,
                   const std::string& fieldName) {
  try {
    size_t pos;
    double value = std::stod(token, &pos);
    if (pos != token.size()) {
      throw std::invalid_argument("trailing characters");
    }
    return value;
  } catch (const std::exception&) {
    throw ear::plugin::CustomLayoutFileError(
        fmt::format("Line {}: invalid {} value '{}'", lineNumber, fieldName,
                   token));
  }
}

bool parseBool(const std::string& token, int lineNumber) {
  std::string lower = token;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  if (lower == "1" || lower == "true" || lower == "yes" || lower == "lfe")
    return true;
  if (lower == "0" || lower == "false" || lower == "no" || lower == "")
    return false;
  throw ear::plugin::CustomLayoutFileError(
      fmt::format("Line {}: invalid lfe flag '{}' (expected 0/1, true/false, "
                 "yes/no)",
                 lineNumber, token));
}

}  // namespace

namespace ear {
namespace plugin {

CustomLayout parseCustomLayout(const std::string& fileContents,
                               const std::string& layoutName) {
  std::vector<ear::Channel> channels;
  std::vector<double> outputGains;
  std::set<std::string> seenNames;

  std::istringstream stream(fileContents);
  std::string rawLine;
  int lineNumber = 0;
  while (std::getline(stream, rawLine)) {
    ++lineNumber;
    std::string line = trim(rawLine);
    if (line.empty() || line[0] == '#') continue;

    auto tokens = tokenize(line);
    if (tokens.size() < 3) {
      throw CustomLayoutFileError(fmt::format(
          "Line {}: expected at least 'name azimuth elevation', got {} "
          "field(s)",
          lineNumber, tokens.size()));
    }
    if (tokens.size() > 6) {
      throw CustomLayoutFileError(fmt::format(
          "Line {}: too many fields (expected at most 'name azimuth "
          "elevation radius gain lfe')",
          lineNumber));
    }

    const std::string& name = tokens[0];
    if (!seenNames.insert(name).second) {
      throw CustomLayoutFileError(
          fmt::format("Line {}: duplicate speaker name '{}'", lineNumber,
                     name));
    }

    double azimuth = parseDouble(tokens[1], lineNumber, "azimuth");
    double elevation = parseDouble(tokens[2], lineNumber, "elevation");
    double radius = tokens.size() > 3
                        ? parseDouble(tokens[3], lineNumber, "radius")
                        : 1.0;
    double gain =
        tokens.size() > 4 ? parseDouble(tokens[4], lineNumber, "gain") : 1.0;
    bool isLfe = tokens.size() > 5 ? parseBool(tokens[5], lineNumber) : false;

    if (radius <= 0.0) {
      throw CustomLayoutFileError(
          fmt::format("Line {}: radius must be > 0", lineNumber));
    }

    channels.emplace_back(name, ear::PolarPosition{azimuth, elevation, radius},
                          boost::none, boost::none, boost::none, isLfe);
    outputGains.push_back(gain);
  }

  if (channels.empty()) {
    throw CustomLayoutFileError(
        "No speakers found in layout file (empty file, or every line was a "
        "comment)");
  }

  CustomLayout result;
  result.layout = ear::Layout{layoutName, channels};
  result.outputGains = std::move(outputGains);
  return result;
}

CustomLayout loadCustomLayoutFromFile(const std::string& filePath) {
  std::ifstream file(filePath, std::ios::in | std::ios::binary);
  if (!file) {
    throw CustomLayoutFileError(
        fmt::format("Could not open layout file '{}'", filePath));
  }
  std::ostringstream contents;
  contents << file.rdbuf();

  // Use the file's base name (without extension) as the layout name.
  std::string name = filePath;
  auto slash = name.find_last_of("/\\");
  if (slash != std::string::npos) name = name.substr(slash + 1);
  auto dot = name.find_last_of('.');
  if (dot != std::string::npos) name = name.substr(0, dot);

  return parseCustomLayout(contents.str(), name);
}

}  // namespace plugin
}  // namespace ear
