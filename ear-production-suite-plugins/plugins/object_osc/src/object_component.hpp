#pragma once

#include "object_plugin_processor.hpp"

#include "JuceHeader.h"

#include "components/look_and_feel/colours.hpp"
#include "components/look_and_feel/fonts.hpp"
#include "components/look_and_feel/shadows.hpp"
#include "components/look_and_feel/tooltips.hpp"
#include "components/ear_combo_box.hpp"
#include "components/onboarding.hpp"
#include "components/overlay.hpp"
#include "components/ear_header.hpp"
#include "helper/properties_file.hpp"
#include "detail/constants.hpp"
#include "value_box_extent.hpp"
#include "value_box_gain.hpp"
#include "value_box_main.hpp"
#include "value_box_metadata.hpp"
#include "value_box_panning.hpp"
#include "value_box_panning_view.hpp"
#include "components/version_label.hpp"
#include <memory>
#include <optional>

namespace ear {
namespace plugin {
namespace ui {

class ObjectsComponent : public Component,
                         private ear::plugin::ui::Onboarding::Listener {
 public:
  ObjectsComponent(ObjectsAudioProcessor* p)
      : p_(p),
        header(std::make_unique<EarHeader>()),
        onBoardingButton(std::make_unique<EarButton>()),
        onBoardingOverlay(std::make_unique<Overlay>()),
        onBoardingContent(std::make_unique<Onboarding>()),
        mainValueBox(std::make_unique<ValueBoxMain>()),
        gainValueBox(std::make_unique<ValueBoxGain>()),
        metadataValueBox(std::make_unique<ValueBoxMetadata>()),
        panningValueBox(std::make_unique<ValueBoxPanning>()),
        extentValueBox(std::make_unique<ValueBoxExtent>()),
        panningViewValueBox(std::make_unique<ValueBoxPanningView>()),
        oscToggle(std::make_unique<ToggleButton>()),
        oscProtocolLabel(std::make_unique<Label>()),
        oscProtocolCombo(std::make_unique<ComboBox>()),
        oscPortLabel(std::make_unique<Label>()),
        oscPortSlider(std::make_unique<Slider>()),
        oscSrcIndexLabel(std::make_unique<Label>()),
        oscSrcIndexSlider(std::make_unique<Slider>()),
        oscDebugLabel(std::make_unique<Label>()),
        statusBarLabel(std::make_unique<Label>()),
        propertiesFileLock(
            std::make_unique<InterProcessLock>("EPS_preferences")),
        propertiesFile(getPropertiesFile(propertiesFileLock.get())) {

    tooltipWindow.setLookAndFeel(&tooltipLookAndFeel);
    tooltipWindow.setOpaque(false);

    header->setText(" Object OSC for EAR Suite");

    byLabel.setFont(font::RobotoSingleton::instance().getRegular(11.f));
    byLabel.setColour(Label::textColourId,
                      EarColours::Text.withAlpha(Emphasis::disabled));
    byLabel.setJustificationType(Justification::centredLeft);
    byLabel.setText("Unofficial fork by Luca Frigo - not affiliated with EBU",
                    dontSendNotification);
    addAndMakeVisible(byLabel);
    onBoardingButton->setButtonText("?");
    onBoardingButton->setShape(EarButton::Shape::Circular);
    onBoardingButton->setFont(
        font::RobotoSingleton::instance().getRegular(20.f));
    onBoardingButton->onClick = [&]() { onBoardingOverlay->setVisible(true); };

    onBoardingOverlay->setContent(onBoardingContent.get());
    onBoardingOverlay->setWindowSize(706, 596);
    onBoardingOverlay->setHeaderText(
        String::fromUTF8("Welcome – do you need help?"));
    onBoardingOverlay->onClose = [&]() {
      onBoardingOverlay->setVisible(false);
      propertiesFile->setValue("showOnboarding", false);
    };
    if (!propertiesFile->containsKey("showOnboarding")) {
      onBoardingOverlay->setVisible(true);
    }
    onBoardingContent->addListener(this);

    addAndMakeVisible(header.get());
    addAndMakeVisible(onBoardingButton.get());
    addChildComponent(onBoardingOverlay.get());

    //addAndMakeVisible(metadataValueBox.get());
    addAndMakeVisible(mainValueBox.get());
    addAndMakeVisible(gainValueBox.get());
    addAndMakeVisible(panningValueBox.get());
    addAndMakeVisible(extentValueBox.get());
    addAndMakeVisible(panningViewValueBox.get());

    // OSC remote control (e.g. live position from SpatGRIS or another
    // show-control / spatialisation system) - see the address list in
    // ObjectsAudioProcessor's header comment.
    oscToggle->setButtonText("OSC remote control");
    oscToggle->setColour(ToggleButton::textColourId, EarColours::Text);
    oscToggle->setToggleState(p->getOscEnabled()->get(), dontSendNotification);
    oscToggle->onClick = [this]() {
      p_->setOscEnabled(oscToggle->getToggleState());
    };
    addAndMakeVisible(oscToggle.get());

    oscProtocolLabel->setText("Protocol", dontSendNotification);
    oscProtocolLabel->setColour(Label::textColourId, EarColours::Text);
    oscProtocolLabel->setJustificationType(Justification::centredRight);
    addAndMakeVisible(oscProtocolLabel.get());

    oscProtocolCombo->addItem("ControlGRIS2", 1);
    oscProtocolCombo->addItem("Custom (/object/*)", 2);
    oscProtocolCombo->setSelectedId(
        p->getOscProtocol() == ObjectsAudioProcessor::OscProtocol::ControlGris2 ? 1 : 2,
        dontSendNotification);
    oscProtocolCombo->onChange = [this]() {
      p_->setOscProtocol(oscProtocolCombo->getSelectedId() == 1
                            ? ObjectsAudioProcessor::OscProtocol::ControlGris2
                            : ObjectsAudioProcessor::OscProtocol::Custom);
    };
    addAndMakeVisible(oscProtocolCombo.get());

    oscPortLabel->setText("Port", dontSendNotification);
    oscPortLabel->setColour(Label::textColourId, EarColours::Text);
    oscPortLabel->setJustificationType(Justification::centredRight);
    addAndMakeVisible(oscPortLabel.get());

    oscPortSlider->setSliderStyle(Slider::IncDecButtons);
    oscPortSlider->setRange(1024.0, 65535.0, 1.0);
    oscPortSlider->setValue(p->getOscPort(), dontSendNotification);
    oscPortSlider->setTextBoxStyle(Slider::TextBoxLeft, false, 60, 22);
    oscPortSlider->onValueChange = [this]() {
      p_->setOscPort(static_cast<int>(oscPortSlider->getValue()));
    };
    addAndMakeVisible(oscPortSlider.get());

    // ControlGRIS/SpatGRIS send every source to one shared OSC
    // destination, disambiguated by this index (1-128) rather than one
    // destination per source - see the long comment on
    // ObjectsAudioProcessor::oscSourceIndex_. Irrelevant to this plugin's
    // own /object/* addresses, which are always implicitly "this object".
    oscSrcIndexLabel->setText("Src #", dontSendNotification);
    oscSrcIndexLabel->setColour(Label::textColourId, EarColours::Text);
    oscSrcIndexLabel->setJustificationType(Justification::centredRight);
    addAndMakeVisible(oscSrcIndexLabel.get());

    oscSrcIndexSlider->setSliderStyle(Slider::IncDecButtons);
    oscSrcIndexSlider->setRange(1.0, 128.0, 1.0);
    oscSrcIndexSlider->setValue(p->getOscSourceIndex(), dontSendNotification);
    oscSrcIndexSlider->setTextBoxStyle(Slider::TextBoxLeft, false, 40, 22);
    oscSrcIndexSlider->onValueChange = [this]() {
      p_->setOscSourceIndex(static_cast<int>(oscSrcIndexSlider->getValue()));
    };
    addAndMakeVisible(oscSrcIndexSlider.get());

    // Shows the last OSC packet this plugin received, even if it was
    // filtered out (wrong plugin id, wrong address) - the fastest way to
    // tell "is anything arriving, and does it look right" without an
    // external OSC sniffer.
    oscDebugLabel->setFont(Font(11.0f));
    oscDebugLabel->setColour(Label::textColourId, EarColours::Text.withAlpha(0.7f));
    oscDebugLabel->setJustificationType(Justification::centredLeft);
    oscDebugLabel->setText(p->getOscDebugText(), dontSendNotification);
    addAndMakeVisible(oscDebugLabel.get());
    p->onOscDebugChanged = [this]() {
      oscDebugLabel->setText(p_->getOscDebugText(), dontSendNotification);
    };

    metadataValueBox->setEnabled(false);
    metadataValueBox->setAlpha(0.38f);

    statusBarLabel->setFont(EarFontsSingleton::instance().Measures);
    addAndMakeVisible(statusBarLabel.get());

    configureVersionLabel(versionLabel);
    addAndMakeVisible(versionLabel);

    panningViewValueBox->getPannerTopView()->setAzimuth(p->getAzimuth()->get(),
                                                        dontSendNotification);
    panningViewValueBox->getPannerSideView()->setElevation(
        p->getElevation()->get(), dontSendNotification);
    panningViewValueBox->getPannerTopView()->setDistance(
        p->getDistance()->get(), dontSendNotification);

    levelMeterSetup();

    mainValueBox->getRoutingComboBox()->grabKeyboardFocus();
  }

  ~ObjectsComponent() { p_->onOscDebugChanged = nullptr; }

  void levelMeterSetup() {
    auto levelMeter = p_->getLevelMeter().lock();
    if(levelMeter){
      auto channelCount = levelMeter->channels();
      if(lastSetMeterChannelCount_ != channelCount ||
         lastSetMeterStartingChannel_ != 0 ||
         !lastSetMeterMode_.has_value() ||
         lastSetMeterMode_.value() != LevelMeter::MeterMode::PeakChannel) {
        std::vector<int> channelNums_(channelCount);
        std::iota(std::begin(channelNums_), std::end(channelNums_), 0);
        gainValueBox->getLevelMeter()->setMeterPeakChannel(levelMeter, channelNums_);
        lastSetMeterMode_ = LevelMeter::MeterMode::PeakChannel;
        lastSetMeterChannelCount_ = channelCount;
        lastSetMeterStartingChannel_ = 0;
      }
    }
  }

  void paint(Graphics& g) override {
    g.fillAll(EarColours::Background);
    levelMeterSetup();

    Shadows::elevation04dp.drawForRectangle(g, mainValueBox->getBounds());
    //Shadows::elevation04dp.drawForRectangle(g, metadataValueBox->getBounds());
    Shadows::elevation01dp.drawForRectangle(g, gainValueBox->getBounds());
    Shadows::elevation01dp.drawForRectangle(g, panningValueBox->getBounds());
    Shadows::elevation01dp.drawForRectangle(g, extentValueBox->getBounds());
    Shadows::elevation01dp.drawForRectangle(g,
                                            panningViewValueBox->getBounds());
  }

  void resized() override {
    auto area = getLocalBounds();
    onBoardingOverlay->setBounds(area);
    area.reduce(5, 5);
    auto headingArea = area.removeFromTop(55);
    auto bottomLabelsArea = area.removeFromBottom(30);
    statusBarLabel->setBounds(bottomLabelsArea.removeFromLeft(bottomLabelsArea.getWidth() / 2));
    versionLabel.setBounds(bottomLabelsArea);
    onBoardingButton->setBounds(
        headingArea.removeFromRight(39).removeFromBottom(39));
    header->setBounds(headingArea);

    byLabel.setBounds(area.removeFromTop(16));

    auto oscRow1 = area.removeFromTop(30);
    oscToggle->setBounds(oscRow1.removeFromLeft(160));
    oscRow1.removeFromLeft(10);
    oscProtocolLabel->setBounds(oscRow1.removeFromLeft(55));
    oscProtocolCombo->setBounds(oscRow1.removeFromLeft(150));
    area.removeFromTop(5);

    auto oscRow2 = area.removeFromTop(30);
    oscPortLabel->setBounds(oscRow2.removeFromLeft(40));
    oscPortSlider->setBounds(oscRow2.removeFromLeft(130));
    oscRow2.removeFromLeft(15);
    oscSrcIndexLabel->setBounds(oscRow2.removeFromLeft(40));
    oscSrcIndexSlider->setBounds(oscRow2.removeFromLeft(110));
    area.removeFromTop(5);

    auto oscDebugRow = area.removeFromTop(18);
    oscDebugLabel->setBounds(oscDebugRow);
    area.removeFromTop(5);

    auto leftColumn = area.withTrimmedRight(area.getWidth() / 2);
    auto rightColumn = area.withTrimmedLeft(area.getWidth() / 2);

    // left column
    mainValueBox->setBounds(leftColumn.removeFromTop(187).reduced(5, 5));
    gainValueBox->setBounds(leftColumn.removeFromTop(113).reduced(5, 5));
    // TODO - panningValueBox original position - reinstate when metadata implemented
    //panningValueBox->setBounds(leftColumn.removeFromTop(219).reduced(5, 5));
    // TODO - extent component reduced by 135px to hide divergence (currently not implemented)
    extentValueBox->setBounds(leftColumn.removeFromTop(372-135).reduced(5, 5));

    // right column
    // TODO - panningValueBox temp new position whilst metadata not implemented
    panningValueBox->setBounds(rightColumn.removeFromTop(219).reduced(5, 5));
    //metadataValueBox->setBounds(rightColumn.removeFromTop(161).reduced(5, 5));
    panningViewValueBox->setBounds(
        rightColumn.removeFromTop(358).reduced(5, 5));
  }

  void endButtonClicked(Onboarding* onboarding) override {
    onBoardingOverlay->setVisible(false);
    propertiesFile->setValue("showOnboarding", false);
  }
  void moreButtonClicked(Onboarding* onboarding) override {
    onBoardingOverlay->setVisible(false);
    URL(ear::plugin::detail::MORE_INFO_URL).launchInDefaultBrowser();
  }

  std::unique_ptr<ear::plugin::ui::EarHeader> header;
  std::unique_ptr<ear::plugin::ui::EarButton> onBoardingButton;
  std::unique_ptr<ear::plugin::ui::Overlay> onBoardingOverlay;
  std::unique_ptr<ear::plugin::ui::Onboarding> onBoardingContent;
  std::unique_ptr<ear::plugin::ui::ValueBoxMain> mainValueBox;
  std::unique_ptr<ear::plugin::ui::ValueBoxGain> gainValueBox;
  std::unique_ptr<ear::plugin::ui::ValueBoxPanning> panningValueBox;
  std::unique_ptr<ear::plugin::ui::ValueBoxMetadata> metadataValueBox;
  std::unique_ptr<ear::plugin::ui::ValueBoxExtent> extentValueBox;
  std::unique_ptr<ear::plugin::ui::ValueBoxPanningView> panningViewValueBox;
  std::unique_ptr<ToggleButton> oscToggle;
  std::unique_ptr<Label> oscProtocolLabel;
  std::unique_ptr<ComboBox> oscProtocolCombo;
  std::unique_ptr<Label> oscPortLabel;
  std::unique_ptr<Slider> oscPortSlider;
  std::unique_ptr<Label> oscSrcIndexLabel;
  std::unique_ptr<Slider> oscSrcIndexSlider;
  std::unique_ptr<Label> oscDebugLabel;
  std::shared_ptr<Label> statusBarLabel;
  Label versionLabel;
  // Unofficial-fork disclosure, not part of upstream EAR Production Suite.
  Label byLabel;

  std::unique_ptr<InterProcessLock> propertiesFileLock;
  std::unique_ptr<PropertiesFile> propertiesFile;

  std::optional<LevelMeter::MeterMode> lastSetMeterMode_;
  int lastSetMeterStartingChannel_{ 0 };
  int lastSetMeterChannelCount_{ 0 };

 private:
  ObjectsAudioProcessor* p_;

  TooltipWindow tooltipWindow{ this };
  TooltipLookAndFeel tooltipLookAndFeel;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ObjectsComponent)
};

}  // namespace ui
}  // namespace plugin
}  // namespace ear
