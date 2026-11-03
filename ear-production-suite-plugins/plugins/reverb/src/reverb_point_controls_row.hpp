#pragma once

#include "JuceHeader.h"
#include "reverb_plugin_processor.hpp"
#include <memory>

/** @brief One output point's full control row: an Active toggle (default
 * ON - see ReverbAudioProcessor's class comment), the routing selector,
 * and every Object-style parameter (position, gain, width/height/depth,
 * diffuse) - the same parameters a real EAR Object exposes - so each of
 * the 10 points can be adjusted afterwards exactly like a normal Object,
 * not just routed. Values default to the point's nominal direction and
 * are pushed to the processor (and from there to Scene) as the user
 * drags each slider.
 *
 * No per-row text label is drawn next to each slider - with 10 rows a
 * repeated short label ("Az", "El", ...) reads as equidistant/ambiguous
 * about which value it names; ReverbAudioProcessorEditor instead draws
 * one column-header row, aligned to kColumnX/kColumnWidth below, once
 * above the whole list. This row has no collapse state of its own - the
 * editor's single global "Collapse all" button hides/shows the entire
 * output-points section (this row, every other row, and the column
 * header) as one block via setVisible(), not a per-row partial collapse.
 * Painted with alternating-row shading (even/odd by point index) so a
 * value can be tracked back to its row at a glance.
 */
// A LinearHorizontal slider with its minimum at the right edge and
// maximum at the left, instead of a plain slider's own left=min/
// right=max - ITU/ADM convention has positive azimuth to the left
// (anticlockwise), so this makes dragging left visually increase the
// value and dragging right decrease it, matching that convention rather
// than fighting it. juce::Slider::valueToProportionOfLength()/
// proportionOfLengthToValue() are virtual methods meant to be overridden
// exactly like this (not assignable std::function members).
class AzimuthSlider : public juce::Slider {
 public:
  double valueToProportionOfLength(double value) override {
    return (getMaximum() - value) / (getMaximum() - getMinimum());
  }
  double proportionOfLengthToValue(double proportion) override {
    return getMaximum() - proportion * (getMaximum() - getMinimum());
  }
};

class ReverbPointControlsRow : public juce::Component {
 public:
  ReverbPointControlsRow(ReverbAudioProcessor* p, int pointIndex);

  void paint(juce::Graphics&) override;
  void resized() override;

  static constexpr int kTopRowHeight = 24;
  static constexpr int kSliderRowHeight = 36;
  static constexpr int kPreferredHeight = kTopRowHeight + kSliderRowHeight;

  // Column x-offsets/width shared with the editor's header row, so the
  // header's titles line up exactly with these sliders regardless of
  // window width.
  static constexpr int kActiveWidth = 60;
  static constexpr int kNameWidth = 150;
  static constexpr int kRoutingWidth = 150;
  static constexpr int kJumpWidth = 34;
  static constexpr int kColumnWidth = 82;
  static constexpr int kColumnGap = 4;
  static constexpr int kNumColumns = 8;  // Az El Dist Gain W H D Diff
  static constexpr int kResetWidth = 60;

  // Fired after any parameter change here, so the scene-position overview
  // (which has no other way to know a point moved) can repaint.
  std::function<void()> onChanged;

  // Re-reads p_->getPointDisplayName(index_) - called once at construction
  // and again whenever the host reports a track name change (see
  // ReverbAudioProcessor::onTrackNameChanged).
  void refreshName();

  // Re-reads every parameter (including routing) from the processor -
  // called after construction's reset button, and by the editor after a
  // bulk output-channel assignment (see ReverbAudioProcessorEditor's own
  // "Assign range..." control) changes routing out from under this row.
  void refreshFromProcessor();

 private:
  ReverbAudioProcessor* p_;
  int index_;

  juce::ToggleButton activeToggle_{"Active"};
  juce::Label nameLabel_;
  juce::ComboBox routingCombo_;
  // Type a channel number here and press Enter to jump routingCombo_
  // straight to it, instead of scrolling a 128-entry list - see
  // setupChannelJumpField() in the .cpp.
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

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbPointControlsRow)
};
