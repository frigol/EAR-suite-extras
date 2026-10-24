#pragma once

#include "JuceHeader.h"
#include "reverb_plugin_processor.hpp"
#include <memory>

/** @brief One output point's full control row: the routing selector plus
 * every Object-style parameter (position, gain, width/height/depth,
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
  static constexpr int kNameWidth = 150;
  static constexpr int kRoutingWidth = 150;
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

 private:
  void refreshFromProcessor();

  ReverbAudioProcessor* p_;
  int index_;

  juce::Label nameLabel_;
  juce::ComboBox routingCombo_;

  juce::Slider azSlider_;
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
