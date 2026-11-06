#include "helper/custom_layout_file.hpp"

#include <algorithm>
#include <boost/optional.hpp>
#include <cctype>
#include <fmt/format.h>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <vector>

namespace {

struct RawChannel {
  std::string name;
  double azimuth;
  double elevation;
  double radius;
  double gain;
  bool isLfe;
};

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

// libear's point_source_panner.cpp classifies which real speakers belong to
// the M (mid/ear-height) layer by checking NOMINAL elevation against
// [-10, 10] degrees (see extraPosVerticalNominal()); only speakers
// recognised as belonging to a layer get its azimuth-gap-filling virtual
// speakers, which are what keep a VirtualNgon's downmix localised to a few
// neighbours instead of spreading across every real speaker adjacent to the
// virtual top/bottom vertex. A custom room's REAL elevations rarely land
// exactly on 0 degrees (ceiling/wall constraints), so the main ring's
// elevation has to be reconstructed here - leaving it unset (nominal == real)
// defeats that layer classification entirely and causes panned objects to
// bleed across the whole speaker ring.
//
// Only the main ring (the layer with the most speakers - always the
// horizontal/ear-height ring in every standard immersive layout) is
// canonicalised, to exactly 0 degrees; every other channel keeps its own
// individual real elevation untouched. This was previously also snapping
// additional layers above/below the main one to fixed values (30/90/-30/
// -90), but domes with more than two rings on one side (e.g. a 37-speaker
// layout with separate -19/0/+19/+43 rings plus a literal zenith speaker)
// could then have two genuinely different rings collapse onto the exact
// same elevation - and, for the zenith case, onto the exact same point as
// the T+000 pole - which libear's convex hull rejects as degenerate
// (coincident/collinear points). Snapping only the main ring avoids any
// such collision by construction, since only one layer is ever touched, and
// is all the originally reported bug needs: rings away from ear height
// don't suffer the same gap-filling miss (their own real elevation already
// falls inside the layer band extraPosVerticalNominal() checks for).
std::vector<double> computeNominalElevations(
    const std::vector<double>& realElevations,
    const std::vector<bool>& isLfe) {
  constexpr double kLayerGapDegrees = 15.0;

  std::vector<std::size_t> order;
  for (std::size_t i = 0; i < realElevations.size(); ++i) {
    if (!isLfe[i]) order.push_back(i);
  }
  std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
    return realElevations[a] < realElevations[b];
  });

  struct LayerInfo {
    std::size_t firstIndex;  // index into the elevation-sorted channel list
    std::size_t count;
  };
  std::vector<LayerInfo> layers;
  for (std::size_t pos = 0; pos < order.size(); ++pos) {
    if (pos == 0 || realElevations[order[pos]] -
                            realElevations[order[pos - 1]] >=
                        kLayerGapDegrees) {
      layers.push_back({pos, 0});
    }
    ++layers.back().count;
  }

  std::size_t mainLayer = 0;
  for (std::size_t l = 1; l < layers.size(); ++l) {
    if (layers[l].count > layers[mainLayer].count) mainLayer = l;
  }
  std::size_t mainLayerEnd = mainLayer + 1 < layers.size()
                                ? layers[mainLayer + 1].firstIndex
                                : order.size();

  std::vector<double> result = realElevations;
  for (std::size_t pos = layers[mainLayer].firstIndex; pos < mainLayerEnd;
      ++pos) {
    result[order[pos]] = 0.0;
  }
  return result;
}

// Shared by both the plugin's own text format and the IEM JSON format: turns
// the parsed per-line/per-entry data into the ear::Layout + output gains
// CustomLayout actually needs.
//
// libear's panning geometry (configureFullPolarPanner()'s Triplet/QuadRegion/
// VirtualNgon construction) builds its gain triangles from each channel's
// REAL position, not the nominal one - the nominal-elevation fix above only
// changes which facets get merged (the hull *topology*), not the angle
// actually fed into the gain maths for the facet an object lands in. A
// custom room's measured elevation can sit tens of degrees away from the
// canonical BS.2051 value (ceiling/wall constraints), and ADM content is
// authored against the canonical angles (e.g. "M layer = ear height = 0
// degrees"), so panning geometry built from the true measured angle doesn't
// line up with what the content expects - an object panned to elevation 0
// lands "below" a real ring that is actually sitting at 24-35 degrees,
// which is where the bleed came from (confirmed by testing: forcing the
// real elevation to the canonical value fixed it).
//
// The true measured elevation is only needed for the hull/layer
// classification above (computeNominalElevations) and is otherwise unused
// elsewhere in this plugin suite (distance-based gain/delay compensation -
// see custom_monitoring_plugin_processor.cpp - reads only .distance, never
// .elevation), so it's safe to pass the same canonical, layer-snapped
// elevation as BOTH the real and nominal position here: this makes a custom
// room's panning geometry behave like a well-installed standard layout
// (matching what content authored against canonical ADM positions expects),
// while the true measured azimuth and radius/distance - which the gain/
// delay compensation and stereo-position imaging genuinely depend on - are
// left untouched.
ear::plugin::CustomLayout buildCustomLayout(const std::vector<RawChannel>& raw,
                                            const std::string& layoutName) {
  if (raw.empty()) {
    throw ear::plugin::CustomLayoutFileError(
        "No speakers found in layout file (empty file, or every line/entry "
        "was skipped)");
  }

  std::vector<double> realElevations;
  std::vector<bool> isLfeFlags;
  for (auto const& ch : raw) {
    realElevations.push_back(ch.elevation);
    isLfeFlags.push_back(ch.isLfe);
  }
  auto canonicalElevations =
      computeNominalElevations(realElevations, isLfeFlags);

  std::vector<ear::Channel> channels;
  std::vector<double> outputGains;
  for (std::size_t i = 0; i < raw.size(); ++i) {
    auto const& ch = raw[i];
    // LFE channels are excluded from the panner layout entirely
    // (Layout::withoutLfe()), so canonicalising their elevation would do
    // nothing useful - keep their true measured elevation, it's harmless
    // and more informative if anything else ever inspects it.
    double panningElevation = ch.isLfe ? ch.elevation : canonicalElevations[i];
    ear::PolarPosition panningPosition{ch.azimuth, panningElevation, ch.radius};
    channels.emplace_back(ch.name, panningPosition, panningPosition,
                          boost::none, boost::none, ch.isLfe);
    outputGains.push_back(ch.gain);
  }

  ear::plugin::CustomLayout result;
  result.layout = ear::Layout{layoutName, channels};
  result.outputGains = std::move(outputGains);
  return result;
}

}  // namespace

namespace ear {
namespace plugin {

CustomLayout parseCustomLayout(const std::string& fileContents,
                               const std::string& layoutName) {
  std::vector<RawChannel> raw;
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

    raw.push_back({name, azimuth, elevation, radius, gain, isLfe});
  }

  return buildCustomLayout(raw, layoutName);
}

CustomLayout parseCustomLayoutIemJson(const std::string& fileContents,
                                      const std::string& layoutName) {
  nlohmann::json root;
  try {
    root = nlohmann::json::parse(fileContents);
  } catch (const nlohmann::json::parse_error& e) {
    throw CustomLayoutFileError(
        fmt::format("Invalid JSON: {}", e.what()));
  }

  // IEM AllRADecoder exports nest the speaker array under
  // "LoudspeakerLayout"/"Loudspeakers"; accept a top-level "Loudspeakers"
  // too, for anything that skips that wrapper.
  const nlohmann::json* speakersNode = nullptr;
  if (root.contains("LoudspeakerLayout") &&
      root["LoudspeakerLayout"].contains("Loudspeakers")) {
    speakersNode = &root["LoudspeakerLayout"]["Loudspeakers"];
  } else if (root.contains("Loudspeakers")) {
    speakersNode = &root["Loudspeakers"];
  } else {
    throw CustomLayoutFileError(
        "No \"LoudspeakerLayout\"/\"Loudspeakers\" array found in this JSON "
        "file - is it an IEM AllRADecoder loudspeaker layout export?");
  }
  if (!speakersNode->is_array()) {
    throw CustomLayoutFileError("\"Loudspeakers\" is not an array");
  }

  std::vector<RawChannel> raw;
  std::set<std::string> seenNames;
  int autoChannel = 0;
  for (auto const& entry : *speakersNode) {
    ++autoChannel;
    // AllRAD's own virtual filler positions (used to complete the decoder's
    // geometry), not real loudspeakers - skip them.
    if (entry.value("IsImaginary", false)) continue;

    double azimuth = entry.value("Azimuth", 0.0);
    double elevation = entry.value("Elevation", 0.0);
    double radius = entry.value("Radius", 1.0);
    double gain = entry.value("Gain", 1.0);
    // Non-standard extra field this plugin recognises on top of IEM's own
    // format - IEM's own tools ignore fields they don't know about, so this
    // doesn't break round-tripping the file through IEM's plugins.
    bool isLfe = entry.value("IsLFE", false);
    int channelNum = entry.value("Channel", autoChannel);

    if (radius <= 0.0) {
      throw CustomLayoutFileError(
          fmt::format("Loudspeaker entry {}: radius must be > 0", channelNum));
    }

    std::string name = fmt::format("CH{}", channelNum);
    if (!seenNames.insert(name).second) {
      throw CustomLayoutFileError(fmt::format(
          "Duplicate \"Channel\" value {} in loudspeaker layout", channelNum));
    }

    raw.push_back({name, azimuth, elevation, radius, gain, isLfe});
  }

  return buildCustomLayout(raw, layoutName);
}

bool hasLfeChannel(const CustomLayout& layout) {
  for (auto const& ch : layout.layout.channels()) {
    if (ch.isLfe()) return true;
  }
  return false;
}

void setChannelIsLfe(CustomLayout& layout, std::size_t index) {
  auto& channels = layout.layout.channels();
  if (index >= channels.size()) {
    throw CustomLayoutFileError("setChannelIsLfe: index out of range");
  }
  for (std::size_t i = 0; i < channels.size(); ++i) {
    channels[i].isLfe(i == index);
  }
}

void addLfeChannel(CustomLayout& layout, const std::string& name,
                   double azimuth, double elevation, double radius,
                   double gain) {
  if (radius <= 0.0) {
    throw CustomLayoutFileError("addLfeChannel: radius must be > 0");
  }
  auto& channels = layout.layout.channels();
  for (auto& ch : channels) ch.isLfe(false);
  channels.emplace_back(name, ear::PolarPosition{azimuth, elevation, radius},
                        boost::none, boost::none, boost::none, true);
  layout.outputGains.push_back(gain);
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
  std::string extension;
  if (dot != std::string::npos) {
    extension = name.substr(dot + 1);
    name = name.substr(0, dot);
  }
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  if (extension == "json") {
    return parseCustomLayoutIemJson(contents.str(), name);
  }
  return parseCustomLayout(contents.str(), name);
}

}  // namespace plugin
}  // namespace ear
