#pragma once

#include "ui/object_frontend_backend_connector.hpp"

namespace ear {
namespace plugin {

/** @brief Minimal concrete ObjectsFrontendBackendConnector for one of the
 * IR Reverb plugin's internal "points" - copied verbatim from the
 * algorithmic Reverb's own ReverbPointConnector (see that plugin's
 * comment for the full explanation; identical mechanism here). Each point
 * drives its own ear::plugin::ObjectBackend connection to Scene, with no
 * JUCE AudioProcessorParameter binding - position/gain etc are pushed
 * programmatically by IrReverbAudioProcessor.
 */
class IrReverbPointConnector : public ui::ObjectsFrontendBackendConnector {
 public:
  void setRouting(int sceneChannel) {
    notifyParameterChanged(ParameterId::ROUTING, sceneChannel);
  }
  void setInputInstanceId(int id) {
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
  void doSetStatusBarText(const std::string&) override {}
};

}  // namespace plugin
}  // namespace ear
