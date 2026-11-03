#pragma once

#include "JuceHeader.h"
#include "point_encoder_plugin_processor.hpp"

#include "components/ear_button.hpp"
#include "components/look_and_feel/colours.hpp"
#include "components/look_and_feel/fonts.hpp"

#include <memory>

/** @brief Read-only position overview, copied from the algorithmic
 * Reverb's own ReverbSceneView (EAR Scene-style sphere, rotate buttons,
 * orientation compass) - simplified to just the active points (no
 * separate "source" overlay, since this plugin has no Scene-discovered
 * input list of its own - see PointEncoderAudioProcessor's class
 * comment). Display only.
 */
class PointEncoderSceneView : public juce::Component, private juce::Timer {
 public:
  explicit PointEncoderSceneView(PointEncoderAudioProcessor* p);
  ~PointEncoderSceneView() override;

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

  PointEncoderAudioProcessor* p_;

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

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PointEncoderSceneView)
};
