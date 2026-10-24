#pragma once

#include "JuceHeader.h"
#include "reverb_plugin_processor.hpp"

#include "components/ear_button.hpp"
#include "components/look_and_feel/colours.hpp"
#include "components/look_and_feel/fonts.hpp"

#include <memory>

/** @brief A read-only position overview styled like EAR Scene's own
 * "sphere" element view (binary_data::previous_icon_svg/next_icon_svg
 * rotate buttons, the same grid/orientation-sphere look and colours),
 * showing where every active source and all 9 output points currently
 * sit at a glance instead of as rows of numbers.
 *
 * Unlike EAR Scene's own ElementOverview this isn't driven by a
 * Programme/ProgrammeObject - it reads straight from
 * ReverbAudioProcessor's own getAvailableObjects()/getActiveInputSlots()/
 * getPointParams(), so it has no dependency on Scene's data model. It's
 * purely a display: positions are still edited with the numeric sliders
 * elsewhere in the editor, not by dragging here.
 */
class ReverbSceneView : public juce::Component, private juce::Timer {
 public:
  explicit ReverbSceneView(ReverbAudioProcessor* p);
  ~ReverbSceneView() override;

  void paint(juce::Graphics&) override;
  void resized() override;

  // Call whenever positions might have changed (points edited, Scene
  // broadcast updated an active source's live position).
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

  ReverbAudioProcessor* p_;

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

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbSceneView)
};
