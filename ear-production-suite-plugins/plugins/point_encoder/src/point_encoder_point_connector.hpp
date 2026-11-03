#pragma once

#include "ui/object_frontend_backend_connector.hpp"

namespace ear {
namespace plugin {

/** @brief Minimal concrete ObjectsFrontendBackendConnector for one of the
 * Point Encoder plugin's up to kMaxPoints slots - copied verbatim from the
 * algorithmic Reverb's own ReverbPointConnector (now retired from that
 * plugin - see its class comment for why this plugin exists). Each active
 * point drives its own ear::plugin::ObjectBackend connection to Scene,
 * exactly the mechanism a real EAR Object plugin uses for its single
 * object, but with no JUCE AudioProcessorParameter binding: every setter
 * is called programmatically from the processor (see
 * PointEncoderAudioProcessor::setPointXxx()), not from automation.
 *
 * ObjectBackend's constructor registers itself as this connector's
 * parameter-changed listener, so it's safe to call the setters below as
 * soon as the owning ObjectBackend has been constructed with a pointer to
 * this connector.
 *
 * The underlying ParameterValue is a boost::variant<float, bool, int,
 * uint32_t, boost::optional<float>, std::string> - ObjectBackend::
 * onParameterChanged() does a hard boost::get<float>/<int>/... keyed on
 * which ParameterId fired, so every setter here must pass the exact type
 * it expects (float for position/gain, int for routing/instance id, not
 * double/unsigned).
 */
class PointEncoderPointConnector : public ui::ObjectsFrontendBackendConnector {
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
