#include "point_encoder_plugin_editor.hpp"
#include "components/look_and_feel/colours.hpp"
#include "components/look_and_feel/fonts.hpp"
#include "helper/custom_layout_file.hpp"

using namespace ear::plugin::ui;

namespace {
constexpr int kRowHeight = 24;
constexpr int kSceneViewHeight = 260;

class PointsColumnHeader : public juce::Component {
 public:
  void paint(juce::Graphics& g) override {
    g.setColour(EarColours::Label);
    g.setFont(EarFontsSingleton::instance().Description);
    auto area = getLocalBounds();
    area.removeFromLeft(PointEncoderPointControlsRow::kActiveWidth + 4 +
                        PointEncoderPointControlsRow::kNameWidth + 8 +
                        PointEncoderPointControlsRow::kChannelWidth + 4 +
                        PointEncoderPointControlsRow::kJumpWidth + 8 +
                        PointEncoderPointControlsRow::kEarChannelWidth + 4 +
                        PointEncoderPointControlsRow::kJumpWidth);
    for (auto const& title :
        {"Az", "El", "Dist", "Gain", "W", "H", "D", "Diff"}) {
      g.drawText(title,
                area.removeFromLeft(PointEncoderPointControlsRow::kColumnWidth),
                juce::Justification::centred);
      area.removeFromLeft(PointEncoderPointControlsRow::kColumnGap);
    }
  }
};
}  // namespace

PointEncoderAudioProcessorEditor::PointEncoderAudioProcessorEditor(
    PointEncoderAudioProcessor* p)
    : AudioProcessorEditor(p), p_(p) {
  masterViewport_.setViewedComponent(&masterContent_, false);
  masterViewport_.setScrollBarsShown(true, false);
  addAndMakeVisible(masterViewport_);

  titleLabel_.setText(
      "LF Multi-Encoder for EAR Production Suite (up to 24 independent "
      "Objects from plain audio channels)",
      juce::dontSendNotification);
  titleLabel_.setFont(juce::Font(16.0f, juce::Font::bold));
  masterContent_.addAndMakeVisible(titleLabel_);

  masterContent_.addAndMakeVisible(layoutPresetLabel_);
  layoutPresetCombo_.setTextWhenNothingSelected("(choose a standard layout...)");
  for (int i = 0;
      i < static_cast<int>(PointEncoderAudioProcessor::getStandardLayoutChoices().size());
      ++i) {
    layoutPresetCombo_.addItem(
        PointEncoderAudioProcessor::getStandardLayoutChoices()[static_cast<size_t>(i)]
            .displayName,
        i + 1);
  }
  layoutPresetCombo_.onChange = [this] {
    int id = layoutPresetCombo_.getSelectedId();
    if (id <= 0) return;
    p_->loadStandardLayout(id - 1);
    layoutStatusLabel_.setColour(juce::Label::textColourId,
                                 juce::Colours::lightgreen);
    layoutStatusLabel_.setText(
        "Loaded preset: " + layoutPresetCombo_.getText(),
        juce::dontSendNotification);
    for (auto const& row : pointRows_) row->refreshFromProcessor();
    if (sceneView_) sceneView_->refresh();
  };
  masterContent_.addAndMakeVisible(layoutPresetCombo_);

  loadLayoutButton_.onClick = [this] { promptLoadLayout(); };
  masterContent_.addAndMakeVisible(loadLayoutButton_);
  layoutStatusLabel_.setFont(juce::Font(13.0f));
  layoutStatusLabel_.setColour(juce::Label::textColourId, juce::Colours::grey);
  masterContent_.addAndMakeVisible(layoutStatusLabel_);

  sceneViewHeading_.setText(
      "Position overview (EAR Scene-style) - active points only",
      juce::dontSendNotification);
  masterContent_.addAndMakeVisible(sceneViewHeading_);
  sceneView_ = std::make_unique<PointEncoderSceneView>(p_);
  masterContent_.addAndMakeVisible(*sceneView_);

  pointsHeading_.setText(
      "Points (activate the ones you need - each becomes its own ADM "
      "Object)",
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

  for (int i = 0; i < PointEncoderAudioProcessor::kMaxPoints; ++i) {
    auto row = std::make_unique<PointEncoderPointControlsRow>(p_, i);
    row->onChanged = [this] { if (sceneView_) sceneView_->refresh(); };
    masterContent_.addAndMakeVisible(*row);
    pointRows_[static_cast<size_t>(i)] = std::move(row);
  }

  p_->onPointsChanged = [this] {
    for (auto const& row : pointRows_) row->refreshFromProcessor();
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
  setSize(1150, 870);
}

PointEncoderAudioProcessorEditor::~PointEncoderAudioProcessorEditor() {
  p_->onPointsChanged = nullptr;
}

void PointEncoderAudioProcessorEditor::promptLoadLayout() {
  fileChooser_ = std::make_unique<juce::FileChooser>(
      "Select a loudspeaker-layout text file", juce::File(), "*.txt;*.csv");
  fileChooser_->launchAsync(
      juce::FileBrowserComponent::openMode |
          juce::FileBrowserComponent::canSelectFiles,
      [this](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (!file.existsAsFile()) return;
        try {
          p_->loadLayoutFile(file);
          layoutStatusLabel_.setColour(juce::Label::textColourId,
                                       juce::Colours::lightgreen);
          layoutStatusLabel_.setText("Loaded: " + file.getFileName(),
                                     juce::dontSendNotification);
        } catch (const ear::plugin::CustomLayoutFileError& e) {
          layoutStatusLabel_.setColour(juce::Label::textColourId,
                                       juce::Colours::orange);
          layoutStatusLabel_.setText(juce::String(e.what()),
                                     juce::dontSendNotification);
        }
        for (auto const& row : pointRows_) row->refreshFromProcessor();
        if (sceneView_) sceneView_->refresh();
      });
}

void PointEncoderAudioProcessorEditor::paint(juce::Graphics& g) {
  g.fillAll(EarColours::Background);
}

void PointEncoderAudioProcessorEditor::resized() {
  masterViewport_.setBounds(getLocalBounds());
  layoutContent();
}

void PointEncoderAudioProcessorEditor::layoutContent() {
  int contentWidth = masterViewport_.getWidth() > 0
                         ? masterViewport_.getWidth() -
                               masterViewport_.getScrollBarThickness()
                         : 980;
  contentWidth = juce::jmax(contentWidth, 900);

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
    auto row = takeTop(kRowHeight);
    layoutPresetLabel_.setBounds(row.removeFromLeft(80));
    row.removeFromLeft(4);
    layoutPresetCombo_.setBounds(row.removeFromLeft(220));
    row.removeFromLeft(10);
    loadLayoutButton_.setBounds(row.removeFromLeft(150));
  }
  {
    auto row = takeTop(kRowHeight);
    layoutStatusLabel_.setBounds(row);
  }
  y += 10;

  sceneViewHeading_.setBounds(takeTop(kRowHeight));
  sceneView_->setBounds(area.getX(), y, area.getWidth(), kSceneViewHeight);
  y += kSceneViewHeight;
  y += 10;

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
                     PointEncoderPointControlsRow::kPreferredHeight);
      y += PointEncoderPointControlsRow::kPreferredHeight + 2;
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
