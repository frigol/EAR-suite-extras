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
  header_->setText(" LF Custom Monitoring for EAR Production Suite");

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
  std::string headingText = " LF Custom Monitoring - " + layout.name();
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
      "Select a loudspeaker layout file", File(), "*.txt;*.csv;*.json");
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
          if (!safeThis->p_->currentLayoutHasLfe()) {
            // IEM JSON layouts never carry an LFE channel (AllRAD has no
            // concept of one) - offer to designate one rather than silently
            // rendering without a sub. Harmless to offer for a .txt load
            // too (the user can just dismiss it), but only bother asking
            // when the extension suggests it's actually likely.
            if (file.getFileExtension().equalsIgnoreCase(".json")) {
              safeThis->promptAssignLfe();
            }
          }
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

void EarCustomMonitoringAudioProcessorEditor::promptAssignLfe() {
  auto* aw = new AlertWindow(
      "No subwoofer channel", // IEM's own term is irrelevant to the user
      "This layout has no channel marked as the subwoofer (LFE). Pick an "
      "existing speaker to use as the sub, or add a new one at its own "
      "position, or skip this and render without a sub for now.",
      AlertWindow::QuestionIcon);

  StringArray choices;
  choices.add("(skip - no subwoofer)");
  choices.add("Add a new speaker as the subwoofer...");
  for (auto const& ch : p_->currentLayout().channels()) {
    choices.add("Use existing: " + String(ch.name()));
  }
  aw->addComboBox("choice", choices, "Subwoofer channel");

  aw->addTextEditor("name", "Subwoofer", "New speaker name");
  aw->addTextEditor("azimuth", "0.0", "Azimuth (deg)");
  aw->addTextEditor("elevation", "-30.0", "Elevation (deg)");
  aw->addTextEditor("radius", "1.0", "Radius (m)");
  aw->addTextEditor("gain", "1.0", "Gain");

  aw->addButton("OK", 1, KeyPress(KeyPress::returnKey));
  aw->addButton("Cancel", 0, KeyPress(KeyPress::escapeKey));

  Component::SafePointer<EarCustomMonitoringAudioProcessorEditor> safeThis(
      this);
  aw->enterModalState(
      true,
      ModalCallbackFunction::create([safeThis, aw](int result) {
        std::unique_ptr<AlertWindow> owned(aw);
        if (safeThis == nullptr || result == 0) return;

        auto* combo = aw->getComboBoxComponent("choice");
        auto selected = combo != nullptr ? combo->getSelectedId() : 0;
        if (selected <= 1) return;  // "skip" or nothing selected

        try {
          if (selected == 2) {
            // "Add a new speaker as the subwoofer..."
            auto name = aw->getTextEditorContents("name");
            auto azimuth = aw->getTextEditorContents("azimuth").getDoubleValue();
            auto elevation =
                aw->getTextEditorContents("elevation").getDoubleValue();
            auto radius = aw->getTextEditorContents("radius").getDoubleValue();
            auto gain = aw->getTextEditorContents("gain").getDoubleValue();
            if (name.isEmpty()) name = "Subwoofer";
            safeThis->p_->addLfeChannelToCurrentLayout(name, azimuth,
                                                       elevation, radius, gain);
          } else {
            // "Use existing: <name>" entries start at combo id 3, in the
            // same order as p_->currentLayout().channels().
            std::size_t index = static_cast<std::size_t>(selected - 3);
            safeThis->p_->promoteChannelToLfe(index);
          }
        } catch (const std::exception& e) {
          AlertWindow::showMessageBoxAsync(
              AlertWindow::WarningIcon, "Could not set subwoofer channel",
              String("The subwoofer channel could not be set:\n\n") +
                  e.what());
        }
      }),
      false);
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
