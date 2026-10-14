#include "custom_monitoring_plugin_editor.hpp"

#include "components/look_and_feel/colours.hpp"
#include "components/look_and_feel/fonts.hpp"
#include "components/version_label.hpp"
#include "detail/constants.hpp"
#include "helper/properties_file.hpp"
#include "helper/custom_layout_file.hpp"

#include <string>

using namespace ear::plugin::ui;

EarCustomMonitoringAudioProcessorEditor::
    EarCustomMonitoringAudioProcessorEditor(
        EarCustomMonitoringAudioProcessor* p)
    : AudioProcessorEditor(p),
      p_(p),
      header_(std::make_unique<EarHeader>()),
      onBoardingButton_(std::make_unique<EarButton>()),
      onBoardingOverlay_(std::make_unique<Overlay>()),
      onBoardingContent_(std::make_unique<Onboarding>()),
      loadLayoutButton_(std::make_unique<EarButton>()),
      speakerMeterGrid_(std::make_unique<SpeakerMeterGrid>()),
      propertiesFileLock_(
          std::make_unique<InterProcessLock>("EPS_preferences")),
      propertiesFile_(getPropertiesFile(propertiesFileLock_.get())) {
  // Leading space: EarHeader positions the heading text right against the
  // logo's own bounding box with no gap, so without it the text visually
  // runs into the logo.
  header_->setText(" Custom Monitoring for EAR Suite");

  byLabel_.setFont(font::RobotoSingleton::instance().getRegular(11.f));
  byLabel_.setColour(Label::textColourId,
                     EarColours::Text.withAlpha(Emphasis::disabled));
  byLabel_.setJustificationType(Justification::centredLeft);
  byLabel_.setText("Unofficial fork by Luca Frigo - not affiliated with EBU",
                   dontSendNotification);

  onBoardingButton_->setButtonText("?");
  onBoardingButton_->setShape(EarButton::Shape::Circular);
  onBoardingButton_->setFont(
      font::RobotoSingleton::instance().getRegular(20.f));
  onBoardingButton_->onClick = [&]() { onBoardingOverlay_->setVisible(true); };

  onBoardingOverlay_->setContent(onBoardingContent_.get());
  onBoardingOverlay_->setWindowSize(706, 596);
  onBoardingOverlay_->setHeaderText(
      String::fromUTF8("Welcome – do you need help?"));
  onBoardingOverlay_->onClose = [&]() {
    onBoardingOverlay_->setVisible(false);
    propertiesFile_->setValue("showOnboarding", false);
  };
  if (!propertiesFile_->containsKey("showOnboarding")) {
    onBoardingOverlay_->setVisible(true);
  }
  onBoardingContent_->addListener(this);

  loadLayoutButton_->setButtonText("Load layout...");
  loadLayoutButton_->setShape(EarButton::Shape::Rounded);
  loadLayoutButton_->onClick = [this]() { promptLoadLayout(); };

  layoutFileLabel_.setFont(font::RobotoSingleton::instance().getRegular(14.f));
  layoutFileLabel_.setColour(Label::textColourId,
                             EarColours::Text.withAlpha(Emphasis::medium));
  layoutFileLabel_.setJustificationType(Justification::centredLeft);

  addAndMakeVisible(header_.get());
  addAndMakeVisible(onBoardingButton_.get());
  addChildComponent(onBoardingOverlay_.get());
  addAndMakeVisible(loadLayoutButton_.get());
  addAndMakeVisible(layoutFileLabel_);

  configureVersionLabel(versionLabel);
  addAndMakeVisible(versionLabel);
  addAndMakeVisible(byLabel_);

  addAndMakeVisible(speakerMeterGrid_.get());

  p_->onLayoutChanged = [this]() { rebuildSpeakerMeters(); };
  rebuildSpeakerMeters();

  setSize(735, 655);
  setResizable(true, true);
  setResizeLimits(500, 400, 2000, 2000);
}

EarCustomMonitoringAudioProcessorEditor::
    ~EarCustomMonitoringAudioProcessorEditor() {
  p_->onLayoutChanged = nullptr;
}

void EarCustomMonitoringAudioProcessorEditor::rebuildSpeakerMeters() {
  auto const& layout = p_->currentLayout();
  auto const& channels = layout.channels();

  speakerMeterGrid_->setMeterCount(static_cast<int>(channels.size()));
  for (int i = 0; i < static_cast<int>(channels.size()); ++i) {
    auto const& ch = channels[static_cast<size_t>(i)];
    speakerMeterGrid_->setSpeakerInfo(i, String(ch.name()),
                                      ch.isLfe() ? "LFE" : "");
    if (auto* meter = speakerMeterGrid_->meterAt(i)) {
      meter->getLevelMeter()->setMeter(p_->getLevelMeter(), i);
    }
  }

  auto file = p_->currentLayoutFile();
  std::string headingText = " Custom Monitoring for EAR Suite - " + layout.name();
  header_->setText(headingText);
  layoutFileLabel_.setText(
      file.existsAsFile()
          ? file.getFullPathName()
          : String("No layout file loaded (using default 0+2+0)"),
      dontSendNotification);

  resized();
}

void EarCustomMonitoringAudioProcessorEditor::promptLoadLayout() {
  fileChooser_ = std::make_unique<FileChooser>(
      "Select a loudspeaker layout file", File(), "*.txt;*.csv");
  // Capture a SafePointer rather than "this": the dialog is async, and the
  // plugin window (and this editor with it) can be closed by the user or
  // the host while it's still open, before the callback fires.
  Component::SafePointer<EarCustomMonitoringAudioProcessorEditor> safeThis(
      this);
  fileChooser_->launchAsync(
      FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
      [safeThis](const FileChooser& fc) {
        if (safeThis == nullptr) return;
        auto file = fc.getResult();
        if (!file.existsAsFile()) return;
        try {
          safeThis->p_->loadLayoutFile(file);
          // rebuildSpeakerMeters() is triggered via p_->onLayoutChanged
        } catch (const std::exception& e) {
          // Catches ear::plugin::CustomLayoutFileError (a malformed file)
          // as well as anything libear itself might throw while building
          // the gain calculator/decorrelators for an unusual geometry (e.g.
          // degenerate/duplicate speaker positions) - either way, this is
          // called from an OS file-dialog callback with nothing above it to
          // catch an escaping exception, so leaving any of these uncaught
          // takes the whole host down rather than just failing the load.
          AlertWindow::showMessageBoxAsync(
              AlertWindow::WarningIcon, "Could not load layout",
              String("The layout file could not be loaded:\n\n") + e.what());
        } catch (...) {
          AlertWindow::showMessageBoxAsync(
              AlertWindow::WarningIcon, "Could not load layout",
              "The layout file could not be loaded (unknown error).");
        }
      });
}

void EarCustomMonitoringAudioProcessorEditor::paint(Graphics& g) {
  g.fillAll(EarColours::Background);
}

void EarCustomMonitoringAudioProcessorEditor::resized() {
  auto area = getLocalBounds();
  onBoardingOverlay_->setBounds(area);
  area.reduce(5, 5);
  auto headingArea = area.removeFromTop(55);
  onBoardingButton_->setBounds(
      headingArea.removeFromRight(39).removeFromBottom(39));
  header_->setBounds(headingArea);

  area.removeFromTop(10);
  auto bottomLabelsArea = area.removeFromBottom(30);
  byLabel_.setBounds(bottomLabelsArea.removeFromLeft(bottomLabelsArea.getWidth() / 2));
  versionLabel.setBounds(bottomLabelsArea);

  auto loadBarArea = area.removeFromTop(36);
  loadLayoutButton_->setBounds(loadBarArea.removeFromLeft(150));
  loadBarArea.removeFromLeft(10);
  layoutFileLabel_.setBounds(loadBarArea);
  area.removeFromTop(10);

  speakerMeterGrid_->setBounds(area);
}

void EarCustomMonitoringAudioProcessorEditor::endButtonClicked(
    Onboarding* onboarding) {
  onBoardingOverlay_->setVisible(false);
  propertiesFile_->setValue("showOnboarding", false);
}
void EarCustomMonitoringAudioProcessorEditor::moreButtonClicked(
    Onboarding* onboarding) {
  onBoardingOverlay_->setVisible(false);
  URL(ear::plugin::detail::MORE_INFO_URL).launchInDefaultBrowser();
}
