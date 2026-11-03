#pragma once

#include "JuceHeader.h"
#include "ir_reverb_plugin_processor.hpp"
#include "ir_reverb_point_controls_row.hpp"
#include "ir_reverb_scene_view.hpp"
#include "ir_reverb_add_items_popup.hpp"
#include "components/ear_button.hpp"
#include <array>
#include <memory>
#include <vector>

/** @brief IR Reverb's editor, at parity with the algorithmic Reverb's own
 * (see that plugin's reverb_plugin_editor.hpp): a left/right "+ Item"
 * card + EAR Scene-style sphere position overview (matching EAR Scene's
 * own ProgrammeView 50/50 split), titled section panels, a single Mix/
 * DSP preset combo, and a column-headed list of full Object-style
 * IrReverbPointControlsRow rows with a single global collapse-all
 * toggle.
 */
class IrReverbAudioProcessorEditor : public juce::AudioProcessorEditor {
 public:
  explicit IrReverbAudioProcessorEditor(IrReverbAudioProcessor* p);
  ~IrReverbAudioProcessorEditor() override;

  void paint(juce::Graphics&) override;
  void resized() override;

 private:
  void refreshActiveInputs();
  void openAddItemsPopup();
  void closeAddItemsPopup();
  void refreshIrCombo();
  void updateIrStatusLabel();
  void refreshPresetCombo(const juce::String& selectName = {});
  void refreshDspControlsFromProcessor();
  void layoutContent();

  IrReverbAudioProcessor* p_;

  juce::TooltipWindow tooltipWindow_{this, 500};

  juce::Viewport masterViewport_;
  juce::Component masterContent_;

  juce::Label titleLabel_;

  // Left/right pair, matching EAR Scene's own ProgrammeView::resized().
  juce::Label activeHeading_;
  juce::TextButton addItemButton_{"+ Item"};
  juce::Viewport activeListViewport_;
  juce::Component activeListContent_;

  juce::Label sceneViewHeading_;
  std::unique_ptr<IrReverbSceneView> sceneView_;

  struct ActiveRow {
    int slotIndex{-1};
    std::unique_ptr<juce::Component> stripe;
    std::unique_ptr<juce::Label> label;
    std::unique_ptr<juce::Label> positionLabel;
    std::unique_ptr<ear::plugin::ui::EarButton> removeButton;
  };
  std::vector<std::unique_ptr<ActiveRow>> activeRows_;

  std::unique_ptr<juce::Component> modalBackdrop_;
  std::unique_ptr<IrReverbAddItemsPopup> addItemsPopup_;

  std::unique_ptr<juce::Component> irPanel_;
  juce::Label irHeading_{{}, "Impulse Response"};
  juce::Label presetLabel_{{}, "Preset"};
  juce::ComboBox presetCombo_;
  juce::TextButton savePresetButton_{"Save As..."};
  juce::TextButton deletePresetButton_{"Delete"};
  juce::TextButton importIrButton_{"Import IR..."};
  juce::ComboBox irCombo_;
  juce::Label irChannelsWarning_;
  juce::ToggleButton preferArrayToggle_{"Interpret 9/10ch as array (PCMA-3D / direct), not AmbiX"};

  // "Levels & IR Shaping" sub-panel, same titled-box/compact-row visual
  // language as the algorithmic Reverb's own Mix/Decay & Diffusion
  // sub-panels (label + fixed-width slider + its own value readout,
  // kDspSubRowHeight-tall rows) - a full-width slider in a 24px row (the
  // Impulse Response panel's own plain rows) doesn't leave room for the
  // slider's value text box underneath, which read as "barely legible"
  // (see ReverbPointControlsRow's own column layout for the pattern this
  // copies).
  std::unique_ptr<juce::Component> shapingPanel_;
  juce::Label shapingHeading_{{}, "Levels & IR Shaping"};
  juce::Label wetLabel_{{}, "Wet level"};
  juce::Slider wetSlider_;
  juce::Label preDelayLabel_{{}, "Pre-delay (ms)"};
  juce::Slider preDelaySlider_;
  // Alters the loaded impulse response itself before convolution - see
  // IrReverbAudioProcessor::IrShapeParams.
  juce::Label stretchLabel_{{}, "Stretch"};
  juce::Slider stretchSlider_;
  juce::Label decayLabel_{{}, "Decay"};
  juce::Slider decaySlider_;
  juce::Label dampingLabel_{{}, "Damping"};
  juce::Slider dampingSlider_;

  juce::Label pointsHeading_{{}, "Output points"};
  juce::TextButton collapseAllButton_;
  bool allExpanded_{false};
  std::unique_ptr<juce::Component> pointsColumnHeader_;
  std::array<std::unique_ptr<IrReverbPointControlsRow>, IrReverbAudioProcessor::kNumPoints>
      pointRows_;

  // Assigns all kNumPoints points' routing at once to a contiguous block
  // of MAX_DAW_CHANNELS - see the algorithmic Reverb's own identical
  // control (reverb_plugin_editor.hpp) for the full explanation.
  juce::Label bulkRoutingLabel_{{}, "Assign output channels"};
  juce::ComboBox bulkRoutingCombo_;
  juce::TextEditor bulkRoutingJumpField_;

  std::unique_ptr<juce::FileChooser> fileChooser_;

  // --- Output/Input OSC (both collapsed by default) - see Point
  // Encoder's identical sections (point_encoder_plugin_editor.hpp).
  juce::TextButton oscOutputHeaderButton_;
  bool oscOutputExpanded_{false};
  juce::ToggleButton oscOutputEnableToggle_{"Enabled"};
  juce::Label oscOutputIpLabel_{{}, "IP"};
  juce::TextEditor oscOutputIpEditor_;
  juce::Label oscOutputPortLabel_{{}, "Port"};
  juce::TextEditor oscOutputPortEditor_;

  juce::TextButton oscInputHeaderButton_;
  bool oscInputExpanded_{false};
  juce::ToggleButton oscInputEnableToggle_{"Enabled"};
  juce::Label oscInputPortLabel_{{}, "Port"};
  juce::TextEditor oscInputPortEditor_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IrReverbAudioProcessorEditor)
};
