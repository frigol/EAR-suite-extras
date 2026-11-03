#pragma once

#include "JuceHeader.h"
#include "point_encoder_plugin_processor.hpp"
#include "point_encoder_point_controls_row.hpp"
#include "point_encoder_scene_view.hpp"
#include <array>
#include <memory>

/** @brief Plain functional UI: a "Load layout..." file picker (same text
 * format as Custom Monitoring's own), a position overview sphere, and a
 * scrollable list of all kMaxPoints point rows (see
 * PointEncoderPointControlsRow), each independently activatable.
 *
 * Everything lives inside one scrollable masterContent_/masterViewport_
 * pair, same pattern as the algorithmic Reverb's own editor.
 */
class PointEncoderAudioProcessorEditor : public juce::AudioProcessorEditor {
 public:
  explicit PointEncoderAudioProcessorEditor(PointEncoderAudioProcessor* p);
  ~PointEncoderAudioProcessorEditor() override;

  void paint(juce::Graphics&) override;
  void resized() override;

 private:
  void layoutContent();
  void promptLoadLayout();

  PointEncoderAudioProcessor* p_;

  juce::TooltipWindow tooltipWindow_{this, 500};

  juce::Viewport masterViewport_;
  juce::Component masterContent_;

  juce::Label titleLabel_;

  juce::Label layoutPresetLabel_{{}, "Layout preset"};
  juce::ComboBox layoutPresetCombo_;
  juce::TextButton loadLayoutButton_{"Load layout..."};
  juce::Label layoutStatusLabel_;
  std::unique_ptr<juce::FileChooser> fileChooser_;

  juce::Label sceneViewHeading_;
  std::unique_ptr<PointEncoderSceneView> sceneView_;

  juce::Label pointsHeading_;
  juce::TextButton collapseAllButton_;
  bool allExpanded_{true};
  std::unique_ptr<juce::Component> pointsColumnHeader_;
  std::array<std::unique_ptr<PointEncoderPointControlsRow>,
            PointEncoderAudioProcessor::kMaxPoints>
      pointRows_;

  // --- Output OSC (collapsed by default) - reports each active point's
  // position/state to a remote IP:port. Collapse state is local editor
  // state (not persisted on the processor like the points list is): these
  // sections aren't meant to stay open across reopens, only the points
  // list's own state matters for that - see getPointsExpanded()'s comment.
  juce::TextButton oscOutputHeaderButton_;
  bool oscOutputExpanded_{false};
  juce::ToggleButton oscOutputEnableToggle_{"Enabled"};
  juce::Label oscOutputIpLabel_{{}, "IP"};
  juce::TextEditor oscOutputIpEditor_;
  juce::Label oscOutputPortLabel_{{}, "Port"};
  juce::TextEditor oscOutputPortEditor_;

  // --- Input OSC (collapsed by default) - lets /point/<n>/az /el /dist
  // /active drive this plugin's points remotely.
  juce::TextButton oscInputHeaderButton_;
  bool oscInputExpanded_{false};
  juce::ToggleButton oscInputEnableToggle_{"Enabled"};
  juce::Label oscInputPortLabel_{{}, "Port"};
  juce::TextEditor oscInputPortEditor_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PointEncoderAudioProcessorEditor)
};
