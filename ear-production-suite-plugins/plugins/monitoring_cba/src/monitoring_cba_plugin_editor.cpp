#include "monitoring_cba_plugin_editor.hpp"
#include "binary_data.hpp"
#include "components/look_and_feel/colours.hpp"
#include "components/look_and_feel/fonts.hpp"
#include "helper/custom_layout_file.hpp"

using namespace ear::plugin::ui;

namespace {
constexpr int kRowHeight = 24;
constexpr int kActiveRowHeight = 46;
constexpr int kSceneViewHeight = 260;
constexpr int kPopupWidth = 700;
constexpr int kPopupHeight = 440;

class SectionPanel : public juce::Component {
 public:
  void paint(juce::Graphics& g) override {
    g.setColour(EarColours::Area01dp);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    g.setColour(EarColours::Sphere);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f, 1.0f);
  }
};

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

juce::Rectangle<int> popupBounds(juce::Rectangle<int> editorBounds) {
  int w = juce::jmin(kPopupWidth, editorBounds.getWidth() - 20);
  int h = juce::jmin(kPopupHeight, editorBounds.getHeight() - 20);
  return juce::Rectangle<int>(0, 0, w, h).withCentre(editorBounds.getCentre());
}

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
}  // namespace

MonitoringCbaAudioProcessorEditor::MonitoringCbaAudioProcessorEditor(
    MonitoringCbaAudioProcessor* p)
    : AudioProcessorEditor(p), p_(p) {
  masterViewport_.setViewedComponent(&masterContent_, false);
  masterViewport_.setScrollBarsShown(true, false);
  addAndMakeVisible(masterViewport_);

  titleLabel_.setText(
      "LF Decoder for EAR Production Suite (decodes picked Objects "
      "straight to channel-based audio, no EAR Scene render path needed)",
      juce::dontSendNotification);
  titleLabel_.setFont(juce::Font(16.0f, juce::Font::bold));
  masterContent_.addAndMakeVisible(titleLabel_);

  activeHeading_.setText("Inputs feeding the monitor", juce::dontSendNotification);
  masterContent_.addAndMakeVisible(activeHeading_);
  addItemButton_.onClick = [this] { openAddItemsPopup(); };
  masterContent_.addAndMakeVisible(addItemButton_);
  activeListViewport_.setViewedComponent(&activeListContent_, false);
  activeListViewport_.setScrollBarsShown(true, false);
  masterContent_.addAndMakeVisible(activeListViewport_);

  sceneViewHeading_.setText(
      "Position overview (EAR Scene-style) - active sources in their Scene "
      "colour",
      juce::dontSendNotification);
  masterContent_.addAndMakeVisible(sceneViewHeading_);
  sceneView_ = std::make_unique<MonitoringCbaSceneView>(p_);
  masterContent_.addAndMakeVisible(*sceneView_);

  setupPanel_ = std::make_unique<SectionPanel>();
  masterContent_.addAndMakeVisible(*setupPanel_);
  setupHeading_.setFont(juce::Font(13.0f, juce::Font::bold));
  masterContent_.addAndMakeVisible(setupHeading_);

  masterContent_.addAndMakeVisible(layoutLabel_);
  for (int i = 0; i < static_cast<int>(
                      MonitoringCbaAudioProcessor::getStandardLayoutChoices().size());
      ++i) {
    layoutCombo_.addItem(
        MonitoringCbaAudioProcessor::getStandardLayoutChoices()[static_cast<size_t>(i)]
            .displayName,
        i + 1);
  }
  layoutCombo_.onChange = [this] {
    int id = layoutCombo_.getSelectedId();
    if (id <= 0) return;
    p_->setStandardLayout(id - 1);
    updateChannelNamesLabel();
    if (sceneView_) sceneView_->refresh();
  };
  masterContent_.addAndMakeVisible(layoutCombo_);
  refreshLayoutCombo();

  loadCustomButton_.onClick = [this] { promptLoadCustomLayout(); };
  masterContent_.addAndMakeVisible(loadCustomButton_);

  channelNamesLabel_.setFont(juce::Font(13.0f));
  channelNamesLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  masterContent_.addAndMakeVisible(channelNamesLabel_);
  updateChannelNamesLabel();

  p_->onAvailableObjectsChanged = [this] {
    refreshActiveInputs();
    if (sceneView_) sceneView_->refresh();
    if (addItemsPopup_) addItemsPopup_->refresh();
  };
  refreshActiveInputs();

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

  setResizable(true, true);
  setSize(1000, 700);
}

MonitoringCbaAudioProcessorEditor::~MonitoringCbaAudioProcessorEditor() {
  p_->onAvailableObjectsChanged = nullptr;
}

void MonitoringCbaAudioProcessorEditor::refreshLayoutCombo() {
  auto current = p_->getCurrentLayoutDisplayName();
  auto const& choices = MonitoringCbaAudioProcessor::getStandardLayoutChoices();
  int selected = 0;
  for (int i = 0; i < static_cast<int>(choices.size()); ++i)
    if (choices[static_cast<size_t>(i)].displayName == current) selected = i + 1;
  layoutCombo_.setSelectedId(selected, juce::dontSendNotification);
}

void MonitoringCbaAudioProcessorEditor::updateChannelNamesLabel() {
  auto names = p_->getOutputChannelNames();
  channelNamesLabel_.setText(
      p_->getCurrentLayoutDisplayName() + "  (" +
          juce::String(p_->getOutputChannelCount()) + " ch): " +
          names.joinIntoString(", "),
      juce::dontSendNotification);
  refreshLayoutCombo();
}

void MonitoringCbaAudioProcessorEditor::promptLoadCustomLayout() {
  fileChooser_ = std::make_unique<juce::FileChooser>(
      "Select a loudspeaker-layout text file", juce::File(), "*.txt;*.csv");
  fileChooser_->launchAsync(
      juce::FileBrowserComponent::openMode |
          juce::FileBrowserComponent::canSelectFiles,
      [this](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (!file.existsAsFile()) return;
        try {
          p_->loadCustomLayoutFile(file);
        } catch (const ear::plugin::CustomLayoutFileError& e) {
          juce::AlertWindow::showMessageBoxAsync(
              juce::AlertWindow::WarningIcon, "Could not load layout",
              juce::String(e.what()));
        }
        updateChannelNamesLabel();
        if (sceneView_) sceneView_->refresh();
      });
}

void MonitoringCbaAudioProcessorEditor::refreshActiveInputs() {
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

void MonitoringCbaAudioProcessorEditor::openAddItemsPopup() {
  if (addItemsPopup_) return;

  auto backdrop = std::make_unique<ModalBackdrop>();
  backdrop->onClick = [this] { closeAddItemsPopup(); };
  backdrop->setBounds(getLocalBounds());
  addAndMakeVisible(*backdrop);
  modalBackdrop_ = std::move(backdrop);

  auto popup = std::make_unique<MonitoringCbaAddItemsPopup>(p_);
  popup->onClose = [this] {
    refreshActiveInputs();
    closeAddItemsPopup();
  };
  popup->setBounds(popupBounds(getLocalBounds()));
  addAndMakeVisible(*popup);
  addItemsPopup_ = std::move(popup);
}

void MonitoringCbaAudioProcessorEditor::closeAddItemsPopup() {
  addItemsPopup_.reset();
  modalBackdrop_.reset();
}

void MonitoringCbaAudioProcessorEditor::paint(juce::Graphics& g) {
  g.fillAll(EarColours::Background);
}

void MonitoringCbaAudioProcessorEditor::resized() {
  masterViewport_.setBounds(getLocalBounds());
  if (modalBackdrop_) modalBackdrop_->setBounds(getLocalBounds());
  if (addItemsPopup_) addItemsPopup_->setBounds(popupBounds(getLocalBounds()));
  layoutContent();
}

void MonitoringCbaAudioProcessorEditor::layoutContent() {
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

  int setupPanelTop = y - 4;
  y += 8;
  setupHeading_.setBounds(takeTop(20));
  {
    auto r = takeTop(kRowHeight);
    layoutLabel_.setBounds(r.removeFromLeft(60));
    r.removeFromLeft(8);
    loadCustomButton_.setBounds(r.removeFromRight(160));
    r.removeFromRight(8);
    layoutCombo_.setBounds(r);
  }
  channelNamesLabel_.setBounds(takeTop(kRowHeight));
  y += 8;
  setupPanel_->setBounds(area.getX() - 6, setupPanelTop, area.getWidth() + 12,
                         y - setupPanelTop);
  y += 10;

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

  masterContent_.setSize(contentWidth, y);
}
