#pragma once

#include "JuceHeader.h"

#include "components/look_and_feel/colours.hpp"
#include "components/panner_side_view.hpp"
#include "components/panner_top_view.hpp"

namespace ear {
namespace plugin {
namespace ui {

/** @brief Draws two small, non-interactive markers for objects A/B over a
 * PannerTopView - geometry constants copied from PannerTopView::paint()
 * (outerDiameter_ there is private, not accessible from here) so the dots
 * land exactly where that view's own thumb would for the same az/distance.
 * A child of the PannerTopView it decorates, sized to match it exactly, so
 * getWidth()/getHeight() here already match the parent's centre. */
class TopViewSatellites : public Component {
 public:
  void setPoints(float azA, float azB, float distance) {
    azA_ = azA;
    azB_ = azB;
    distance_ = distance;
    repaint();
  }

  void paint(Graphics& g) override {
    const float outerDiameter = 205.f;
    const float dotSize = 22.f;
    const float innerDiameter = distance_ * outerDiameter;
    auto drawDot = [&](float azDeg, Colour colour, const String& label) {
      float azRad = azDeg / 180.f * MathConstants<float>::pi;
      float x = std::sin(azRad) * innerDiameter / 2.f;
      float y = std::cos(azRad) * innerDiameter / 2.f;
      juce::Rectangle<float> dotArea(getWidth() / 2.f - x - dotSize / 2.f,
                                     getHeight() / 2.f - y - dotSize / 2.f,
                                     dotSize, dotSize);
      g.setColour(colour);
      g.fillEllipse(dotArea);
      g.setColour(EarColours::Background);
      g.drawEllipse(dotArea, 1.5f);
      g.setColour(Colours::white);
      g.setFont(Font(dotSize * 0.55f, Font::bold));
      g.drawText(label, dotArea, Justification::centred);
    };
    drawDot(azA_, Colour(0xff2979ff), "L");
    drawDot(azB_, Colour(0xffff3b30), "R");
  }

 private:
  float azA_{0.f}, azB_{0.f}, distance_{1.f};
};

/** @brief As TopViewSatellites, over a PannerSideView (elevation only). */
class SideViewSatellites : public Component {
 public:
  void setPoints(float elA, float elB) {
    elA_ = elA;
    elB_ = elB;
    repaint();
  }

  void paint(Graphics& g) override {
    const float trackLength = 205.f;
    const float dotSize = 22.f;
    auto drawDot = [&](float elDeg, Colour colour, const String& label) {
      float y = getHeight() / 2.f - (elDeg / 90.f * trackLength / 2.f);
      juce::Rectangle<float> dotArea(getWidth() - dotSize * 1.6f, y - dotSize / 2.f,
                                     dotSize, dotSize);
      g.setColour(colour);
      g.fillEllipse(dotArea);
      g.setColour(EarColours::Background);
      g.drawEllipse(dotArea, 1.5f);
      g.setColour(Colours::white);
      g.setFont(Font(dotSize * 0.55f, Font::bold));
      g.drawText(label, dotArea, Justification::centred);
    };
    drawDot(elA_, Colour(0xff2979ff), "L");
    drawDot(elB_, Colour(0xffff3b30), "R");
  }

 private:
  float elA_{0.f}, elB_{0.f};
};

class ValueBoxPanningView : public Component {
 public:
  ValueBoxPanningView()
      : pannerTopView_(std::make_shared<PannerTopView>()),
        pannerSideView_(std::make_shared<PannerSideView>()),
        topSatellites_(std::make_unique<TopViewSatellites>()),
        sideSatellites_(std::make_unique<SideViewSatellites>()) {
    setColour(backgroundColourId, EarColours::Area01dp);

    addAndMakeVisible(pannerTopView_.get());
    addAndMakeVisible(pannerSideView_.get());

    topSatellites_->setInterceptsMouseClicks(false, false);
    sideSatellites_->setInterceptsMouseClicks(false, false);
    pannerTopView_->addAndMakeVisible(topSatellites_.get());
    pannerSideView_->addAndMakeVisible(sideSatellites_.get());
  }

  ~ValueBoxPanningView() {}

  void paint(Graphics& g) override { g.fillAll(findColour(backgroundColourId)); }

  enum ColourIds { backgroundColourId = 0x00010001 };

  void resized() override {
    auto area = getLocalBounds();
    area.reduce(10, 5);
    pannerTopView_->setBounds(area.removeFromLeft(275));
    pannerSideView_->setBounds(area);
    topSatellites_->setBounds(pannerTopView_->getLocalBounds());
    sideSatellites_->setBounds(pannerSideView_->getLocalBounds());
  }

  Value& getAzimuthValueObject() { return pannerTopView_->getAzimuthValueObject(); }
  Value& getElevationValueObject() { return pannerSideView_->getElevationValueObject(); }
  Value& getDistanceValueObject() { return pannerTopView_->getDistanceValueObject(); }

  std::shared_ptr<PannerTopView> getPannerTopView() { return pannerTopView_; }
  std::shared_ptr<PannerSideView> getPannerSideView() { return pannerSideView_; }

  void setSatellitePositions(float azA, float elA, float azB, float elB, float distance) {
    topSatellites_->setPoints(azA, azB, distance);
    sideSatellites_->setPoints(elA, elB);
  }

 private:
  std::shared_ptr<PannerTopView> pannerTopView_;
  std::shared_ptr<PannerSideView> pannerSideView_;
  std::unique_ptr<TopViewSatellites> topSatellites_;
  std::unique_ptr<SideViewSatellites> sideSatellites_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ValueBoxPanningView)
};

}  // namespace ui
}  // namespace plugin
}  // namespace ear
