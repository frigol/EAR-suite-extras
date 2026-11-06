#pragma once

#include "ear-plugin-base/export.h"
#include <ear/ear.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace ear {
namespace plugin {

/** @brief Thrown when a custom loudspeaker layout file cannot be parsed. */
class EAR_PLUGIN_BASE_EXPORT CustomLayoutFileError : public std::runtime_error {
 public:
  explicit CustomLayoutFileError(const std::string& message)
      : std::runtime_error(message) {}
};

/** @brief A custom loudspeaker layout loaded from a simple text/csv file,
 * together with the per-output trim gains that libear's Layout has no room
 * for (libear only knows position + LFE flag).
 *
 * File format, one speaker per line:
 *   name azimuth elevation [radius] [gain] [lfe]
 * Fields may be separated by whitespace and/or commas. Lines starting with
 * '#' (after leading whitespace) and blank lines are ignored.
 *   - azimuth, elevation: degrees. Azimuth 0 = front, positive = anticlockwise
 *     (left), matching the ITU-R BS.2051 / libear convention.
 *   - radius: metres, defaults to 1.0.
 *   - gain: linear trim applied to this output after EAR's gain calculation,
 *     defaults to 1.0.
 *   - lfe: 0/1, true/false, yes/no; defaults to 0. LFE speakers are excluded
 *     from the object point-source panner (as with any libear layout) but
 *     still receive direct/diffuse gain for DirectSpeakers/HOA content that
 *     explicitly targets them.
 *
 * A speaker placed at exactly (elevation 90, i.e. the zenith) MUST be named
 * exactly "T+000" (case-sensitive, matching the BS.2051 name for that
 * position). libear's point-source panner (point_source_panner.cpp,
 * getAugmentedLayout()) always adds its own virtual top speaker at that same
 * position UNLESS the layout already has a real channel named "T+000" or
 * "UH+180" - without that name, the real and virtual points coincide exactly,
 * and libear's convex-hull construction throws "collinear points in convex
 * hull" for any triple involving both (they're two names for the same point,
 * so every such triangle has zero area). There is no equivalent workaround
 * for a real speaker placed exactly at the nadir (elevation -90): libear
 * always adds its own virtual bottom speaker there regardless of channel
 * name, so a real speaker at exactly -90 elevation will always hit this same
 * crash - nudge it a fraction of a degree off the exact pole instead.
 */
struct EAR_PLUGIN_BASE_EXPORT CustomLayout {
  ear::Layout layout;
  std::vector<double> outputGains;  // parallel to layout.channels()
};

/** @brief Parse a custom loudspeaker layout from the given file.
 *
 * Two formats are supported, chosen by the file's extension:
 *   - ".txt"/".csv" (or anything else): the plugin's own simple text format,
 *     documented on CustomLayout above.
 *   - ".json": an IEM AllRADecoder loudspeaker layout export (the
 *     "LoudspeakerLayout"/"Loudspeakers" array of {Azimuth, Elevation,
 *     Radius, Gain, Channel, IsImaginary} objects). IEM's azimuth/elevation
 *     convention already matches BS.2051/libear's (0 = front, positive
 *     azimuth = left, positive elevation = up), so no sign conversion is
 *     applied. Entries with "IsImaginary": true are AllRAD's own virtual
 *     filler positions (not real loudspeakers) and are skipped. Channels
 *     are named "CH<n>" from their "Channel" field.
 *
 *     IEM's format has no concept of an LFE/subwoofer channel, so nothing
 *     is marked as LFE unless the file carries this plugin's own
 *     non-standard extra field "IsLFE": true/false on a loudspeaker entry
 *     (IEM's own tools ignore fields they don't recognise, so adding this
 *     by hand keeps the file working in IEM's plugins too). Use
 *     hasLfeChannel(), setChannelIsLfe() and addLfeChannel() below to let
 *     the user resolve this after loading, rather than failing the load.
 *
 * @throws CustomLayoutFileError on any parse error (message includes the
 *     offending line number where applicable, for the text format).
 */
EAR_PLUGIN_BASE_EXPORT CustomLayout
loadCustomLayoutFromFile(const std::string& filePath);

/** @brief As loadCustomLayoutFromFile() for the plugin's own text format,
 * but parses already-read file contents. Exposed mainly for testing. */
EAR_PLUGIN_BASE_EXPORT CustomLayout
parseCustomLayout(const std::string& fileContents,
                   const std::string& layoutName = "custom");

/** @brief As loadCustomLayoutFromFile() for the IEM JSON format, but parses
 * already-read file contents. Exposed mainly for testing. */
EAR_PLUGIN_BASE_EXPORT CustomLayout
parseCustomLayoutIemJson(const std::string& fileContents,
                         const std::string& layoutName = "custom");

/** @brief True if any channel in the layout is already marked as LFE. */
EAR_PLUGIN_BASE_EXPORT bool hasLfeChannel(const CustomLayout& layout);

/** @brief Mark an existing channel (by its index into layout.layout.channels())
 * as LFE, clearing the flag on every other channel first (a loudspeaker
 * layout here has at most one LFE output). */
EAR_PLUGIN_BASE_EXPORT void setChannelIsLfe(CustomLayout& layout,
                                            std::size_t index);

/** @brief Append a brand-new channel to the layout and mark it as LFE,
 * clearing the flag on every other channel first. For IEM JSON imports,
 * which never describe a subwoofer position at all. */
EAR_PLUGIN_BASE_EXPORT void addLfeChannel(CustomLayout& layout,
                                          const std::string& name,
                                          double azimuth, double elevation,
                                          double radius, double gain);

}  // namespace plugin
}  // namespace ear
