#pragma once

#include "JuceHeader.h"
#include "monitoring_cba_plugin_processor.hpp"
#include "monitoring_cba_scene_view.hpp"
#include "monitoring_cba_add_items_popup.hpp"
#include "components/ear_button.hpp"
#include <memory>
#include <vector>

/** @brief Monitoring CBA's editor, at parity with the algorithmic Reverb's
 * own "Inputs feeding..." + "+ Item" + Scene-style sphere left/right
 * split (see reverb_plugin_editor.hpp) - the output side is a single
 * "Monitoring Setup" panel instead of Reverb's own output-points list,
 * since this plugin's outputs are plain channels, not individually
 * positioned Objects.
 */
class MonitoringCbaAudioProcessorEditor : public juce::AudioProcessorEditor {
 public:
  explicit MonitoringCbaAudioProcessorEditor(MonitoringCbaAudioProcessor* p);
  ~MonitoringCbaAudioProcessorEditor() override;

  void paint(juce::Graphics&) override;
  void resized() override;

 private:
  void refreshActiveInputs();
  void openAddItemsPopup();
  void closeAddItemsPopup();
  void refreshLayoutCombo();
  void updateChannelNamesLabel();
  void promptLoadCustomLayout();
  void layoutContent();

  MonitoringCbaAudioProcessor* p_;

  juce::TooltipWindow tooltipWindow_{this, 500};

  juce::Viewport masterViewport_;
  juce::Component masterContent_;

  juce::Label titleLabel_;

  juce::Label activeHeading_;
  juce::TextButton addItemButton_{"+ Item"};
  juce::Viewport activeListViewport_;
  juce::Component activeListContent_;

  juce::Label sceneViewHeading_;
  std::unique_ptr<MonitoringCbaSceneView> sceneView_;

  struct ActiveRow {
    int slotIndex{-1};
    std::unique_ptr<juce::Component> stripe;
    std::unique_ptr<juce::Label> label;
    std::unique_ptr<juce::Label> positionLabel;
    std::unique_ptr<ear::plugin::ui::EarButton> removeButton;
  };
  std::vector<std::unique_ptr<ActiveRow>> activeRows_;

  std::unique_ptr<juce::Component> modalBackdrop_;
  std::unique_ptr<MonitoringCbaAddItemsPopup> addItemsPopup_;

  std::unique_ptr<juce::Component> setupPanel_;
  juce::Label setupHeading_{{}, "Monitoring Setup"};
  juce::Label layoutLabel_{{}, "Layout"};
  juce::ComboBox layoutCombo_;
  juce::TextButton loadCustomButton_{"Load custom layout..."};
  juce::Label channelNamesLabel_;

  std::unique_ptr<juce::FileChooser> fileChooser_;

  // --- Output OSC (collapsed by default) - reports each active input
  // slot's live position/state. No Input OSC section here - see the
  // processor header's own comment for why.
  juce::TextButton oscOutputHeaderButton_;
  bool oscOutputExpanded_{false};
  juce::ToggleButton oscOutputEnableToggle_{"Enabled"};
  juce::Label oscOutputIpLabel_{{}, "IP"};
  juce::TextEditor oscOutputIpEditor_;
  juce::Label oscOutputPortLabel_{{}, "Port"};
  juce::TextEditor oscOutputPortEditor_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MonitoringCbaAudioProcessorEditor)
};
