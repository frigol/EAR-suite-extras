#pragma once

#include "JuceHeader.h"
#include "ir_reverb_plugin_processor.hpp"
#include <memory>

/** @brief One output point's full control row - copied from the
 * algorithmic Reverb's own ReverbPointControlsRow (now that IR Reverb's
 * PointParams/getters/setters mirror that plugin's 1:1): an Active
 * toggle (default ON), the routing selector, plus every Object-style
 * parameter (position, gain, width/height/depth, diffuse), so each of
 * the 10 points can be adjusted afterwards exactly like a normal Object,
 * not just routed.
 *
 * No per-row text label is drawn next to each slider - IrReverbAudio
 * ProcessorEditor draws one column-header row, aligned to kColumnWidth
 * below, once above the whole list. This row has no collapse state of
 * its own - the editor's single global "Collapse all" button hides/shows
 * the entire output-points section as one block.
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

class IrReverbPointControlsRow : public juce::Component {
 public:
  IrReverbPointControlsRow(IrReverbAudioProcessor* p, int pointIndex);

  void paint(juce::Graphics&) override;
  void resized() override;

  static constexpr int kTopRowHeight = 24;
  static constexpr int kSliderRowHeight = 36;
  static constexpr int kPreferredHeight = kTopRowHeight + kSliderRowHeight;

  static constexpr int kActiveWidth = 60;
  static constexpr int kNameWidth = 150;
  static constexpr int kRoutingWidth = 150;
  static constexpr int kJumpWidth = 34;
  static constexpr int kColumnWidth = 82;
  static constexpr int kColumnGap = 4;
  static constexpr int kNumColumns = 8;  // Az El Dist Gain W H D Diff
  static constexpr int kResetWidth = 60;

  std::function<void()> onChanged;

  void refreshName();

  // Re-reads every parameter (including routing) from the processor -
  // called by the editor after a bulk output-channel assignment (see
  // IrReverbAudioProcessorEditor's own "Assign range..." control) changes
  // routing out from under this row.
  void refreshFromProcessor();

 private:
  IrReverbAudioProcessor* p_;
  int index_;

  juce::ToggleButton activeToggle_{"Active"};
  juce::Label nameLabel_;
  juce::ComboBox routingCombo_;
  // Type a channel number here and press Enter to jump routingCombo_
  // straight to it - see setupChannelJumpField() in the .cpp.
  juce::TextEditor routingJumpField_;

  AzimuthSlider azSlider_;
  juce::Slider elSlider_;
  juce::Slider distSlider_;
  juce::Slider gainSlider_;
  juce::Slider widthSlider_;
  juce::Slider heightSlider_;
  juce::Slider depthSlider_;
  juce::Slider diffuseSlider_;
  juce::TextButton resetButton_{"Reset"};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IrReverbPointControlsRow)
};
