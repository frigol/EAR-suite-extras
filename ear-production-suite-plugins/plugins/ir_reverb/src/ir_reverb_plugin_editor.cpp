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

void setupLevelSlider(juce::Slider& s, double min, double max, double step,
                      double initial) {
  s.setSliderStyle(juce::Slider::LinearHorizontal);
  s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
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
      "IR Reverb for EAR Suite (convolution, direction-weighted encode/decode)",
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

  pointsHeading_.setText(
      "Output points (Object-style parameters, like a real EAR Object)",
      juce::dontSendNotification);
  masterContent_.addAndMakeVisible(pointsHeading_);
  collapseAllButton_.setButtonText("Collapse all");
  collapseAllButton_.onClick = [this] {
    allExpanded_ = !allExpanded_;
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
    row->positionLabel->setFont(juce::Font(12.0f));
    row->positionLabel->setColour(juce::Label::textColourId, juce::Colours::grey);
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
  {
    auto r = takeTop(kRowHeight);
    wetLabel_.setBounds(r.removeFromLeft(100));
    wetSlider_.setBounds(r);
  }
  {
    auto r = takeTop(kRowHeight);
    preDelayLabel_.setBounds(r.removeFromLeft(100));
    preDelaySlider_.setBounds(r);
  }
  y += 8;
  irPanel_->setBounds(area.getX() - 6, irPanelTop, area.getWidth() + 12,
                      y - irPanelTop);
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

  masterContent_.setSize(contentWidth, y);
}
