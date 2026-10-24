#pragma once

#include "ui/object_frontend_backend_connector.hpp"

namespace ear {
namespace plugin {

/** @brief Minimal concrete ObjectsFrontendBackendConnector for one of the
 * IR Reverb's 10 output points - identical in spirit to the algorithmic
 * Reverb's own ReverbPointConnector (see reverb_point_connector.hpp in
 * that plugin): registers one ear::plugin::ObjectBackend connection to
 * Scene per point, with no JUCE AudioProcessorParameter binding - routing,
 * name and colour are pushed programmatically (name/colour following the
 * host track, see IrReverbAudioProcessor::updateTrackProperties()).
 *
 * v1 scope: no per-point position/gain/width/height/depth/diffuse
 * overrides yet (unlike the algorithmic Reverb's points, which expose the
 * full Object parameter set) - each point stays at its nominal "4+6+0"
 * position. Deferred, not forgotten.
 */
class IrReverbPointConnector : public ui::ObjectsFrontendBackendConnector {
 public:
  void setRouting(int sceneChannel) {
    notifyParameterChanged(ParameterId::ROUTING, sceneChannel);
  }
  void setName(const std::string& name) {
    notifyParameterChanged(ParameterId::NAME, name);
  }
  void setColour(uint32_t argb) {
    notifyParameterChanged(ParameterId::COLOUR, argb);
  }
  void setPosition(float azimuthDeg, float elevationDeg, float distance) {
    notifyParameterChanged(ParameterId::AZIMUTH, azimuthDeg);
    notifyParameterChanged(ParameterId::ELEVATION, elevationDeg);
    notifyParameterChanged(ParameterId::DISTANCE, distance);
  }
  // No per-point UI for these yet (see class comment), but Scene still
  // needs a value for every one of a real Object's parameters - leaving
  // gain/width/height/depth/diffuse completely unset (never notified at
  // all) left Scene's object record for each point incomplete, which
  // took down Scene's entire output, not just these points' own signal,
  // as soon as this plugin's points were routed in. Sent once at
  // construction with the same defaults the algorithmic Reverb's own
  // points start at.
  void setGain(float linearGain) {
    notifyParameterChanged(ParameterId::GAIN, linearGain);
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
  void setDiffuse(float diffuse) {
    notifyParameterChanged(ParameterId::DIFFUSE, diffuse);
  }
  void setInputInstanceId(int id) {
    notifyParameterChanged(ParameterId::INPUT_INSTANCE_ID, id);
  }

 protected:
  void doSetStatusBarText(const std::string&) override {}
};

}  // namespace plugin
}  // namespace ear
