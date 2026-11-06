#pragma once

#include "JuceHeader.h"

#include <cmath>
#include "components/ear_slider.hpp"
#include "components/ear_inverted_slider.hpp"
#include "components/look_and_feel/colours.hpp"
#include "components/look_and_feel/fonts.hpp"

namespace ear {
namespace plugin {
namespace ui {

/** @brief Centre Azimuth/Elevation/Distance, plus Pair Distance (angular
 * separation between the two objects) and Pair Rotation (the separation
 * axis's roll - 0 = horizontal L/R, 90 = vertical up/down). */
class ValueBoxPanning : public Component {
 public:
  ValueBoxPanning()
      : headingLabel_(std::make_unique<Label>()),
        azLabel_(std::make_unique<Label>()),
        azSlider_(std::make_shared<EarInvertedSlider>()),
        elLabel_(std::make_unique<Label>()),
        elSlider_(std::make_shared<EarSlider>()),
        distLabel_(std::make_unique<Label>()),
        distSlider_(std::make_shared<EarSlider>()),
        pairDistLabel_(std::make_unique<Label>()),
        pairDistSlider_(std::make_shared<EarSlider>()),
        pairRotLabel_(std::make_unique<Label>()),
        pairRotSlider_(std::make_shared<EarSlider>()) {
    setColour(backgroundColourId, EarColours::Area01dp);

    headingLabel_->setFont(EarFontsSingleton::instance().Heading);
    headingLabel_->setColour(Label::textColourId, EarColours::Heading);
    headingLabel_->setText("Panning", juce::NotificationType::dontSendNotification);
    headingLabel_->setJustificationType(Justification::bottomLeft);
    addAndMakeVisible(headingLabel_.get());

    auto setupLabel = [](Label* l, const char* text) {
      l->setFont(EarFontsSingleton::instance().Label);
      l->setColour(Label::textColourId, EarColours::Label);
      l->setText(text, juce::NotificationType::dontSendNotification);
      l->setJustificationType(Justification::bottomRight);
    };
    setupLabel(azLabel_.get(), "Azimuth");
    addAndMakeVisible(azLabel_.get());
    setupLabel(elLabel_.get(), "Elevation");
    addAndMakeVisible(elLabel_.get());
    setupLabel(distLabel_.get(), "Distance");
    addAndMakeVisible(distLabel_.get());
    setupLabel(pairDistLabel_.get(), "Pair Distance");
    addAndMakeVisible(pairDistLabel_.get());
    setupLabel(pairRotLabel_.get(), "Pair Rotation");
    addAndMakeVisible(pairRotLabel_.get());

    azSlider_->setTicks(std::vector<Tick>{
        {String::fromUTF8("\xe2\x80\x93""180"), -180.f, Justification::right},
        {String::fromUTF8("0"), 0.f, Justification::centred},
        {String::fromUTF8("+180"), 180.f, Justification::left}});
    azSlider_->setUnit("Degree");
    azSlider_->setTextBoxStyle(Slider::TextBoxRight, false, 44, 40);
    azSlider_->setRange(-180.f, 180.f);
    azSlider_->setNumDecimalPlacesToDisplay(1);
    azSlider_->setDoubleClickReturnValue(true, 0.f);
    addAndMakeVisible(azSlider_.get());

    elSlider_->setTicks(std::vector<Tick>{
        {String::fromUTF8("\xe2\x80\x93""90"), -90.f, Justification::left},
        {String::fromUTF8("0"), 0.f, Justification::centred},
        {String::fromUTF8("+90"), 90.f, Justification::right}});
    elSlider_->setUnit("Degree");
    elSlider_->setTextBoxStyle(Slider::TextBoxRight, false, 44, 40);
    elSlider_->setRange(-90.f, 90.f);
    elSlider_->setNumDecimalPlacesToDisplay(0);
    elSlider_->setDoubleClickReturnValue(true, 0.f);
    addAndMakeVisible(elSlider_.get());

    distSlider_->setTicks(std::vector<Tick>{
        {String::fromUTF8("0"), 0.f, Justification::left},
        {String::fromUTF8("1"), 1.f, Justification::centred},
        {String::fromUTF8("2"), 2.f, Justification::right}});
    distSlider_->setTextBoxStyle(Slider::TextBoxRight, false, 44, 40);
    distSlider_->setRange(0.f, 2.f);
    distSlider_->setNumDecimalPlacesToDisplay(2);
    distSlider_->setDoubleClickReturnValue(true, 1.f);
    distSlider_->setValue(1.f);
    addAndMakeVisible(distSlider_.get());

    pairDistSlider_->setTicks(std::vector<Tick>{
        {String::fromUTF8("0"), 0.f, Justification::left},
        {String::fromUTF8("90"), 90.f, Justification::centred},
        {String::fromUTF8("180"), 180.f, Justification::right}});
    pairDistSlider_->setUnit("Degree");
    pairDistSlider_->setTextBoxStyle(Slider::TextBoxRight, false, 44, 40);
    pairDistSlider_->setRange(0.f, 180.f);
    pairDistSlider_->setNumDecimalPlacesToDisplay(1);
    pairDistSlider_->setDoubleClickReturnValue(true, 30.f);
    pairDistSlider_->setValue(30.f);
    addAndMakeVisible(pairDistSlider_.get());

    // Not inverted like azSlider_ - normal slider, so ticks use the normal
    // convention (same as elSlider_/pairDistSlider_): leftmost tick text
    // extends rightward from the left edge, rightmost tick text extends
    // leftward from the right edge, away from the value textbox. Using
    // azSlider_'s (inverted-slider) convention here put "+180" to the right
    // of its tick mark, straight into the value textbox.
    pairRotSlider_->setTicks(std::vector<Tick>{
        {String::fromUTF8("\xe2\x80\x93""180"), -180.f, Justification::left},
        {String::fromUTF8("0"), 0.f, Justification::centred},
        {String::fromUTF8("+180"), 180.f, Justification::right}});
    pairRotSlider_->setUnit("Degree");
    pairRotSlider_->setTextBoxStyle(Slider::TextBoxRight, false, 44, 40);
    pairRotSlider_->setRange(-180.f, 180.f);
    pairRotSlider_->setNumDecimalPlacesToDisplay(1);
    pairRotSlider_->setDoubleClickReturnValue(true, 0.f);
    addAndMakeVisible(pairRotSlider_.get());
  }

  ~ValueBoxPanning() {}

  void paint(Graphics& g) override { g.fillAll(findColour(backgroundColourId)); }

  enum ColourIds { backgroundColourId = 0x00010001 };

  void resized() override {
    auto area = getLocalBounds();
    area.reduce(10, 5);
    auto headingArea = area.removeFromTop(30);
    headingLabel_->setBounds(headingArea);

    area.removeFromTop(marginBig_);

    // Divide whatever height remains evenly across all 5 rows, rather than
    // a fixed row height that can overflow (and silently shortchange the
    // last row) if the parent ever gives this box less space than assumed.
    constexpr int kNumRows = 5;
    float rowH = area.getHeight() / static_cast<float>(kNumRows);

    auto layoutRow = [&](Label* label, Component* slider) {
      auto row = area.removeFromTop(static_cast<int>(std::round(rowH)));
      auto sliderRow = row.reduced(0, static_cast<int>(marginBig_ / 2.f));
      label->setBounds(row.withWidth(labelWidth_));
      slider->setBounds(sliderRow.withTrimmedLeft(labelWidth_ + marginBig_));
    };

    layoutRow(azLabel_.get(), azSlider_.get());
    layoutRow(elLabel_.get(), elSlider_.get());
    layoutRow(distLabel_.get(), distSlider_.get());
    layoutRow(pairDistLabel_.get(), pairDistSlider_.get());
    layoutRow(pairRotLabel_.get(), pairRotSlider_.get());
  }

  std::shared_ptr<EarInvertedSlider> getAzimuthSlider() { return azSlider_; }
  std::shared_ptr<EarSlider> getElevationSlider() { return elSlider_; }
  std::shared_ptr<EarSlider> getDistanceSlider() { return distSlider_; }
  std::shared_ptr<EarSlider> getPairDistanceSlider() { return pairDistSlider_; }
  std::shared_ptr<EarSlider> getPairRotationSlider() { return pairRotSlider_; }

 private:
  std::unique_ptr<Label> headingLabel_;
  std::unique_ptr<Label> azLabel_;
  std::shared_ptr<EarInvertedSlider> azSlider_;
  std::unique_ptr<Label> elLabel_;
  std::shared_ptr<EarSlider> elSlider_;
  std::unique_ptr<Label> distLabel_;
  std::shared_ptr<EarSlider> distSlider_;
  std::unique_ptr<Label> pairDistLabel_;
  std::shared_ptr<EarSlider> pairDistSlider_;
  std::unique_ptr<Label> pairRotLabel_;
  std::shared_ptr<EarSlider> pairRotSlider_;

  const float labelWidth_ = 95.f;
  const float rowHeight_ = 40.f;
  const float marginBig_ = 14.f;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ValueBoxPanning)
};

}  // namespace ui
}  // namespace plugin
}  // namespace ear
