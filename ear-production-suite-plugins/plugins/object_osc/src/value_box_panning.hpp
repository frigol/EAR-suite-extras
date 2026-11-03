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

// Az/El/Dist <-> X/Y/Z toggle (Control-GRIS style): the x/y/z sliders are
// purely a display/edit convenience local to this box - they drive the
// real azSlider_/elSlider_/distSlider_ (which stay the actual source of
// truth wired to the processor by ObjectsJuceFrontendConnector) rather
// than attaching to the processor themselves. Conversion uses the same
// sign convention as ObjectsAudioProcessor's applyCartesian() (ADM
// azimuth: 0=front, positive=left; y=front axis, x=right axis, z=up),
// just inverted for display:
//   horiz = dist * cos(el);  x = -horiz * sin(az);  y = horiz * cos(az);
//   z = dist * sin(el)
class ValueBoxPanning : public Component, private Timer {
 public:
  ValueBoxPanning()
      : headingLabel_(std::make_unique<Label>()),
        azLabel_(std::make_unique<Label>()),
        azSlider_(std::make_shared<EarInvertedSlider>()),
        elLabel_(std::make_unique<Label>()),
        elSlider_(std::make_shared<EarSlider>()),
        distLabel_(std::make_unique<Label>()),
        distSlider_(std::make_shared<EarSlider>()),
        xLabel_(std::make_unique<Label>()),
        xSlider_(std::make_shared<EarSlider>()),
        yLabel_(std::make_unique<Label>()),
        ySlider_(std::make_shared<EarSlider>()),
        zLabel_(std::make_unique<Label>()),
        zSlider_(std::make_shared<EarSlider>()) {
    setColour(backgroundColourId, EarColours::Area01dp);

    headingLabel_->setFont(EarFontsSingleton::instance().Heading);
    headingLabel_->setColour(Label::textColourId, EarColours::Heading);
    headingLabel_->setText("Panning",
                           juce::NotificationType::dontSendNotification);
    headingLabel_->setJustificationType(Justification::bottomLeft);
    addAndMakeVisible(headingLabel_.get());

    // XYZ mode is dormant for now (by request) - only the original AED
    // display is exposed. xyzMode_ stays permanently false, so resized()
    // always lays out az/el/dist; updateXyzFromPolar()/pushXyzToPolar() are
    // kept intact (dead code, not deleted) to re-enable later by restoring
    // the toggle button below.
    //
    // modeToggleButton_.setButtonText("X Y Z");
    // modeToggleButton_.setTooltip(
    //     "Switch between Azimuth/Elevation/Distance and X/Y/Z cartesian "
    //     "display for editing this point's position (Control GRIS style).");
    // modeToggleButton_.onClick = [this] {
    //   xyzMode_ = !xyzMode_;
    //   modeToggleButton_.setButtonText(xyzMode_ ? "Az El Dist" : "X Y Z");
    //   if (xyzMode_) {
    //     updateXyzFromPolar();
    //     startTimerHz(20);
    //   } else {
    //     stopTimer();
    //   }
    //   resized();
    // };
    // addAndMakeVisible(modeToggleButton_);

    azLabel_->setFont(EarFontsSingleton::instance().Label);
    azLabel_->setColour(Label::textColourId, EarColours::Label);
    azLabel_->setText("Azimuth", juce::NotificationType::dontSendNotification);
    azLabel_->setJustificationType(Justification::bottomRight);
    addAndMakeVisible(azLabel_.get());
    elLabel_->setFont(EarFontsSingleton::instance().Label);
    elLabel_->setColour(Label::textColourId, EarColours::Label);
    elLabel_->setText("Elevation",
                      juce::NotificationType::dontSendNotification);
    elLabel_->setJustificationType(Justification::bottomRight);
    addAndMakeVisible(elLabel_.get());
    distLabel_->setFont(EarFontsSingleton::instance().Label);
    distLabel_->setColour(Label::textColourId, EarColours::Label);
    distLabel_->setText("Distance",
                        juce::NotificationType::dontSendNotification);
    distLabel_->setJustificationType(Justification::bottomRight);
    addAndMakeVisible(distLabel_.get());

    azSlider_->setTicks(std::vector<Tick>{
        {String::fromUTF8("–180"), -180.f, Justification::right},
        {String::fromUTF8("0"), 0.f, Justification::centred},
        {String::fromUTF8("+180"), 180.f, Justification::left}});
    azSlider_->setUnit("Degree");
    azSlider_->textFromValueFunction = [&](double val) {
      return String(val, 1).replaceCharacters(StringRef(String::fromUTF8("-")),
                                              StringRef(String::fromUTF8("–")));
    };
    azSlider_->valueFromTextFunction = [&](const String& text) {
      return text
          .replaceCharacters(StringRef(String::fromUTF8("–")),
                             StringRef(String::fromUTF8("-")))
          .retainCharacters("0123456789.,-+")
          .getDoubleValue();
    };
    azSlider_->setTextBoxStyle(Slider::TextBoxRight, false, 44, 40);
    azSlider_->setRange(-180.f, 180.f);
    azSlider_->setNumDecimalPlacesToDisplay(1);
    azSlider_->setDoubleClickReturnValue(true, 0.f);
    addAndMakeVisible(azSlider_.get());

    elSlider_->setTicks(std::vector<Tick>{
        {String::fromUTF8("–90"), -90.f, Justification::left},
        {String::fromUTF8("0"), 0.f, Justification::centred},
        {String::fromUTF8("+90"), 90.f, Justification::right}});
    elSlider_->setUnit("Degree");
    elSlider_->textFromValueFunction = [&](double val) {
      return String(val, 1).replaceCharacters(StringRef(String::fromUTF8("-")),
                                              StringRef(String::fromUTF8("–")));
    };
    elSlider_->valueFromTextFunction = [&](const String& text) {
      return text
          .replaceCharacters(StringRef(String::fromUTF8("–")),
                             StringRef(String::fromUTF8("-")))
          .retainCharacters("0123456789.,-+")
          .getDoubleValue();
    };
    elSlider_->setTextBoxStyle(Slider::TextBoxRight, false, 44, 40);
    elSlider_->setRange(-90.f, 90.f);
    elSlider_->setNumDecimalPlacesToDisplay(0);
    elSlider_->setDoubleClickReturnValue(true, 0.f);
    addAndMakeVisible(elSlider_.get());

    // Range extends to 2.0, not 1.0 - see the comment on distance_'s range
    // in object_plugin_processor.cpp (CUBE pad mode can report a real
    // distance up to sqrt(2) toward a room corner).
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

    xLabel_->setFont(EarFontsSingleton::instance().Label);
    xLabel_->setColour(Label::textColourId, EarColours::Label);
    xLabel_->setText("X", juce::NotificationType::dontSendNotification);
    xLabel_->setJustificationType(Justification::bottomRight);
    addChildComponent(xLabel_.get());
    yLabel_->setFont(EarFontsSingleton::instance().Label);
    yLabel_->setColour(Label::textColourId, EarColours::Label);
    yLabel_->setText("Y", juce::NotificationType::dontSendNotification);
    yLabel_->setJustificationType(Justification::bottomRight);
    addChildComponent(yLabel_.get());
    zLabel_->setFont(EarFontsSingleton::instance().Label);
    zLabel_->setColour(Label::textColourId, EarColours::Label);
    zLabel_->setText("Z", juce::NotificationType::dontSendNotification);
    zLabel_->setJustificationType(Justification::bottomRight);
    addChildComponent(zLabel_.get());

    for (auto* slider : {xSlider_.get(), ySlider_.get(), zSlider_.get()}) {
      slider->setTicks(std::vector<Tick>{
          {String::fromUTF8("–1"), -1.f, Justification::left},
          {String::fromUTF8("0"), 0.f, Justification::centred},
          {String::fromUTF8("+1"), 1.f, Justification::right}});
      slider->setTextBoxStyle(Slider::TextBoxRight, false, 44, 40);
      slider->setRange(-1.f, 1.f);
      slider->setNumDecimalPlacesToDisplay(2);
      slider->setDoubleClickReturnValue(true, 0.f);
      addChildComponent(slider);
    }
    zSlider_->setDoubleClickReturnValue(true, 0.f);

    xSlider_->onValueChange = [this] { pushXyzToPolar(); };
    ySlider_->onValueChange = [this] { pushXyzToPolar(); };
    zSlider_->onValueChange = [this] { pushXyzToPolar(); };

    updateXyzFromPolar();
  }

  ~ValueBoxPanning() { stopTimer(); }

  void paint(Graphics& g) override {
    // background
    g.fillAll(findColour(backgroundColourId));
  }

  enum ColourIds {
    backgroundColourId = 0x00010001,
  };

  void resized() override {
    auto area = getLocalBounds();
    area.reduce(10, 5);
    auto headingArea = area.removeFromTop(30);
    modeToggleButton_.setBounds(
        headingArea.removeFromRight(toggleWidth_).reduced(0, 3));
    headingArea.removeFromRight(marginBig_);
    headingLabel_->setBounds(headingArea);

    area.removeFromTop(2.f * marginBig_);

    float sliderWidth = area.getWidth() - labelWidth_ - marginBig_;

    auto azArea = area.removeFromTop(rowHeight_);
    auto azLabelArea =
        azArea.withWidth(labelWidth_).withTrimmedBottom(labelPaddingBottom_);
    auto azSliderArea = azArea.withTrimmedLeft(labelWidth_ + marginBig_);

    area.removeFromTop(marginBig_);

    auto elArea = area.removeFromTop(rowHeight_);
    auto elLabelArea =
        elArea.withWidth(labelWidth_).withTrimmedBottom(labelPaddingBottom_);
    auto elSliderArea = elArea.withTrimmedLeft(labelWidth_ + marginBig_);

    area.removeFromTop(marginBig_);

    auto distArea = area.removeFromTop(rowHeight_);
    auto distLabelArea =
        distArea.withWidth(labelWidth_).withTrimmedBottom(labelPaddingBottom_);
    auto distSliderArea = distArea.withTrimmedLeft(labelWidth_ + marginBig_);

    azLabel_->setVisible(!xyzMode_);
    azSlider_->setVisible(!xyzMode_);
    elLabel_->setVisible(!xyzMode_);
    elSlider_->setVisible(!xyzMode_);
    distLabel_->setVisible(!xyzMode_);
    distSlider_->setVisible(!xyzMode_);
    xLabel_->setVisible(xyzMode_);
    xSlider_->setVisible(xyzMode_);
    yLabel_->setVisible(xyzMode_);
    ySlider_->setVisible(xyzMode_);
    zLabel_->setVisible(xyzMode_);
    zSlider_->setVisible(xyzMode_);

    if (!xyzMode_) {
      azLabel_->setBounds(azLabelArea);
      azSlider_->setBounds(azSliderArea);
      elLabel_->setBounds(elLabelArea);
      elSlider_->setBounds(elSliderArea);
      distLabel_->setBounds(distLabelArea);
      distSlider_->setBounds(distSliderArea);
    } else {
      xLabel_->setBounds(azLabelArea);
      xSlider_->setBounds(azSliderArea);
      yLabel_->setBounds(elLabelArea);
      ySlider_->setBounds(elSliderArea);
      zLabel_->setBounds(distLabelArea);
      zSlider_->setBounds(distSliderArea);
    }
  }

  std::shared_ptr<EarInvertedSlider> getAzimuthSlider() { return azSlider_; }
  std::shared_ptr<EarSlider> getElevationSlider() { return elSlider_; }
  std::shared_ptr<EarSlider> getDistanceSlider() { return distSlider_; }

 private:
  // Recomputes the x/y/z display sliders from the current (real)
  // az/el/dist slider values. Called on toggle-on and on each timer tick
  // while in XYZ mode, so externally-pushed polar changes (OSC, other UI)
  // stay reflected - skips a slider currently being dragged so it isn't
  // fought mid-gesture.
  void updateXyzFromPolar() {
    if (xSlider_->isMouseButtonDown() || ySlider_->isMouseButtonDown() ||
        zSlider_->isMouseButtonDown())
      return;
    double azRad = degreesToRadians(azSlider_->getValue());
    double elRad = degreesToRadians(elSlider_->getValue());
    double dist = distSlider_->getValue();
    double horiz = dist * std::cos(elRad);
    double x = -horiz * std::sin(azRad);
    double y = horiz * std::cos(azRad);
    double z = dist * std::sin(elRad);
    xSlider_->setValue(x, dontSendNotification);
    ySlider_->setValue(y, dontSendNotification);
    zSlider_->setValue(z, dontSendNotification);
  }

  // Inverse of updateXyzFromPolar(): applied whenever the user edits an
  // x/y/z slider directly, pushed into the REAL az/el/dist sliders with
  // sendNotification so the existing listener already attached by
  // ObjectsJuceFrontendConnector (via setAzimuthSlider()/etc.) picks it
  // up and applies it to the processor - this box never talks to the
  // processor directly.
  void pushXyzToPolar() {
    double x = xSlider_->getValue();
    double y = ySlider_->getValue();
    double z = zSlider_->getValue();
    double horiz = std::sqrt(x * x + y * y);
    double azDeg = radiansToDegrees(std::atan2(-x, y));
    double elDeg = radiansToDegrees(std::atan2(z, horiz));
    double dist = std::sqrt(x * x + y * y + z * z);
    azSlider_->setValue(azDeg, sendNotificationSync);
    elSlider_->setValue(elDeg, sendNotificationSync);
    distSlider_->setValue(dist, sendNotificationSync);
  }

  void timerCallback() override { updateXyzFromPolar(); }

  std::unique_ptr<Label> headingLabel_;
  TextButton modeToggleButton_;
  bool xyzMode_ = false;
  std::unique_ptr<Label> azLabel_;
  std::shared_ptr<EarInvertedSlider> azSlider_;
  std::unique_ptr<Label> elLabel_;
  std::shared_ptr<EarSlider> elSlider_;
  std::unique_ptr<Label> distLabel_;
  std::shared_ptr<EarSlider> distSlider_;
  std::unique_ptr<Label> xLabel_;
  std::shared_ptr<EarSlider> xSlider_;
  std::unique_ptr<Label> yLabel_;
  std::shared_ptr<EarSlider> ySlider_;
  std::unique_ptr<Label> zLabel_;
  std::shared_ptr<EarSlider> zSlider_;

  const float labelWidth_ = 71.f;
  const float labelPaddingBottom_ = 0.f;
  const float rowHeight_ = 40.f;
  const float marginSmall_ = 5.f;
  const float marginBig_ = 10.f;
  const float toggleWidth_ = 90.f;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ValueBoxPanning)
};

}  // namespace ui
}  // namespace plugin
}  // namespace ear
