#pragma once

#include "JuceHeader.h"

#include "components/ear_combo_box.hpp"
#include "components/ear_name_text_editor.hpp"
#include "components/routing_info_icon.hpp"
#include "components/look_and_feel/colours.hpp"
#include "components/look_and_feel/fonts.hpp"
#include <daw_channel_count.h>

namespace ear {
namespace plugin {
namespace ui {

/** @brief Name/colour identity for the pair, plus TWO routing selectors -
 * one per derived object (A/B) - since this plugin drives two independent
 * Scene connections from one set of controls. */
class ValueBoxMain : public Component {
 public:
  ValueBoxMain()
      : colourComboBox_(std::make_shared<EarComboBox>()),
        name_(std::make_shared<EarNameTextEditor>()),
        useTrackNameCheckbox_(std::make_shared<ToggleButton>()),
        routingALabel_(std::make_unique<Label>()),
        routingAInfoIcon_(createRoutingInfoIcon()),
        routingAComboBox_(std::make_shared<EarComboBox>()),
        routingBLabel_(std::make_unique<Label>()),
        routingBInfoIcon_(createRoutingInfoIcon()),
        routingBComboBox_(std::make_shared<EarComboBox>()) {
    setColour(backgroundColourId, EarColours::Area04dp);

    name_->setLabelText("Object Name");
    name_->setText("ObjectPair_1");
    name_->setEnabled(false);
    name_->setAlpha(0.38f);
    addAndMakeVisible(name_.get());

    useTrackNameCheckbox_->setButtonText("Use track name");
    useTrackNameCheckbox_->setClickingTogglesState(false);
    addAndMakeVisible(useTrackNameCheckbox_.get());

    addChildComponent(routingAInfoIcon_.get());
    addChildComponent(routingBInfoIcon_.get());

    routingALabel_->setFont(EarFontsSingleton::instance().Label);
    routingALabel_->setText("Routing L", juce::NotificationType::dontSendNotification);
    routingALabel_->setColour(Label::textColourId, EarColours::Label);
    routingALabel_->setJustificationType(Justification::right);
    addAndMakeVisible(routingALabel_.get());
    routingAComboBox_->setDefaultText("Select Scene channel");
    for (int i = 1; i <= MAX_DAW_CHANNELS; ++i) routingAComboBox_->addTextEntry(String(i), i);
    addAndMakeVisible(routingAComboBox_.get());

    routingBLabel_->setFont(EarFontsSingleton::instance().Label);
    routingBLabel_->setText("Routing R", juce::NotificationType::dontSendNotification);
    routingBLabel_->setColour(Label::textColourId, EarColours::Label);
    routingBLabel_->setJustificationType(Justification::right);
    addAndMakeVisible(routingBLabel_.get());
    routingBComboBox_->setDefaultText("Select Scene channel");
    for (int i = 1; i <= MAX_DAW_CHANNELS; ++i) routingBComboBox_->addTextEntry(String(i), i);
    addAndMakeVisible(routingBComboBox_.get());

    for (auto colour : EarColours::Items) colourComboBox_->addColourEntry(colour);
    colourComboBox_->setEnabled(false);
    addAndMakeVisible(colourComboBox_.get());
  }

  ~ValueBoxMain() {}

  void paint(Graphics& g) override { g.fillAll(findColour(backgroundColourId)); }

  enum ColourIds { backgroundColourId = 0x00010001 };

  void resized() override {
    auto area = getLocalBounds();
    area.reduce(10, 5);

    area.removeFromTop(marginBig_);

    auto descArea = area.removeFromTop(90);
    auto colourArea = descArea.withWidth(labelWidth_);
    colourComboBox_->setBounds(colourArea.removeFromTop(63));
    auto nameArea = descArea.withTrimmedLeft(labelWidth_ + marginBig_);
    name_->setBounds(nameArea.removeFromTop(63).reduced(0, marginSmall_));
    useTrackNameCheckbox_->setBounds(nameArea);

    area.removeFromTop(15.f);

    auto routingAArea = area.removeFromTop(rowHeight_);
    routingALabel_->setBounds(routingAArea.withWidth(labelWidth_));
    if (routingAInfoIcon_->isVisible()) {
      placeRoutingInfoIconRight(routingAArea, routingAInfoIcon_.get());
      routingAArea.removeFromRight(marginSmall_);
    }
    routingAComboBox_->setBounds(
        routingAArea.withTrimmedLeft(labelWidth_ + marginBig_).reduced(0, marginSmall_));

    area.removeFromTop(marginSmall_);

    auto routingBArea = area.removeFromTop(rowHeight_);
    routingBLabel_->setBounds(routingBArea.withWidth(labelWidth_));
    if (routingBInfoIcon_->isVisible()) {
      placeRoutingInfoIconRight(routingBArea, routingBInfoIcon_.get());
      routingBArea.removeFromRight(marginSmall_);
    }
    routingBComboBox_->setBounds(
        routingBArea.withTrimmedLeft(labelWidth_ + marginBig_).reduced(0, marginSmall_));
  }

  std::shared_ptr<EarNameTextEditor> getNameTextEditor() { return name_; }
  std::shared_ptr<ToggleButton> getUseTrackNameCheckbox() { return useTrackNameCheckbox_; }
  std::shared_ptr<EarComboBox> getColourComboBox() { return colourComboBox_; }
  std::shared_ptr<EarComboBox> getRoutingAComboBox() { return routingAComboBox_; }
  std::shared_ptr<EarComboBox> getRoutingBComboBox() { return routingBComboBox_; }

  void setValidRoutingMax(int maxChVal) {
    routingAInfoIcon_->setVisible(maxChVal < 128);
    routingBInfoIcon_->setVisible(maxChVal < 128);
    for (auto* combo : {routingAComboBox_.get(), routingBComboBox_.get()}) {
      if (auto popup = combo->getPopup()) {
        for (int i = 1; i <= MAX_DAW_CHANNELS; ++i) {
          auto entry = popup->getEntryById(i);
          if (entry) entry->setSelectable(i <= maxChVal);
        }
      }
    }
  }

 private:
  const float labelWidth_ = 110.f;
  const float rowHeight_ = 36.f;
  const float marginSmall_ = 5.f;
  const float marginBig_ = 10.f;

  std::shared_ptr<EarComboBox> colourComboBox_;
  std::shared_ptr<EarNameTextEditor> name_;
  std::shared_ptr<ToggleButton> useTrackNameCheckbox_;
  std::unique_ptr<Label> routingALabel_;
  std::shared_ptr<ImageComponent> routingAInfoIcon_;
  std::shared_ptr<EarComboBox> routingAComboBox_;
  std::unique_ptr<Label> routingBLabel_;
  std::shared_ptr<ImageComponent> routingBInfoIcon_;
  std::shared_ptr<EarComboBox> routingBComboBox_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ValueBoxMain)
};

}  // namespace ui
}  // namespace plugin
}  // namespace ear
