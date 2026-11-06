#pragma once

#include "ui/object_frontend_backend_connector.hpp"
#include <string>

namespace ear {
namespace plugin {
namespace ui {

/** @brief A ObjectsFrontendBackendConnector with no UI widgets of its own -
 * just a push() that forwards a fully-computed object state straight to
 * whatever ObjectBackend it's paired with.
 *
 * ObjectStereoAudioProcessor computes TWO independent object positions
 * (centre +/- the pair-distance/rotation offset - see its recomputeAndPush())
 * from one shared set of UI-facing parameters, and needs one of these per
 * object to push each into its own ObjectBackend/Scene connection. The real,
 * widget-bound connector (ObjectStereoJuceFrontendConnector) only binds the
 * shared parameters' sliders/combo boxes; it never talks to a backend
 * directly.
 */
class DerivedObjectConnector : public ObjectsFrontendBackendConnector {
 public:
  void push(int routing, float gainLinear, float azimuthDeg, float elevationDeg,
           float distance, float widthDeg, float heightDeg, float depth,
           float diffuse, const std::string& name, uint32_t colourArgb) {
    notifyParameterChanged(ParameterId::ROUTING, routing);
    notifyParameterChanged(ParameterId::GAIN, gainLinear);
    notifyParameterChanged(ParameterId::AZIMUTH, azimuthDeg);
    notifyParameterChanged(ParameterId::ELEVATION, elevationDeg);
    notifyParameterChanged(ParameterId::DISTANCE, distance);
    notifyParameterChanged(ParameterId::WIDTH, widthDeg);
    notifyParameterChanged(ParameterId::HEIGHT, heightDeg);
    notifyParameterChanged(ParameterId::DEPTH, depth);
    notifyParameterChanged(ParameterId::DIFFUSE, diffuse);
    notifyParameterChanged(ParameterId::NAME, name);
    notifyParameterChanged(ParameterId::COLOUR, colourArgb);
  }

  /** @brief Tells Scene which reaper-adm-extension Input-plugin instance ID
   * this derived object corresponds to, so ADM export can resolve the
   * routed DAW channel back to this plugin instance - same mechanism as
   * point_encoder's PointObjectConnector::setInputInstanceId(), needed once
   * per represented object/channel, not just once per plugin instance. */
  void setInputInstanceId(int id) {
    notifyParameterChanged(ParameterId::INPUT_INSTANCE_ID, id);
  }

 protected:
  void doSetStatusBarText(const std::string&) override {
    // No UI for this connector to show a status bar on.
  }
};

}  // namespace ui
}  // namespace plugin
}  // namespace ear
