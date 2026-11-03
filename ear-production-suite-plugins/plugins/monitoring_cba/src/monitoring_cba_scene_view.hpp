#pragma once

#include "JuceHeader.h"
#include "monitoring_cba_plugin_processor.hpp"

#include "components/ear_button.hpp"
#include "components/look_and_feel/colours.hpp"
#include "components/look_and_feel/fonts.hpp"

#include <memory>

/** @brief Read-only position overview, copied from the algorithmic
 * Reverb's own ReverbSceneView (EAR Scene-style sphere, rotate buttons,
 * orientation compass) - simplified to just the active input sources (no
 * "points" of its own to show, since this plugin's output is plain
 * channel-based audio, not individually positioned Objects). Display
 * only.
 */
class MonitoringCbaSceneView : public juce::Component, private juce::Timer {
 public:
  explicit MonitoringCbaSceneView(MonitoringCbaAudioProcessor* p);
  ~MonitoringCbaSceneView() override;

  void paint(juce::Graphics&) override;
  void resized() override;

  void refresh();

 private:
  void rebuildBackground();
  void rotate(float angleDeg);
  void timerCallback() override;
  void updateViewingAngle();
  void drawDot(juce::Graphics& g, float azimuthDeg, float elevationDeg,
              float distance, juce::Colour colour);
  static int getSectorIndex(float viewingAngleDeg);
  void paintOrientationCompass(juce::Graphics& g, juce::Rectangle<float> area);

  MonitoringCbaAudioProcessor* p_;

  std::unique_ptr<ear::plugin::ui::EarButton> rotateLeftButton_;
  std::unique_ptr<ear::plugin::ui::EarButton> rotateRightButton_;

  juce::Image cachedBackground_;
  juce::Rectangle<float> sphereArea_;
  juce::Rectangle<float> compassArea_;

  float startViewingAngle_{0.0f};
  float currentViewingAngle_{180.0f};
  float endViewingAngle_{180.0f};

  static constexpr float kDepthFactor = 0.3f;
  static constexpr float kDotRadius = 8.0f;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MonitoringCbaSceneView)
};
