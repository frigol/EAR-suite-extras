#pragma once

#include "JuceHeader.h"
#include "point_encoder_plugin_processor.hpp"
#include <memory>

/** @brief One point slot's full control row: Active toggle, editable name,
 * "which input channel feeds this point" mapping combo, and the full
 * Object-style parameter set (position, gain, width/height/depth,
 * diffuse) - the same parameters a real EAR Object exposes, same as the
 * algorithmic Reverb's own points used to have before the "Point Encoder"
 * split (see PointEncoderAudioProcessor's class comment).
 *
 * No per-row text label is drawn next to each slider - PointEncoderAudio
 * ProcessorEditor draws one column-header row, aligned to kColumnWidth
 * below, once above the whole list.
 */
// A LinearHorizontal slider with its minimum at the right edge and
// maximum at the left - see the algorithmic Reverb's own AzimuthSlider
// (reverb_point_controls_row.hpp) for the full explanation.
class AzimuthSlider : public juce::Slider {
 public:
  double valueToProportionOfLength(double value) override {
    return (getMaximum() - value) / (getMaximum() - getMinimum());
  }
  double proportionOfLengthToValue(double proportion) override {
    return getMaximum() - proportion * (getMaximum() - getMinimum());
  }
};

class PointEncoderPointControlsRow : public juce::Component {
 public:
  PointEncoderPointControlsRow(PointEncoderAudioProcessor* p, int pointIndex);

  void paint(juce::Graphics&) override;
  void resized() override;

  static constexpr int kTopRowHeight = 24;
  static constexpr int kSliderRowHeight = 36;
  static constexpr int kPreferredHeight = kTopRowHeight + kSliderRowHeight;

  static constexpr int kActiveWidth = 60;
  static constexpr int kNameWidth = 150;
  static constexpr int kChannelWidth = 120;
  static constexpr int kEarChannelWidth = 120;
  static constexpr int kJumpWidth = 34;
  static constexpr int kColumnWidth = 82;
  static constexpr int kColumnGap = 4;
  static constexpr int kNumColumns = 8;  // Az El Dist Gain W H D Diff
  static constexpr int kResetWidth = 60;

  std::function<void()> onChanged;

  void refreshFromProcessor();

 private:
  PointEncoderAudioProcessor* p_;
  int index_;

  juce::ToggleButton activeToggle_{"Active"};
  juce::TextEditor nameEditor_;
  juce::ComboBox channelCombo_;
  // Type a channel number here and press Enter to jump channelCombo_
  // straight to it - see setupChannelJumpField() in the .cpp.
  juce::TextEditor channelJumpField_;
  // Which channel this point reports itself on to Scene - distinct from
  // channelCombo_ (which of this plugin's own inputs feeds the point's
  // audio): see PointState::earChannel's own comment for why these are
  // separate.
  juce::ComboBox earChannelCombo_;
  juce::TextEditor earChannelJumpField_;

  AzimuthSlider azSlider_;
  juce::Slider elSlider_;
  juce::Slider distSlider_;
  juce::Slider gainSlider_;
  juce::Slider widthSlider_;
  juce::Slider heightSlider_;
  juce::Slider depthSlider_;
  juce::Slider diffuseSlider_;
  juce::TextButton resetButton_{"Reset"};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PointEncoderPointControlsRow)
};
