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
  juce::Label wetLabel_{{}, "Wet level"};
  juce::Slider wetSlider_;
  juce::Label preDelayLabel_{{}, "Pre-delay (ms)"};
  juce::Slider preDelaySlider_;

  juce::Label pointsHeading_{{}, "Output points"};
  juce::TextButton collapseAllButton_;
  bool allExpanded_{true};
  std::unique_ptr<juce::Component> pointsColumnHeader_;
  std::array<std::unique_ptr<IrReverbPointControlsRow>, IrReverbAudioProcessor::kNumPoints>
      pointRows_;

  std::unique_ptr<juce::FileChooser> fileChooser_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IrReverbAudioProcessorEditor)
};
