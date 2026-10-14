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
 * @throws CustomLayoutFileError on any parse error (message includes the
 *     offending line number where applicable).
 */
EAR_PLUGIN_BASE_EXPORT CustomLayout
loadCustomLayoutFromFile(const std::string& filePath);

/** @brief As loadCustomLayoutFromFile(), but parses already-read file
 * contents. Exposed mainly for testing. */
EAR_PLUGIN_BASE_EXPORT CustomLayout
parseCustomLayout(const std::string& fileContents,
                   const std::string& layoutName = "custom");

}  // namespace plugin
}  // namespace ear
