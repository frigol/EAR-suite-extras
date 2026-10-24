#pragma once

#include "ui/object_frontend_backend_connector.hpp"

namespace ear {
namespace plugin {

/** @brief Minimal concrete ObjectsFrontendBackendConnector for one of the
 * Reverb plugin's internal "points" (one of the 10 positions in its own
 * "4+6+0" layout - see ReverbAudioProcessor::buildPointLayout()). Each
 * point drives its own ear::plugin::
 * ObjectBackend connection to Scene - the exact mechanism a real EAR Object
 * plugin uses for its single object - but with no JUCE AudioProcessorParameter
 * binding: routing is user-controlled via a plain combo box, and position/
 * gain are pushed programmatically by the reverb engine (computed from the
 * selected source object's live position), not user-automated per point.
 *
 * ObjectBackend's constructor registers itself as this connector's parameter-
 * changed listener (see object_backend.cpp), so it's safe to call the
 * setters below as soon as the owning ObjectBackend has been constructed
 * with a pointer to this connector.
 *
 * As of Phase 3, position/gain/width/height/depth/diffuse are no longer
 * only pushed automatically from the DSP's nominal ITU layout - the user
 * can override every one of them per point from the editor, exactly as on
 * a real EAR Object, via ReverbAudioProcessor::setPointPosition() etc.
 *
 * The underlying ParameterValue is a boost::variant<float, bool, int,
 * uint32_t, boost::optional<float>, std::string> (object_frontend_backend_
 * connector.hpp) - ObjectBackend::onParameterChanged() does a hard
 * boost::get<float>/<int>/... keyed on which ParameterId fired, so every
 * setter here must pass the exact type it expects (float for position/gain,
 * int for routing, not double/unsigned - a mismatched alternative throws
 * boost::bad_get at the receiving end instead of failing to compile here).
 */
class ReverbPointConnector : public ui::ObjectsFrontendBackendConnector {
 public:
  void setRouting(int sceneChannel) {
    notifyParameterChanged(ParameterId::ROUTING, sceneChannel);
  }
  void setInputInstanceId(int id) {
    // ObjectBackend::onParameterChanged() does boost::get<int> for this
    // id (not uint32_t, unlike ROUTING/GAIN's own types) - see
    // object_backend.cpp.
    notifyParameterChanged(ParameterId::INPUT_INSTANCE_ID, id);
  }
  void setName(const std::string& name) {
    notifyParameterChanged(ParameterId::NAME, name);
  }
  void setColour(uint32_t argb) {
    notifyParameterChanged(ParameterId::COLOUR, argb);
  }
  void setGain(float linearGain) {
    notifyParameterChanged(ParameterId::GAIN, linearGain);
  }
  void setPosition(float azimuthDeg, float elevationDeg, float distance) {
    notifyParameterChanged(ParameterId::AZIMUTH, azimuthDeg);
    notifyParameterChanged(ParameterId::ELEVATION, elevationDeg);
    notifyParameterChanged(ParameterId::DISTANCE, distance);
  }
  void setDiffuse(float diffuse) {
    notifyParameterChanged(ParameterId::DIFFUSE, diffuse);
  }
  void setWidth(float width) {
    notifyParameterChanged(ParameterId::WIDTH, width);
  }
  void setHeight(float height) {
    notifyParameterChanged(ParameterId::HEIGHT, height);
  }
  void setDepth(float depth) {
    notifyParameterChanged(ParameterId::DEPTH, depth);
  }

 protected:
  // No status bar in this plugin's UI (yet) - Phase 1 has no per-point
  // status display; connection state across all 9 points is surfaced at
  // the plugin level instead (see ReverbAudioProcessorEditor).
  void doSetStatusBarText(const std::string&) override {}
};

}  // namespace plugin
}  // namespace ear
