#include "reverb_plugin_editor.hpp"
#include "reverb_add_items_popup.hpp"
#include "reverb_point_controls_row.hpp"
#include "reverb_scene_view.hpp"
#include "reverb_time_structure_view.hpp"
#include "binary_data.hpp"
#include "components/look_and_feel/colours.hpp"
#include "components/look_and_feel/fonts.hpp"
#include <daw_channel_count.h>
#include <algorithm>
#include <cmath>

using namespace ear::plugin::ui;

namespace {
constexpr int kRowHeight = 24;
constexpr int kActiveRowHeight = 46;  // matches EAR Scene's own ItemView row height
constexpr int kDspSubRowHeight = 38;
constexpr int kSceneViewHeight = 260;
constexpr int kTimeStructureHeight = 150;
constexpr int kPopupWidth = 700;
constexpr int kPopupHeight = 440;

// A thin colour-filled strip, for the per-item colour indicator on each
// active-input row - the same "coloured bar marks this item" grammar EAR
// Scene uses for its own item rows.
class ColourStripe : public juce::Component {
 public:
  juce::Colour colour;
  void paint(juce::Graphics& g) override { g.fillAll(colour); }
};

// A bordered card background, placed behind a whole section so it reads
// as one distinct group - e.g. the DSP/time-structure "Parameters"
// section, set apart from the input list and the output points above and
// below it.
class SectionPanel : public juce::Component {
 public:
  void paint(juce::Graphics& g) override {
    g.setColour(EarColours::Area01dp);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    g.setColour(EarColours::Sphere);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f, 1.0f);
  }
};

void setupDspSlider(juce::Slider& s, double min, double max, double step,
                    double initial) {
  s.setSliderStyle(juce::Slider::LinearHorizontal);
  s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
  s.setScrollWheelEnabled(false);
  s.setRange(min, max, step);
  s.setValue(initial, juce::dontSendNotification);
}

std::unique_ptr<EarButton> makeIconButton(const char* onOffSvgData,
                                          int svgDataSize) {
  auto button = std::make_unique<EarButton>();
  button->setOffStateIcon(std::unique_ptr<juce::Drawable>(
      juce::Drawable::createFromImageData(onOffSvgData, static_cast<size_t>(svgDataSize))));
  button->setOnStateIcon(std::unique_ptr<juce::Drawable>(
      juce::Drawable::createFromImageData(onOffSvgData, static_cast<size_t>(svgDataSize))));
  return button;
}

// Centred, but never larger than the editor itself (if the host has made
// the window smaller than the popup's natural size).
juce::Rectangle<int> popupBounds(juce::Rectangle<int> editorBounds) {
  int w = juce::jmin(kPopupWidth, editorBounds.getWidth() - 20);
  int h = juce::jmin(kPopupHeight, editorBounds.getHeight() - 20);
  return juce::Rectangle<int>(0, 0, w, h).withCentre(editorBounds.getCentre());
}

// The dimming backdrop behind the "+ Item" popup - click anywhere on it to
// dismiss, same as clicking outside EAR Scene's own popup.
class ModalBackdrop : public juce::Component {
 public:
  std::function<void()> onClick;
  void paint(juce::Graphics& g) override {
    g.fillAll(juce::Colours::black.withAlpha(0.55f));
  }
  void mouseDown(const juce::MouseEvent&) override {
    if (onClick) onClick();
  }
};

// One column-header row for the output-points list, aligned to exactly
// the same x-offsets ReverbPointControlsRow uses for its own sliders -
// shown once above all 10 rows instead of repeating an equidistant,
// easy-to-misread short label on every single row.
class PointsColumnHeader : public juce::Component {
 public:
  void paint(juce::Graphics& g) override {
    g.setColour(EarColours::Label);
    g.setFont(EarFontsSingleton::instance().Description);
    auto area = getLocalBounds();
    // ReverbPointControlsRow::resized()'s slider row (the row's own
    // bottom half, below Active/name/routing) starts at x=0, so the
    // header does too.
    for (auto const& title :
        {"Az", "El", "Dist", "Gain", "W", "H", "D", "Diff"}) {
      g.drawText(title, area.removeFromLeft(ReverbPointControlsRow::kColumnWidth),
                juce::Justification::centred);
      area.removeFromLeft(ReverbPointControlsRow::kColumnGap);
    }
  }
};
}  // namespace

ReverbAudioProcessorEditor::ReverbAudioProcessorEditor(ReverbAudioProcessor* p)
    : AudioProcessorEditor(p), p_(p) {
  masterViewport_.setViewedComponent(&masterContent_, false);
  masterViewport_.setScrollBarsShown(true, false);
  addAndMakeVisible(masterViewport_);

  titleLabel_.setText(
      "LF Reverb for EAR Production Suite (direction-weighted early "
      "reflections + shared diffuse tail)",
      juce::dontSendNotification);
  titleLabel_.setFont(juce::Font(16.0f, juce::Font::bold));
  masterContent_.addAndMakeVisible(titleLabel_);

  // Left/right pair, matching EAR Scene's own ProgrammeView layout.
  activeHeading_.setText("Inputs feeding the reverb", juce::dontSendNotification);
  masterContent_.addAndMakeVisible(activeHeading_);
  addItemButton_.onClick = [this] { openAddItemsPopup(); };
  masterContent_.addAndMakeVisible(addItemButton_);
  activeListViewport_.setViewedComponent(&activeListContent_, false);
  activeListViewport_.setScrollBarsShown(true, false);
  masterContent_.addAndMakeVisible(activeListViewport_);

  sceneViewHeading_.setText(
      "Position overview (EAR Scene-style) - points in orange/grey, "
      "active sources in their Scene colour",
      juce::dontSendNotification);
  masterContent_.addAndMakeVisible(sceneViewHeading_);

  sceneView_ = std::make_unique<ReverbSceneView>(p_);
  masterContent_.addAndMakeVisible(*sceneView_);

  parametersPanel_ = std::make_unique<SectionPanel>();
  masterContent_.addAndMakeVisible(*parametersPanel_);
  dspHeading_.setText("Reverb DSP", juce::dontSendNotification);
  masterContent_.addAndMakeVisible(dspHeading_);

  auto dsp = p_->getDspParams();

  tailEngineLabel_.setTooltip(
      "Which algorithm generates the diffuse tail - swap freely to A/B "
      "compare; the other engine stays prepared so switching is instant.");
  masterContent_.addAndMakeVisible(tailEngineLabel_);
  tailEngineCombo_.addItem("Custom FDN", 1);
  tailEngineCombo_.addItem("MVerb Plate", 2);
  tailEngineCombo_.setSelectedId(
      dsp.tailEngine == ear::plugin::reverbdsp::TailEngineType::MVerbPlate ? 2 : 1,
      juce::dontSendNotification);
  tailEngineCombo_.onChange = [this] {
    p_->setTailEngine(tailEngineCombo_.getSelectedId() == 2
                          ? ear::plugin::reverbdsp::TailEngineType::MVerbPlate
                          : ear::plugin::reverbdsp::TailEngineType::CustomFdn);
  };
  masterContent_.addAndMakeVisible(tailEngineCombo_);

  masterContent_.addAndMakeVisible(presetLabel_);
  presetCombo_.setTextWhenNothingSelected("(no preset)");
  presetCombo_.onChange = [this] {
    auto name = presetCombo_.getText();
    if (name.isNotEmpty() && p_->loadPreset(name)) {
      refreshDspControlsFromProcessor();
    }
  };
  masterContent_.addAndMakeVisible(presetCombo_);
  savePresetButton_.onClick = [this] {
    auto* aw = new juce::AlertWindow("Save Preset", "Preset name:",
                                     juce::AlertWindow::NoIcon);
    aw->addTextEditor("name", presetCombo_.getText());
    aw->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    aw->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    aw->enterModalState(
        true,
        juce::ModalCallbackFunction::create([this, aw](int result) {
          if (result == 1) {
            auto name = aw->getTextEditorContents("name").trim();
            if (name.isNotEmpty()) {
              p_->savePresetAs(name);
              refreshPresetCombo(name);
            }
          }
        }),
        true);
  };
  masterContent_.addAndMakeVisible(savePresetButton_);
  deletePresetButton_.onClick = [this] {
    auto name = presetCombo_.getText();
    if (name.isNotEmpty()) {
      p_->deletePreset(name);
      refreshPresetCombo();
    }
  };
  masterContent_.addAndMakeVisible(deletePresetButton_);
  refreshPresetCombo();

  // --- "Mix" sub-panel: Direct / Early / Diffuse, each a toggle + its
  // own level, stacked in one column so the 3 signal paths that reach
  // the 9 outputs read as one clearly-related group.
  mixPanel_ = std::make_unique<SectionPanel>();
  masterContent_.addAndMakeVisible(*mixPanel_);
  mixHeading_.setFont(juce::Font(13.0f, juce::Font::bold));
  masterContent_.addAndMakeVisible(mixHeading_);

  directEnabledToggle_.setToggleState(dsp.directEnabled, juce::dontSendNotification);
  directEnabledToggle_.setTooltip(
      "Pass the unprocessed dry signal through to the 9 outputs too. Off by "
      "default: the dry object is already sent to Scene on its own channel, "
      "so this is only for auditioning/special routing.");
  directEnabledToggle_.onClick = [this] {
    p_->setDirectEnabled(directEnabledToggle_.getToggleState());
    if (timeStructureView_) timeStructureView_->refresh();
  };
  masterContent_.addAndMakeVisible(directEnabledToggle_);
  setupDspSlider(directLevelSlider_, 0.0, 2.0, 0.01, dsp.directLevel);
  directLevelSlider_.setDoubleClickReturnValue(true, 1.0);
  directLevelSlider_.setTooltip(
      "Linear output level multiplier for the unprocessed dry signal (1 = unity). "
      "Double-click to reset.");
  directLevelSlider_.onValueChange = [this] {
    p_->setDirectLevel(static_cast<float>(directLevelSlider_.getValue()));
    if (timeStructureView_) timeStructureView_->refresh();
  };
  masterContent_.addAndMakeVisible(directLevelSlider_);

  earlyEnabledToggle_.setToggleState(dsp.earlyEnabled, juce::dontSendNotification);
  earlyEnabledToggle_.setTooltip("Include the early reflections in the output.");
  earlyEnabledToggle_.onClick = [this] {
    p_->setEarlyEnabled(earlyEnabledToggle_.getToggleState());
    if (timeStructureView_) timeStructureView_->refresh();
  };
  masterContent_.addAndMakeVisible(earlyEnabledToggle_);
  setupDspSlider(earlyLevelSlider_, 0.0, 2.0, 0.01, dsp.earlyLevel);
  earlyLevelSlider_.setDoubleClickReturnValue(true, 1.0);
  earlyLevelSlider_.setTooltip(
      "Linear output level multiplier for the early reflections (1 = unity). "
      "Double-click to reset.");
  earlyLevelSlider_.onValueChange = [this] {
    p_->setEarlyLevel(static_cast<float>(earlyLevelSlider_.getValue()));
    if (timeStructureView_) timeStructureView_->refresh();
  };
  masterContent_.addAndMakeVisible(earlyLevelSlider_);

  diffuseEnabledToggle_.setToggleState(dsp.diffuseEnabled, juce::dontSendNotification);
  diffuseEnabledToggle_.setTooltip("Include the diffuse tail in the output.");
  diffuseEnabledToggle_.onClick = [this] {
    p_->setDiffuseEnabled(diffuseEnabledToggle_.getToggleState());
    if (timeStructureView_) timeStructureView_->refresh();
  };
  masterContent_.addAndMakeVisible(diffuseEnabledToggle_);
  setupDspSlider(diffuseLevelSlider_, 0.0, 2.0, 0.01, dsp.diffuseLevel);
  diffuseLevelSlider_.setDoubleClickReturnValue(true, 0.6);
  diffuseLevelSlider_.setTooltip(
      "Linear output level multiplier for the diffuse tail (1 = unity). "
      "Double-click to reset.");
  diffuseLevelSlider_.onValueChange = [this] {
    p_->setDiffuseLevel(static_cast<float>(diffuseLevelSlider_.getValue()));
    if (timeStructureView_) timeStructureView_->refresh();
  };
  masterContent_.addAndMakeVisible(diffuseLevelSlider_);

  // --- "Decay & Diffusion" sub-panel: everything shaping the tail
  // itself, separate from whether/how loud each path is.
  decayPanel_ = std::make_unique<SectionPanel>();
  masterContent_.addAndMakeVisible(*decayPanel_);
  decayHeading_.setFont(juce::Font(13.0f, juce::Font::bold));
  masterContent_.addAndMakeVisible(decayHeading_);

  setupDspSlider(decaySlider_, 0.2, 10.0, 0.1, dsp.decaySeconds);
  decaySlider_.setDoubleClickReturnValue(true, 1.8);
  decaySlider_.setTooltip(
      "RT60: time for the diffuse tail to decay by 60dB, in seconds. "
      "Double-click to reset.");
  decaySlider_.onValueChange = [this] {
    p_->setDecaySeconds(static_cast<float>(decaySlider_.getValue()));
    if (timeStructureView_) timeStructureView_->refresh();
  };
  setupDspSlider(dampingSlider_, 0.0, 0.95, 0.01, dsp.damping);
  dampingSlider_.setDoubleClickReturnValue(true, 0.3);
  dampingSlider_.setTooltip(
      "High-frequency absorption of the tail, 0 = none, 1 = fully damped. "
      "Double-click to reset.");
  dampingSlider_.onValueChange = [this] {
    p_->setDamping(static_cast<float>(dampingSlider_.getValue()));
  };
  // Shown/edited as a whole-number percentage (50%-200%) rather than a
  // 0.5-2.0 decimal multiplier - plainer, and matches "more approximate,
  // no decimals" for this control; converted to the 0.5..2.0 multiplier
  // the processor actually wants when applied.
  setupDspSlider(roomSizeSlider_, 50.0, 200.0, 1.0, dsp.roomSize * 100.0);
  roomSizeSlider_.setTextValueSuffix("%");
  roomSizeSlider_.setNumDecimalPlacesToDisplay(0);
  roomSizeSlider_.setTooltip("Scales the tail's internal echo spacing (100% = design size).");
  roomSizeSlider_.setDoubleClickReturnValue(true, 100.0);
  roomVolumeLabel_.setTooltip(
      "Rough estimated room volume for the current Room Size, from the "
      "reverb's own internal delay times at the speed of sound - an "
      "intuitive reference, not a precise acoustic measurement.");
  masterContent_.addAndMakeVisible(roomVolumeLabel_);
  roomSizeSlider_.onValueChange = [this] {
    p_->setRoomSize(static_cast<float>(roomSizeSlider_.getValue()) / 100.0f);
    if (timeStructureView_) timeStructureView_->refresh();
    updateRoomVolumeLabel();
  };
  updateRoomVolumeLabel();

  setupDspSlider(densitySlider_, 0.0, 1.0, 0.01, dsp.density);
  densitySlider_.setDoubleClickReturnValue(true, 1.0);
  densitySlider_.setTooltip(
      "0 = sparse/comb-like tail, 1 = fully dense/smooth tail. Double-click to reset.");
  densitySlider_.onValueChange = [this] {
    p_->setDensity(static_cast<float>(densitySlider_.getValue()));
    if (timeStructureView_) timeStructureView_->refresh();
  };
  setupDspSlider(preDelaySlider_, 0.0, 200.0, 1.0, dsp.preDelayMs);
  preDelaySlider_.setDoubleClickReturnValue(true, 0.0);
  preDelaySlider_.setTooltip(
      "Gap, in milliseconds, before reflections/tail start. Double-click to reset.");
  preDelaySlider_.onValueChange = [this] {
    p_->setPreDelayMs(static_cast<float>(preDelaySlider_.getValue()));
    if (timeStructureView_) timeStructureView_->refresh();
  };
  setupDspSlider(bandwidthSlider_, 0.0, 1.0, 0.01, dsp.bandwidth);
  bandwidthSlider_.setDoubleClickReturnValue(true, 1.0);
  bandwidthSlider_.setTooltip(
      "Brightness of the signal feeding the tail: 0 = dark/muffled, 1 = full bandwidth. "
      "Double-click to reset.");
  bandwidthSlider_.onValueChange = [this] {
    p_->setBandwidth(static_cast<float>(bandwidthSlider_.getValue()));
  };

  timeStructureHeading_.setText("Time structure (pre-delay / early / diffuse tail)",
                                juce::dontSendNotification);
  masterContent_.addAndMakeVisible(timeStructureHeading_);
  timeStructureView_ = std::make_unique<ReverbTimeStructureView>(p_);
  masterContent_.addAndMakeVisible(*timeStructureView_);

  for (auto* label : {&decayLabel_, &dampingLabel_, &roomSizeLabel_,
                      &densityLabel_, &preDelayLabel_, &bandwidthLabel_}) {
    label->setJustificationType(juce::Justification::centred);
    masterContent_.addAndMakeVisible(*label);
  }
  for (auto* slider : {&decaySlider_, &dampingSlider_, &roomSizeSlider_,
                       &densitySlider_, &preDelaySlider_, &bandwidthSlider_})
    masterContent_.addAndMakeVisible(*slider);

  pointsHeading_.setText(
      "Output points (Object-style parameters, like a real EAR Object - "
      "turn Active off to free this point's Scene slot and route it "
      "through a Point Encoder instance instead)",
      juce::dontSendNotification);
  masterContent_.addAndMakeVisible(pointsHeading_);

  // Read from the processor (not just defaulted here), so the expanded/
  // collapsed state survives the editor itself being closed and reopened
  // - only a freshly created processor starts collapsed.
  allExpanded_ = p_->getPointsExpanded();
  collapseAllButton_.setButtonText(allExpanded_ ? "Collapse all" : "Expand all");
  collapseAllButton_.onClick = [this] {
    allExpanded_ = !allExpanded_;
    p_->setPointsExpanded(allExpanded_);
    collapseAllButton_.setButtonText(allExpanded_ ? "Collapse all" : "Expand all");
    layoutContent();
  };
  masterContent_.addAndMakeVisible(collapseAllButton_);

  pointsColumnHeader_ = std::make_unique<PointsColumnHeader>();
  masterContent_.addAndMakeVisible(*pointsColumnHeader_);

  for (int i = 0; i < ReverbAudioProcessor::kNumPoints; ++i) {
    auto row = std::make_unique<ReverbPointControlsRow>(p_, i);
    row->onChanged = [this] { if (sceneView_) sceneView_->refresh(); };
    masterContent_.addAndMakeVisible(*row);
    pointRows_[static_cast<size_t>(i)] = std::move(row);
  }

  masterContent_.addAndMakeVisible(bulkRoutingLabel_);
  {
    constexpr int kNumPoints = ReverbAudioProcessor::kNumPoints;
    constexpr int kNumRanges = MAX_DAW_CHANNELS - kNumPoints + 1;
    for (int start = 1; start <= kNumRanges; ++start)
      bulkRoutingCombo_.addItem(
          juce::String(start) + "-" + juce::String(start + kNumPoints - 1), start);
  }
  bulkRoutingCombo_.setTextWhenNothingSelected("(choose a range...)");
  bulkRoutingCombo_.onChange = [this] {
    int start = bulkRoutingCombo_.getSelectedId();
    if (start <= 0) return;
    for (int i = 0; i < ReverbAudioProcessor::kNumPoints; ++i)
      p_->setPointRouting(i, (start - 1) + i);
    for (auto const& row : pointRows_) row->refreshFromProcessor();
    if (sceneView_) sceneView_->refresh();
  };
  masterContent_.addAndMakeVisible(bulkRoutingCombo_);

  bulkRoutingJumpField_.setInputRestrictions(3, "0123456789");
  bulkRoutingJumpField_.setTextToShowWhenEmpty("#", juce::Colours::grey);
  bulkRoutingJumpField_.setJustification(juce::Justification::centred);
  bulkRoutingJumpField_.setTooltip(
      "Type the range's start channel number and press Enter to jump "
      "straight to it.");
  auto jumpBulkRouting = [this] {
    if (bulkRoutingJumpField_.getText().isEmpty()) return;
    int n = bulkRoutingJumpField_.getText().getIntValue();
    constexpr int kNumRanges =
        MAX_DAW_CHANNELS - ReverbAudioProcessor::kNumPoints + 1;
    if (n >= 1 && n <= kNumRanges)
      bulkRoutingCombo_.setSelectedId(n, juce::sendNotification);
    bulkRoutingJumpField_.clear();
  };
  bulkRoutingJumpField_.onReturnKey = jumpBulkRouting;
  bulkRoutingJumpField_.onFocusLost = jumpBulkRouting;
  masterContent_.addAndMakeVisible(bulkRoutingJumpField_);

  p_->onAvailableObjectsChanged = [this] {
    refreshActiveInputs();
    if (sceneView_) sceneView_->refresh();
    if (addItemsPopup_) addItemsPopup_->refresh();
  };
  refreshActiveInputs();

  p_->onTrackNameChanged = [this] {
    for (auto const& row : pointRows_) row->refreshName();
    if (sceneView_) sceneView_->refresh();
  };

  // --- Output OSC (collapsed by default) ------------------------------
  oscOutputHeaderButton_.setButtonText(
      oscOutputExpanded_ ? "v Output OSC" : "> Output OSC");
  oscOutputHeaderButton_.onClick = [this] {
    oscOutputExpanded_ = !oscOutputExpanded_;
    oscOutputHeaderButton_.setButtonText(
        oscOutputExpanded_ ? "v Output OSC" : "> Output OSC");
    layoutContent();
  };
  masterContent_.addAndMakeVisible(oscOutputHeaderButton_);

  oscOutputEnableToggle_.setToggleState(p_->getOscOutputEnabled(),
                                        juce::dontSendNotification);
  oscOutputEnableToggle_.onClick = [this] {
    p_->setOscOutputEnabled(oscOutputEnableToggle_.getToggleState());
  };
  masterContent_.addAndMakeVisible(oscOutputEnableToggle_);

  masterContent_.addAndMakeVisible(oscOutputIpLabel_);
  oscOutputIpEditor_.setText(p_->getOscOutputIp(), juce::dontSendNotification);
  oscOutputIpEditor_.onFocusLost = [this] {
    p_->setOscOutputIp(oscOutputIpEditor_.getText());
  };
  oscOutputIpEditor_.onReturnKey = [this] {
    p_->setOscOutputIp(oscOutputIpEditor_.getText());
  };
  masterContent_.addAndMakeVisible(oscOutputIpEditor_);

  masterContent_.addAndMakeVisible(oscOutputPortLabel_);
  oscOutputPortEditor_.setInputRestrictions(5, "0123456789");
  oscOutputPortEditor_.setText(juce::String(p_->getOscOutputPort()),
                               juce::dontSendNotification);
  oscOutputPortEditor_.onFocusLost = [this] {
    p_->setOscOutputPort(oscOutputPortEditor_.getText().getIntValue());
  };
  oscOutputPortEditor_.onReturnKey = [this] {
    p_->setOscOutputPort(oscOutputPortEditor_.getText().getIntValue());
  };
  masterContent_.addAndMakeVisible(oscOutputPortEditor_);

  // --- Input OSC (collapsed by default) --------------------------------
  oscInputHeaderButton_.setButtonText(
      oscInputExpanded_ ? "v Input OSC" : "> Input OSC");
  oscInputHeaderButton_.onClick = [this] {
    oscInputExpanded_ = !oscInputExpanded_;
    oscInputHeaderButton_.setButtonText(
        oscInputExpanded_ ? "v Input OSC" : "> Input OSC");
    layoutContent();
  };
  masterContent_.addAndMakeVisible(oscInputHeaderButton_);

  oscInputEnableToggle_.setToggleState(p_->getOscInputEnabled(),
                                       juce::dontSendNotification);
  oscInputEnableToggle_.onClick = [this] {
    p_->setOscInputEnabled(oscInputEnableToggle_.getToggleState());
  };
  masterContent_.addAndMakeVisible(oscInputEnableToggle_);

  masterContent_.addAndMakeVisible(oscInputPortLabel_);
  oscInputPortEditor_.setInputRestrictions(5, "0123456789");
  oscInputPortEditor_.setText(juce::String(p_->getOscInputPort()),
                              juce::dontSendNotification);
  oscInputPortEditor_.onFocusLost = [this] {
    p_->setOscInputPort(oscInputPortEditor_.getText().getIntValue());
  };
  oscInputPortEditor_.onReturnKey = [this] {
    p_->setOscInputPort(oscInputPortEditor_.getText().getIntValue());
  };
  masterContent_.addAndMakeVisible(oscInputPortEditor_);

  setResizable(true, true);
  setSize(1000, 870);
}

ReverbAudioProcessorEditor::~ReverbAudioProcessorEditor() {
  p_->onAvailableObjectsChanged = nullptr;
  p_->onTrackNameChanged = nullptr;
}

void ReverbAudioProcessorEditor::refreshActiveInputs() {
  activeRows_.clear();

  auto activeSlots = p_->getActiveInputSlots();
  for (auto const& slot : activeSlots) {
    auto row = std::make_unique<ActiveRow>();
    row->slotIndex = slot.slotIndex;

    auto stripe = std::make_unique<ColourStripe>();
    stripe->colour = juce::Colour(slot.colour);
    activeListContent_.addAndMakeVisible(*stripe);
    row->stripe = std::move(stripe);

    row->label = std::make_unique<juce::Label>();
    juce::String displayName = slot.isLive
                                   ? (slot.name.isEmpty() ? juce::String("(unnamed)")
                                                          : slot.name)
                                   : juce::String("(disconnected - remove & re-add)");
    row->label->setText(
        displayName + "  [ch " + juce::String(slot.slotIndex + 1) + "]",
        juce::dontSendNotification);
    row->label->setColour(juce::Label::backgroundColourId,
                          EarColours::ObjectItemBackground);
    row->label->setColour(
        juce::Label::textColourId,
        slot.isLive ? EarColours::Text.withAlpha(Emphasis::high)
                   : juce::Colours::orange);
    row->label->setFont(EarFontsSingleton::instance().Items);
    activeListContent_.addAndMakeVisible(*row->label);

    row->removeButton =
        makeIconButton(binary_data::delete_icon_svg,
                      static_cast<int>(binary_data::delete_icon_svgSize));
    int slotIndex = slot.slotIndex;
    row->removeButton->onClick = [this, slotIndex] {
      p_->setInputSlotSource(slotIndex, "");
      refreshActiveInputs();
      if (addItemsPopup_) addItemsPopup_->refresh();
    };
    activeListContent_.addAndMakeVisible(*row->removeButton);

    activeRows_.push_back(std::move(row));
  }

  layoutContent();
}

void ReverbAudioProcessorEditor::updateRoomVolumeLabel() {
  // Rough physical estimate, for an intuitive sense of scale rather than
  // an exact acoustic model: the FDN's own average internal delay time
  // at the current Room Size, read as a one-way path length at the
  // speed of sound and treated as a cube's side. Rounded to the nearest
  // 10 m3 - this is a ballpark figure, not a precise reading.
  constexpr float kAvgBaseDelaySeconds = 1945.75f / 48000.0f;  // see kBaseDelayLengths
  constexpr float kSpeedOfSoundMps = 343.0f;
  float roomSize = static_cast<float>(roomSizeSlider_.getValue()) / 100.0f;
  float pathMetres = kAvgBaseDelaySeconds * kSpeedOfSoundMps * roomSize;
  float volumeM3 = pathMetres * pathMetres * pathMetres;
  float roundedM3 = std::round(volumeM3 / 10.0f) * 10.0f;
  roomVolumeLabel_.setText(
      juce::String::fromUTF8("\xe2\x89\x88 ") + juce::String(roundedM3, 0) + " m" +
          juce::String::fromUTF8("\xc2\xb3"),
      juce::dontSendNotification);
}

void ReverbAudioProcessorEditor::refreshPresetCombo(
    const juce::String& selectName) {
  presetCombo_.clear(juce::dontSendNotification);
  auto names = p_->getAvailablePresetNames();
  int id = 1;
  int selectedId = 0;
  for (auto const& name : names) {
    presetCombo_.addItem(name, id);
    if (name == selectName) selectedId = id;
    ++id;
  }
  presetCombo_.setSelectedId(selectedId, juce::dontSendNotification);
}

void ReverbAudioProcessorEditor::refreshDspControlsFromProcessor() {
  auto dsp = p_->getDspParams();
  decaySlider_.setValue(dsp.decaySeconds, juce::dontSendNotification);
  dampingSlider_.setValue(dsp.damping, juce::dontSendNotification);
  roomSizeSlider_.setValue(dsp.roomSize * 100.0, juce::dontSendNotification);
  densitySlider_.setValue(dsp.density, juce::dontSendNotification);
  preDelaySlider_.setValue(dsp.preDelayMs, juce::dontSendNotification);
  bandwidthSlider_.setValue(dsp.bandwidth, juce::dontSendNotification);
  directEnabledToggle_.setToggleState(dsp.directEnabled,
                                      juce::dontSendNotification);
  directLevelSlider_.setValue(dsp.directLevel, juce::dontSendNotification);
  earlyEnabledToggle_.setToggleState(dsp.earlyEnabled,
                                     juce::dontSendNotification);
  earlyLevelSlider_.setValue(dsp.earlyLevel, juce::dontSendNotification);
  diffuseEnabledToggle_.setToggleState(dsp.diffuseEnabled,
                                       juce::dontSendNotification);
  diffuseLevelSlider_.setValue(dsp.diffuseLevel, juce::dontSendNotification);
  tailEngineCombo_.setSelectedId(
      dsp.tailEngine == ear::plugin::reverbdsp::TailEngineType::MVerbPlate ? 2
                                                                            : 1,
      juce::dontSendNotification);
  updateRoomVolumeLabel();
  if (timeStructureView_) timeStructureView_->refresh();
}

void ReverbAudioProcessorEditor::openAddItemsPopup() {
  if (addItemsPopup_) return;

  auto backdrop = std::make_unique<ModalBackdrop>();
  backdrop->onClick = [this] { closeAddItemsPopup(); };
  backdrop->setBounds(getLocalBounds());
  addAndMakeVisible(*backdrop);
  modalBackdrop_ = std::move(backdrop);

  auto popup = std::make_unique<ReverbAddItemsPopup>(p_);
  popup->onClose = [this] {
    refreshActiveInputs();
    closeAddItemsPopup();
  };
  popup->setBounds(popupBounds(getLocalBounds()));
  addAndMakeVisible(*popup);
  addItemsPopup_ = std::move(popup);
}

void ReverbAudioProcessorEditor::closeAddItemsPopup() {
  addItemsPopup_.reset();
  modalBackdrop_.reset();
}

void ReverbAudioProcessorEditor::paint(juce::Graphics& g) {
  g.fillAll(EarColours::Background);
}

void ReverbAudioProcessorEditor::resized() {
  masterViewport_.setBounds(getLocalBounds());

  if (modalBackdrop_) modalBackdrop_->setBounds(getLocalBounds());
  if (addItemsPopup_) addItemsPopup_->setBounds(popupBounds(getLocalBounds()));

  layoutContent();
}

void ReverbAudioProcessorEditor::layoutContent() {
  int contentWidth = masterViewport_.getWidth() > 0
                         ? masterViewport_.getWidth() -
                               masterViewport_.getScrollBarThickness()
                         : 980;
  contentWidth = juce::jmax(contentWidth, 820);

  auto area = juce::Rectangle<int>(0, 0, contentWidth, 100000).reduced(10, 0);
  int y = 10;
  auto takeTop = [&](int h) {
    auto r = juce::Rectangle<int>(area.getX(), y, area.getWidth(), h);
    y += h;
    return r;
  };

  titleLabel_.setBounds(takeTop(28));
  y += 6;

  // Left/right pair at matching height, like EAR Scene's own
  // elementsContainer_/elementOverview_ split (programme_view.hpp).
  {
    int splitWidth = area.getWidth();
    int leftWidth = splitWidth / 2 - 5;
    int rightWidth = splitWidth - leftWidth - 10;
    int leftX = area.getX();
    int rightX = area.getX() + leftWidth + 10;
    int topY = y;

    auto leftHeader = juce::Rectangle<int>(leftX, topY, leftWidth, kRowHeight);
    addItemButton_.setBounds(leftHeader.removeFromRight(70));
    activeHeading_.setBounds(leftHeader);
    activeListViewport_.setBounds(leftX, topY + kRowHeight + 4, leftWidth,
                                  kSceneViewHeight - kRowHeight - 4);

    sceneViewHeading_.setBounds(rightX, topY, rightWidth, kRowHeight);
    sceneView_->setBounds(rightX, topY + kRowHeight + 4, rightWidth,
                          kSceneViewHeight - kRowHeight - 4);

    y = topY + kSceneViewHeight;
  }
  y += 10;

  int parametersPanelTop = y - 4;
  y += 8;  // inner top padding, inside the panel

  {
    auto row = takeTop(kRowHeight);
    dspHeading_.setBounds(row.removeFromLeft(row.getWidth() - 260));
    tailEngineCombo_.setBounds(row.removeFromRight(160));
    row.removeFromRight(8);
    tailEngineLabel_.setBounds(row.removeFromRight(84));
  }

  {
    auto row = takeTop(kRowHeight);
    presetLabel_.setBounds(row.removeFromLeft(50));
    row.removeFromLeft(4);
    deletePresetButton_.setBounds(row.removeFromRight(70));
    row.removeFromRight(8);
    savePresetButton_.setBounds(row.removeFromRight(90));
    row.removeFromRight(8);
    presetCombo_.setBounds(row);
  }

  // Mix (left) / Decay & Diffusion (right) sub-panels, side by side at
  // matching height, each with its own titled bordered box.
  constexpr int kMixPanelWidth = 320;
  constexpr int kSubPanelHeight = 164 + kDspSubRowHeight;
  {
    int mixX = area.getX();
    int decayX = area.getX() + kMixPanelWidth + 10;
    int decayWidth = area.getWidth() - kMixPanelWidth - 10;
    int topY = y;

    mixPanel_->setBounds(mixX, topY, kMixPanelWidth, kSubPanelHeight);
    auto mixArea = juce::Rectangle<int>(mixX, topY, kMixPanelWidth, kSubPanelHeight)
                       .reduced(10, 6);
    mixHeading_.setBounds(mixArea.removeFromTop(20));
    constexpr int kToggleWidth = 165;
    auto layoutMixRow = [&](juce::ToggleButton& toggle, juce::Slider& slider) {
      auto row = mixArea.removeFromTop(kDspSubRowHeight);
      toggle.setBounds(row.removeFromLeft(kToggleWidth));
      slider.setBounds(row);
    };
    layoutMixRow(directEnabledToggle_, directLevelSlider_);
    layoutMixRow(earlyEnabledToggle_, earlyLevelSlider_);
    layoutMixRow(diffuseEnabledToggle_, diffuseLevelSlider_);

    decayPanel_->setBounds(decayX, topY, decayWidth, kSubPanelHeight);
    auto decayArea =
        juce::Rectangle<int>(decayX, topY, decayWidth, kSubPanelHeight).reduced(10, 6);
    decayHeading_.setBounds(decayArea.removeFromTop(20));

    constexpr int kDspLabelWidth = 90;
    constexpr int kDspSliderWidth = 110;
    constexpr int kVolumeLabelWidth = 80;
    auto layoutDspRow = [&](juce::Rectangle<int> row,
                           std::initializer_list<
                               std::pair<juce::Component*, juce::Slider*>>
                               controls) {
      for (auto pair : controls) {
        pair.first->setBounds(row.removeFromLeft(kDspLabelWidth));
        pair.second->setBounds(row.removeFromLeft(kDspSliderWidth));
        row.removeFromLeft(12);
      }
    };
    layoutDspRow(decayArea.removeFromTop(kDspSubRowHeight),
                {{&decayLabel_, &decaySlider_}, {&dampingLabel_, &dampingSlider_}});
    {
      auto row = decayArea.removeFromTop(kDspSubRowHeight);
      roomSizeLabel_.setBounds(row.removeFromLeft(kDspLabelWidth));
      roomSizeSlider_.setBounds(row.removeFromLeft(kDspSliderWidth));
      roomVolumeLabel_.setBounds(row.removeFromLeft(kVolumeLabelWidth));
    }
    layoutDspRow(decayArea.removeFromTop(kDspSubRowHeight),
                {{&densityLabel_, &densitySlider_},
                 {&preDelayLabel_, &preDelaySlider_}});
    layoutDspRow(decayArea.removeFromTop(kDspSubRowHeight),
                {{&bandwidthLabel_, &bandwidthSlider_}});

    y = topY + kSubPanelHeight;
  }
  y += 10;

  timeStructureHeading_.setBounds(takeTop(kRowHeight));
  timeStructureView_->setBounds(takeTop(kTimeStructureHeight));
  y += 8;  // inner bottom padding

  parametersPanel_->setBounds(area.getX() - 6, parametersPanelTop,
                              area.getWidth() + 12, y - parametersPanelTop);
  y += 10;

  // The active-inputs rows live inside activeListContent_ (scrolled by
  // activeListViewport_, already positioned above alongside the scene
  // view), not in the main column - laid out in their own local space.
  {
    int listWidth = activeListViewport_.getWidth() > 0
                        ? activeListViewport_.getWidth() -
                              activeListViewport_.getScrollBarThickness()
                        : activeListViewport_.getWidth();
    listWidth = juce::jmax(listWidth, 100);
    int rowY = 0;
    for (auto const& row : activeRows_) {
      row->stripe->setBounds(0, rowY, 5, kActiveRowHeight);
      row->label->setBounds(8, rowY, listWidth - 46, kActiveRowHeight);
      row->removeButton->setBounds(listWidth - 36, rowY + 9, 28, 28);
      rowY += kActiveRowHeight + 2;
    }
    activeListContent_.setSize(listWidth,
                               juce::jmax(rowY, activeListViewport_.getHeight()));
  }

  {
    auto headingRow = takeTop(kRowHeight);
    collapseAllButton_.setBounds(headingRow.removeFromRight(100));
    headingRow.removeFromRight(8);
    pointsHeading_.setBounds(headingRow);
  }
  bulkRoutingLabel_.setVisible(allExpanded_);
  bulkRoutingCombo_.setVisible(allExpanded_);
  bulkRoutingJumpField_.setVisible(allExpanded_);
  if (allExpanded_) {
    auto row = takeTop(kRowHeight);
    bulkRoutingLabel_.setBounds(row.removeFromLeft(150));
    row.removeFromLeft(8);
    bulkRoutingCombo_.setBounds(row.removeFromLeft(110));
    row.removeFromLeft(4);
    bulkRoutingJumpField_.setBounds(row.removeFromLeft(34));
    y += 4;
  }
  pointsColumnHeader_->setVisible(allExpanded_);
  if (allExpanded_) {
    pointsColumnHeader_->setBounds(takeTop(18));
  }
  for (auto const& row : pointRows_) {
    row->setVisible(allExpanded_);
    if (allExpanded_) {
      row->setBounds(area.getX(), y, area.getWidth(),
                     ReverbPointControlsRow::kPreferredHeight);
      y += ReverbPointControlsRow::kPreferredHeight + 2;
    }
  }
  y += 10;

  oscOutputHeaderButton_.setBounds(takeTop(kRowHeight).removeFromLeft(160));
  if (oscOutputExpanded_) {
    auto row = takeTop(kRowHeight);
    oscOutputEnableToggle_.setBounds(row.removeFromLeft(90));
    row.removeFromLeft(12);
    oscOutputIpLabel_.setBounds(row.removeFromLeft(24));
    oscOutputIpEditor_.setBounds(row.removeFromLeft(140));
    row.removeFromLeft(12);
    oscOutputPortLabel_.setBounds(row.removeFromLeft(36));
    oscOutputPortEditor_.setBounds(row.removeFromLeft(70));
  }
  oscOutputEnableToggle_.setVisible(oscOutputExpanded_);
  oscOutputIpLabel_.setVisible(oscOutputExpanded_);
  oscOutputIpEditor_.setVisible(oscOutputExpanded_);
  oscOutputPortLabel_.setVisible(oscOutputExpanded_);
  oscOutputPortEditor_.setVisible(oscOutputExpanded_);
  y += 10;

  oscInputHeaderButton_.setBounds(takeTop(kRowHeight).removeFromLeft(160));
  if (oscInputExpanded_) {
    auto row = takeTop(kRowHeight);
    oscInputEnableToggle_.setBounds(row.removeFromLeft(90));
    row.removeFromLeft(12);
    oscInputPortLabel_.setBounds(row.removeFromLeft(36));
    oscInputPortEditor_.setBounds(row.removeFromLeft(70));
  }
  oscInputEnableToggle_.setVisible(oscInputExpanded_);
  oscInputPortLabel_.setVisible(oscInputExpanded_);
  oscInputPortEditor_.setVisible(oscInputExpanded_);
  y += 10;

  masterContent_.setSize(contentWidth, y);
}
