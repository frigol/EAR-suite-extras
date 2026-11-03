#pragma once

#include "JuceHeader.h"
#include "reverb_plugin_processor.hpp"
#include "components/ear_button.hpp"
#include <array>
#include <memory>
#include <vector>

class ReverbPointControlsRow;
class ReverbSceneView;
class ReverbTimeStructureView;
class ReverbAddItemsPopup;

/** @brief Plain functional UI, plus a ReverbSceneView position overview
 * styled like EAR Scene's own sphere view.
 *
 * Everything lives inside one scrollable masterContent_/masterViewport_
 * pair rather than a fixed-height window with several independent nested
 * viewports: a host that won't grow the plugin window to the full
 * requested size (or one that reopens at a previously-saved smaller size)
 * would otherwise clip whole sections - in particular the 9 output-point
 * rows at the bottom - with no way to reach them. With a single outer
 * viewport, every section is always reachable by scrolling, regardless
 * of what size the host actually gives the window.
 *
 * Input selection now matches EAR Scene's own layout (see plugins/scene/
 * src/programme_view.hpp's resized(): elementsContainer_ on the left half,
 * elementOverview_ on the right half, side by side at matching height) and
 * its own "+ Item" workflow: a left-hand card holding the "+ Item" button
 * and the active-inputs list (coloured strip, name, trash button, each row
 * the same height as Scene's own item rows), its own Viewport scrolling
 * independently if there are many, with the position overview occupying
 * the right half at the same height - rather than everything stacked in
 * one column.
 *
 * Each of the 9 output points gets a ReverbPointControlsRow exposing the
 * same parameters a real EAR Object has (position, gain, width/height/
 * depth, diffuse) in addition to its routing selector.
 */
class ReverbAudioProcessorEditor : public juce::AudioProcessorEditor {
 public:
  explicit ReverbAudioProcessorEditor(ReverbAudioProcessor* p);
  ~ReverbAudioProcessorEditor() override;

  void paint(juce::Graphics&) override;
  void resized() override;

 private:
  void refreshActiveInputs();
  void openAddItemsPopup();
  void closeAddItemsPopup();
  void updateRoomVolumeLabel();
  // Lays out every section inside masterContent_ and sets its total size,
  // so the outer viewport's scroll range always matches current content
  // (which changes as active-input rows are added/removed).
  void layoutContent();

  ReverbAudioProcessor* p_;

  // Makes every setTooltip() on a control below actually show on hover -
  // without this JUCE builds the tooltip text but never displays it.
  juce::TooltipWindow tooltipWindow_{this, 500};

  juce::Viewport masterViewport_;
  juce::Component masterContent_;

  juce::Label titleLabel_;

  // Left/right pair, matching EAR Scene's own ProgrammeView::resized()
  // (elementsContainer_ left half, elementOverview_ right half, both at
  // the same height): the active-inputs card on the left, the position
  // overview on the right.
  juce::Label activeHeading_;
  juce::TextButton addItemButton_{"+ Item"};
  juce::Viewport activeListViewport_;
  juce::Component activeListContent_;

  juce::Label sceneViewHeading_;
  std::unique_ptr<ReverbSceneView> sceneView_;

  // Bordered background behind dspHeading_..timeStructureView_ (added
  // first so it paints behind them), so the parameters read as one
  // distinct section, set apart from the input list above and the output
  // points below. Two titled sub-panels inside it (Mix / Decay &
  // Diffusion) group the controls further, so it's clear at a glance
  // what each one does.
  std::unique_ptr<juce::Component> parametersPanel_;
  juce::Label dspHeading_;
  juce::Label tailEngineLabel_{{}, "Tail engine"};
  juce::ComboBox tailEngineCombo_;

  // A single combo box listing saved parameter presets (Mix/Decay/
  // Diffusion only, not per-point routing/position - see
  // ReverbAudioProcessor::savePresetAs()) - deliberately one dropdown for
  // the whole preset, not one control per parameter.
  juce::Label presetLabel_{{}, "Preset"};
  juce::ComboBox presetCombo_;
  juce::TextButton savePresetButton_{"Save As..."};
  juce::TextButton deletePresetButton_{"Delete"};
  void refreshPresetCombo(const juce::String& selectName = {});
  void refreshDspControlsFromProcessor();

  std::unique_ptr<juce::Component> mixPanel_;
  juce::Label mixHeading_{{}, "Mix"};
  // One row per signal path reaching the 9 outputs, stacked Direct/Early/
  // Diffuse top to bottom, each an on/off toggle beside its own level.
  juce::ToggleButton directEnabledToggle_{"Direct"};
  juce::Slider directLevelSlider_;
  juce::ToggleButton earlyEnabledToggle_{"Early Reflections"};
  juce::Slider earlyLevelSlider_;
  juce::ToggleButton diffuseEnabledToggle_{"Diffuse Tail"};
  juce::Slider diffuseLevelSlider_;

  std::unique_ptr<juce::Component> decayPanel_;
  juce::Label decayHeading_{{}, "Decay & Diffusion"};
  juce::Label decayLabel_{{}, "Decay (s)"};
  juce::Slider decaySlider_;
  juce::Label dampingLabel_{{}, "Damping (0-1)"};
  juce::Slider dampingSlider_;
  juce::Label roomSizeLabel_{{}, "Room size (x)"};
  juce::Slider roomSizeSlider_;
  juce::Label roomVolumeLabel_;  // derived "~ NNN m3" readout next to Room size
  juce::Label densityLabel_{{}, "Density (0-1)"};
  juce::Slider densitySlider_;
  juce::Label preDelayLabel_{{}, "Pre-delay (ms)"};
  juce::Slider preDelaySlider_;
  juce::Label bandwidthLabel_{{}, "Bandwidth (0-1)"};
  juce::Slider bandwidthSlider_;

  juce::Label timeStructureHeading_;
  std::unique_ptr<ReverbTimeStructureView> timeStructureView_;

  struct ActiveRow {
    int slotIndex{-1};
    std::unique_ptr<juce::Component> stripe;
    std::unique_ptr<juce::Label> label;
    std::unique_ptr<ear::plugin::ui::EarButton> removeButton;
  };
  std::vector<std::unique_ptr<ActiveRow>> activeRows_;

  // The "+ Item" popup and its dimming backdrop - both children of this
  // editor directly (not masterContent_), so they float above everything
  // at a fixed position regardless of the master viewport's scroll
  // position, and are only created while actually open.
  std::unique_ptr<juce::Component> modalBackdrop_;
  std::unique_ptr<ReverbAddItemsPopup> addItemsPopup_;

  juce::Label pointsHeading_;
  // Single global toggle for all 10 point rows at once, so the whole
  // section can be collapsed in one click instead of each row's own
  // collapse arrow individually.
  juce::TextButton collapseAllButton_;
  bool allExpanded_{false};
  // One aligned header row of column titles (Az/El/Dist/.../Reset), shown
  // once above all 10 point rows, instead of repeating a short label next
  // to every single slider on every row - with many rows those repeated
  // labels read as equidistant/ambiguous about which value they belong
  // to; one header aligned to the same column positions each row uses
  // does not.
  std::unique_ptr<juce::Component> pointsColumnHeader_;
  std::array<std::unique_ptr<ReverbPointControlsRow>,
            ReverbAudioProcessor::kNumPoints>
      pointRows_;

  // Assigns all kNumPoints points' routing at once to a contiguous block
  // of MAX_DAW_CHANNELS, e.g. "45-54" routes point 0 to channel 45, point
  // 1 to 46, ... point 9 to 54 - instead of setting all 10 routing combos
  // one at a time. Each point can still be overridden individually
  // afterwards via its own row. Typing the block's start channel number
  // jumps straight to it, same as each row's own routingJumpField_.
  juce::Label bulkRoutingLabel_{{}, "Assign output channels"};
  juce::ComboBox bulkRoutingCombo_;
  juce::TextEditor bulkRoutingJumpField_;

  // --- Output/Input OSC (both collapsed by default) - see Point
  // Encoder's identical sections (point_encoder_plugin_editor.hpp) for
  // the full rationale; collapse state here is local editor state, not
  // persisted on the processor.
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

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbAudioProcessorEditor)
};
