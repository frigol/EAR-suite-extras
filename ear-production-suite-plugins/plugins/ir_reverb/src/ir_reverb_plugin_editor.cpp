#include "ir_reverb_plugin_editor.hpp"
#include "binary_data.hpp"
#include "components/look_and_feel/colours.hpp"
#include "components/look_and_feel/fonts.hpp"
#include <daw_channel_count.h>

using namespace ear::plugin::ui;

namespace {
constexpr int kRowHeight = 24;
constexpr int kActiveRowHeight = 46;  // matches EAR Scene's own ItemView row height
constexpr int kSceneViewHeight = 260;
constexpr int kPopupWidth = 700;
constexpr int kPopupHeight = 440;
// Same compact label+slider+value row the algorithmic Reverb's own Mix/
// Decay & Diffusion sub-panels use - tall enough for a LinearHorizontal
// slider's TextBoxBelow value readout to actually fit underneath it.
constexpr int kDspSubRowHeight = 38;
constexpr int kDspLabelWidth = 90;
constexpr int kDspSliderWidth = 150;

void setupLevelSlider(juce::Slider& s, double min, double max, double step,
                      double initial) {
  s.setSliderStyle(juce::Slider::LinearHorizontal);
  s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
  s.setScrollWheelEnabled(false);
  s.setRange(min, max, step);
  s.setValue(initial, juce::dontSendNotification);
}

// Titled, bordered background - copied from the algorithmic Reverb's own
// SectionPanel.
class SectionPanel : public juce::Component {
 public:
  void paint(juce::Graphics& g) override {
    g.setColour(EarColours::Area01dp);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    g.setColour(EarColours::Sphere);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f, 1.0f);
  }
};

// A thin colour-filled strip, for the per-item colour indicator on each
// active-input row - copied from the algorithmic Reverb's own ColourStripe.
class ColourStripe : public juce::Component {
 public:
  juce::Colour colour;
  void paint(juce::Graphics& g) override { g.fillAll(colour); }
};

std::unique_ptr<EarButton> makeIconButton(const char* onOffSvgData,
                                          int svgDataSize) {
  auto button = std::make_unique<EarButton>();
  button->setOffStateIcon(std::unique_ptr<juce::Drawable>(
      juce::Drawable::createFromImageData(onOffSvgData, static_cast<size_t>(svgDataSize))));
  button->setOnStateIcon(std::unique_ptr<juce::Drawable>(
      juce::Drawable::createFromImageData(onOffSvgData, static_cast<size_t>(svgDataSize))));
  return button;
}

// Centred, but never larger than the editor itself - copied from the
// algorithmic Reverb's own popupBounds().
juce::Rectangle<int> popupBounds(juce::Rectangle<int> editorBounds) {
  int w = juce::jmin(kPopupWidth, editorBounds.getWidth() - 20);
  int h = juce::jmin(kPopupHeight, editorBounds.getHeight() - 20);
  return juce::Rectangle<int>(0, 0, w, h).withCentre(editorBounds.getCentre());
}

// The dimming backdrop behind the "+ Item" popup - copied from the
// algorithmic Reverb's own ModalBackdrop.
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

class PointsColumnHeader : public juce::Component {
 public:
  void paint(juce::Graphics& g) override {
    g.setColour(EarColours::Label);
    g.setFont(EarFontsSingleton::instance().Description);
    auto area = getLocalBounds();
    for (auto const& title :
        {"Az", "El", "Dist", "Gain", "W", "H", "D", "Diff"}) {
      g.drawText(title, area.removeFromLeft(IrReverbPointControlsRow::kColumnWidth),
                juce::Justification::centred);
      area.removeFromLeft(IrReverbPointControlsRow::kColumnGap);
    }
  }
};
}  // namespace

IrReverbAudioProcessorEditor::IrReverbAudioProcessorEditor(
    IrReverbAudioProcessor* p)
    : AudioProcessorEditor(p), p_(p) {
  masterViewport_.setViewedComponent(&masterContent_, false);
  masterViewport_.setScrollBarsShown(true, false);
  addAndMakeVisible(masterViewport_);

  titleLabel_.setText(
      "LF IR Reverb for EAR Production Suite (convolution, "
      "direction-weighted encode/decode)",
      juce::dontSendNotification);
  titleLabel_.setFont(juce::Font(16.0f, juce::Font::bold));
  masterContent_.addAndMakeVisible(titleLabel_);

  // Left/right pair, matching EAR Scene's own ProgrammeView layout - same
  // as the algorithmic Reverb's own editor.
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

  sceneView_ = std::make_unique<IrReverbSceneView>(p_);
  masterContent_.addAndMakeVisible(*sceneView_);

  irPanel_ = std::make_unique<SectionPanel>();
  masterContent_.addAndMakeVisible(*irPanel_);
  masterContent_.addAndMakeVisible(irHeading_);

  shapingPanel_ = std::make_unique<SectionPanel>();
  masterContent_.addAndMakeVisible(*shapingPanel_);
  shapingHeading_.setFont(juce::Font(13.0f, juce::Font::bold));
  masterContent_.addAndMakeVisible(shapingHeading_);

  masterContent_.addAndMakeVisible(presetLabel_);
  presetCombo_.setTextWhenNothingSelected("(no preset)");
  presetCombo_.onChange = [this] {
    auto name = presetCombo_.getText();
    if (name.isNotEmpty() && p_->loadPreset(name))
      refreshDspControlsFromProcessor();
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

  importIrButton_.onClick = [this] {
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Select an impulse response", juce::File(),
        "*.wav;*.aif;*.aiff");
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc) {
          auto file = fc.getResult();
          if (!file.existsAsFile()) return;
          p_->importAndLoadIr(file);
          refreshIrCombo();
          updateIrStatusLabel();
        });
  };
  masterContent_.addAndMakeVisible(importIrButton_);
  irCombo_.onChange = [this] {
    auto name = irCombo_.getText();
    if (name.isNotEmpty()) p_->loadIrByName(name);
    updateIrStatusLabel();
  };
  masterContent_.addAndMakeVisible(irCombo_);
  irChannelsWarning_.setFont(juce::Font(13.0f));
  masterContent_.addAndMakeVisible(irChannelsWarning_);
  preferArrayToggle_.setToggleState(p_->getPreferArrayInterpretation(),
                                    juce::dontSendNotification);
  preferArrayToggle_.onClick = [this] {
    p_->setPreferArrayInterpretation(preferArrayToggle_.getToggleState());
    updateIrStatusLabel();
  };
  masterContent_.addAndMakeVisible(preferArrayToggle_);
  refreshIrCombo();
  updateIrStatusLabel();

  setupLevelSlider(wetSlider_, 0.0, 2.0, 0.01, p_->getWetLevel());
  wetSlider_.setDoubleClickReturnValue(true, 1.0);
  wetSlider_.setTooltip("Overall reverb output level. Double-click to reset.");
  wetSlider_.onValueChange = [this] {
    p_->setWetLevel(static_cast<float>(wetSlider_.getValue()));
  };
  masterContent_.addAndMakeVisible(wetLabel_);
  masterContent_.addAndMakeVisible(wetSlider_);

  setupLevelSlider(preDelaySlider_, 0.0, 500.0, 1.0, p_->getPreDelayMs());
  preDelaySlider_.setDoubleClickReturnValue(true, 0.0);
  preDelaySlider_.setTooltip(
      "Gap, in milliseconds, before the convolved reverb starts. Double-click to reset.");
  preDelaySlider_.onValueChange = [this] {
    p_->setPreDelayMs(static_cast<float>(preDelaySlider_.getValue()));
  };
  masterContent_.addAndMakeVisible(preDelayLabel_);
  masterContent_.addAndMakeVisible(preDelaySlider_);

  setupLevelSlider(stretchSlider_, 0.25, 4.0, 0.01, p_->getIrShapeParams().stretch);
  stretchSlider_.setDoubleClickReturnValue(true, 1.0);
  stretchSlider_.setTooltip(
      "Resamples the loaded IR to stretch (>1) or compress (<1) its own "
      "duration before anything else - this is what actually lengthens "
      "the tail past the file's own length (reads it back slower, which "
      "also darkens/slows its spectral content a little, same trade-off "
      "any simple stretch has). 1.0 = unmodified. Double-click to reset.");
  stretchSlider_.onValueChange = [this] {
    p_->setIrStretch(static_cast<float>(stretchSlider_.getValue()));
  };
  masterContent_.addAndMakeVisible(stretchLabel_);
  masterContent_.addAndMakeVisible(stretchSlider_);

  setupLevelSlider(decaySlider_, 0.05, 1.0, 0.01, p_->getIrShapeParams().decay);
  decaySlider_.setDoubleClickReturnValue(true, 1.0);
  decaySlider_.setTooltip(
      "Exponential fade-out trim applied after Stretch - 1.0 leaves the "
      "(possibly stretched) IR untouched; lower values shorten/soften the "
      "tail within that same duration. Double-click to reset.");
  decaySlider_.onValueChange = [this] {
    p_->setIrDecay(static_cast<float>(decaySlider_.getValue()));
  };
  masterContent_.addAndMakeVisible(decayLabel_);
  masterContent_.addAndMakeVisible(decaySlider_);

  setupLevelSlider(dampingSlider_, 0.0, 1.0, 0.01, p_->getIrShapeParams().damping);
  dampingSlider_.setDoubleClickReturnValue(true, 0.0);
  dampingSlider_.setTooltip(
      "High-frequency absorption applied to the whole shaped IR, 0 = none, "
      "1 = heavily darkened. Double-click to reset.");
  dampingSlider_.onValueChange = [this] {
    p_->setIrDamping(static_cast<float>(dampingSlider_.getValue()));
  };
  masterContent_.addAndMakeVisible(dampingLabel_);
  masterContent_.addAndMakeVisible(dampingSlider_);

  pointsHeading_.setText(
      "Output points (Object-style parameters, like a real EAR Object - "
      "turn Active off to free this point's Scene slot and route it "
      "through a Point Encoder instance instead)",
      juce::dontSendNotification);
  masterContent_.addAndMakeVisible(pointsHeading_);
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

  for (int i = 0; i < IrReverbAudioProcessor::kNumPoints; ++i) {
    auto row = std::make_unique<IrReverbPointControlsRow>(p_, i);
    row->onChanged = [this] { if (sceneView_) sceneView_->refresh(); };
    masterContent_.addAndMakeVisible(*row);
    pointRows_[static_cast<size_t>(i)] = std::move(row);
  }

  masterContent_.addAndMakeVisible(bulkRoutingLabel_);
  {
    constexpr int kNumPoints = IrReverbAudioProcessor::kNumPoints;
    constexpr int kNumRanges = MAX_DAW_CHANNELS - kNumPoints + 1;
    for (int start = 1; start <= kNumRanges; ++start)
      bulkRoutingCombo_.addItem(
          juce::String(start) + "-" + juce::String(start + kNumPoints - 1), start);
  }
  bulkRoutingCombo_.setTextWhenNothingSelected("(choose a range...)");
  bulkRoutingCombo_.onChange = [this] {
    int start = bulkRoutingCombo_.getSelectedId();
    if (start <= 0) return;
    for (int i = 0; i < IrReverbAudioProcessor::kNumPoints; ++i)
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
        MAX_DAW_CHANNELS - IrReverbAudioProcessor::kNumPoints + 1;
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

IrReverbAudioProcessorEditor::~IrReverbAudioProcessorEditor() {
  p_->onAvailableObjectsChanged = nullptr;
  p_->onTrackNameChanged = nullptr;
}

void IrReverbAudioProcessorEditor::refreshDspControlsFromProcessor() {
  wetSlider_.setValue(p_->getWetLevel(), juce::dontSendNotification);
  preDelaySlider_.setValue(p_->getPreDelayMs(), juce::dontSendNotification);
  auto shape = p_->getIrShapeParams();
  stretchSlider_.setValue(shape.stretch, juce::dontSendNotification);
  decaySlider_.setValue(shape.decay, juce::dontSendNotification);
  dampingSlider_.setValue(shape.damping, juce::dontSendNotification);
}

void IrReverbAudioProcessorEditor::refreshPresetCombo(
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

void IrReverbAudioProcessorEditor::updateIrStatusLabel() {
  if (p_->isArrayMode()) {
    irChannelsWarning_.setColour(juce::Label::textColourId,
                                 juce::Colours::lightgreen);
    irChannelsWarning_.setText(
        "Loaded as discrete array: " + p_->getArrayPresetName() + " (" +
            juce::String(p_->getLoadedIrNumChannels()) + " channels).",
        juce::dontSendNotification);
  } else if (p_->isAmbisonicMode()) {
    irChannelsWarning_.setColour(juce::Label::textColourId,
                                 juce::Colours::lightgreen);
    irChannelsWarning_.setText(
        "Loaded as AmbiX order " + juce::String(p_->getAmbisonicOrder()) +
            " (" + juce::String(p_->getLoadedIrNumChannels()) +
            " channels, ACN/SN3D).",
        juce::dontSendNotification);
  } else if (p_->isStereoMode()) {
    irChannelsWarning_.setColour(juce::Label::textColourId,
                                 juce::Colours::lightgreen);
    irChannelsWarning_.setText(
        "Loaded as stereo - each point's own channel (left-side points "
        "from channel 1, right-side from channel 2).",
        juce::dontSendNotification);
  } else if (p_->getLoadedIrNumChannels() > 1) {
    irChannelsWarning_.setColour(juce::Label::textColourId, juce::Colours::orange);
    irChannelsWarning_.setText(
        "Loaded IR has " + juce::String(p_->getLoadedIrNumChannels()) +
            " channels - not a recognised AmbiX order, only channel 1 is "
            "used (mono).",
        juce::dontSendNotification);
  } else {
    irChannelsWarning_.setText("", juce::dontSendNotification);
  }
}

void IrReverbAudioProcessorEditor::refreshIrCombo() {
  irCombo_.clear(juce::dontSendNotification);
  auto names = p_->getAvailableIrNames();
  int id = 1;
  int selected = 0;
  auto loaded = p_->getLoadedIrName();
  for (auto const& name : names) {
    irCombo_.addItem(name, id);
    if (name == loaded) selected = id;
    ++id;
  }
  irCombo_.setSelectedId(selected, juce::dontSendNotification);
}

void IrReverbAudioProcessorEditor::refreshActiveInputs() {
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

    auto info = p_->getInputSlotInfo(slot.slotIndex);
    row->positionLabel = std::make_unique<juce::Label>(
        juce::String(),
        "az " + juce::String(info.azimuthDeg, 1) + " / el " +
            juce::String(info.elevationDeg, 1) + " / dist " +
            juce::String(info.distance, 2));
    row->positionLabel->setFont(juce::Font(14.0f));
    row->positionLabel->setColour(juce::Label::textColourId,
                                  juce::Colours::lightgrey);
    activeListContent_.addAndMakeVisible(*row->positionLabel);

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

void IrReverbAudioProcessorEditor::openAddItemsPopup() {
  if (addItemsPopup_) return;

  auto backdrop = std::make_unique<ModalBackdrop>();
  backdrop->onClick = [this] { closeAddItemsPopup(); };
  backdrop->setBounds(getLocalBounds());
  addAndMakeVisible(*backdrop);
  modalBackdrop_ = std::move(backdrop);

  auto popup = std::make_unique<IrReverbAddItemsPopup>(p_);
  popup->onClose = [this] {
    refreshActiveInputs();
    closeAddItemsPopup();
  };
  popup->setBounds(popupBounds(getLocalBounds()));
  addAndMakeVisible(*popup);
  addItemsPopup_ = std::move(popup);
}

void IrReverbAudioProcessorEditor::closeAddItemsPopup() {
  addItemsPopup_.reset();
  modalBackdrop_.reset();
}

void IrReverbAudioProcessorEditor::paint(juce::Graphics& g) {
  g.fillAll(EarColours::Background);
}

void IrReverbAudioProcessorEditor::resized() {
  masterViewport_.setBounds(getLocalBounds());

  if (modalBackdrop_) modalBackdrop_->setBounds(getLocalBounds());
  if (addItemsPopup_) addItemsPopup_->setBounds(popupBounds(getLocalBounds()));

  layoutContent();
}

void IrReverbAudioProcessorEditor::layoutContent() {
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

  int irPanelTop = y - 4;
  y += 8;

  irHeading_.setBounds(takeTop(kRowHeight));
  {
    auto r = takeTop(kRowHeight);
    presetLabel_.setBounds(r.removeFromLeft(50));
    r.removeFromLeft(4);
    deletePresetButton_.setBounds(r.removeFromRight(70));
    r.removeFromRight(8);
    savePresetButton_.setBounds(r.removeFromRight(90));
    r.removeFromRight(8);
    presetCombo_.setBounds(r);
  }
  {
    auto r = takeTop(kRowHeight);
    importIrButton_.setBounds(r.removeFromLeft(120));
    r.removeFromLeft(8);
    irCombo_.setBounds(r);
  }
  irChannelsWarning_.setBounds(takeTop(kRowHeight));
  preferArrayToggle_.setBounds(takeTop(kRowHeight));
  y += 8;
  irPanel_->setBounds(area.getX() - 6, irPanelTop, area.getWidth() + 12,
                      y - irPanelTop);
  y += 10;

  // "Levels & IR Shaping" - same compact two-per-row label+slider+value
  // layout as the algorithmic Reverb's own Mix/Decay & Diffusion
  // sub-panels (see kDspLabelWidth/kDspSliderWidth/kDspSubRowHeight).
  int shapingPanelTop = y - 4;
  y += 8;
  shapingHeading_.setBounds(takeTop(20));
  auto layoutShapingRow = [&](juce::Label& labelA, juce::Slider& sliderA,
                             juce::Label* labelB, juce::Slider* sliderB) {
    auto r = takeTop(kDspSubRowHeight);
    labelA.setBounds(r.removeFromLeft(kDspLabelWidth));
    sliderA.setBounds(r.removeFromLeft(kDspSliderWidth));
    if (labelB && sliderB) {
      r.removeFromLeft(24);
      labelB->setBounds(r.removeFromLeft(kDspLabelWidth));
      sliderB->setBounds(r.removeFromLeft(kDspSliderWidth));
    }
  };
  layoutShapingRow(wetLabel_, wetSlider_, &preDelayLabel_, &preDelaySlider_);
  layoutShapingRow(stretchLabel_, stretchSlider_, &decayLabel_, &decaySlider_);
  layoutShapingRow(dampingLabel_, dampingSlider_, nullptr, nullptr);
  y += 8;
  shapingPanel_->setBounds(area.getX() - 6, shapingPanelTop, area.getWidth() + 12,
                           y - shapingPanelTop);
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
      row->label->setBounds(8, rowY, listWidth - 46, kActiveRowHeight / 2);
      row->positionLabel->setBounds(8, rowY + kActiveRowHeight / 2,
                                    listWidth - 46, kActiveRowHeight / 2);
      row->removeButton->setBounds(listWidth - 36, rowY + 9, 28, 28);
      rowY += kActiveRowHeight + 2;
    }
    activeListContent_.setSize(listWidth,
                               juce::jmax(rowY, activeListViewport_.getHeight()));
  }

  {
    auto row = takeTop(kRowHeight);
    collapseAllButton_.setBounds(row.removeFromRight(100));
    row.removeFromRight(8);
    pointsHeading_.setBounds(row);
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
  if (allExpanded_) pointsColumnHeader_->setBounds(takeTop(18));
  for (auto const& row : pointRows_) {
    row->setVisible(allExpanded_);
    if (allExpanded_) {
      row->setBounds(area.getX(), y, area.getWidth(),
                     IrReverbPointControlsRow::kPreferredHeight);
      y += IrReverbPointControlsRow::kPreferredHeight + 2;
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
