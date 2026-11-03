#include "monitoring_cba_scene_view.hpp"
#include "binary_data.hpp"

using namespace ear::plugin::ui;

MonitoringCbaSceneView::MonitoringCbaSceneView(MonitoringCbaAudioProcessor* p)
    : p_(p) {
  rotateLeftButton_ = std::make_unique<EarButton>();
  rotateLeftButton_->setOffStateIcon(std::unique_ptr<juce::Drawable>(
      juce::Drawable::createFromImageData(binary_data::previous_icon_svg,
                                          binary_data::previous_icon_svgSize)));
  rotateLeftButton_->setOnStateIcon(std::unique_ptr<juce::Drawable>(
      juce::Drawable::createFromImageData(binary_data::previous_icon_svg,
                                          binary_data::previous_icon_svgSize)));
  rotateLeftButton_->onClick = [this] { rotate(90.0f); };
  addAndMakeVisible(*rotateLeftButton_);

  rotateRightButton_ = std::make_unique<EarButton>();
  rotateRightButton_->setOffStateIcon(std::unique_ptr<juce::Drawable>(
      juce::Drawable::createFromImageData(binary_data::next_icon_svg,
                                          binary_data::next_icon_svgSize)));
  rotateRightButton_->setOnStateIcon(std::unique_ptr<juce::Drawable>(
      juce::Drawable::createFromImageData(binary_data::next_icon_svg,
                                          binary_data::next_icon_svgSize)));
  rotateRightButton_->onClick = [this] { rotate(-90.0f); };
  addAndMakeVisible(*rotateRightButton_);
}

MonitoringCbaSceneView::~MonitoringCbaSceneView() { stopTimer(); }

void MonitoringCbaSceneView::refresh() { repaint(); }

void MonitoringCbaSceneView::drawDot(juce::Graphics& g, float azimuthDeg,
                                     float elevationDeg, float distance,
                                     juce::Colour colour) {
  float azimuth = azimuthDeg + currentViewingAngle_;

  float x = std::sin(juce::degreesToRadians(azimuth)) *
           std::cos(juce::degreesToRadians(elevationDeg)) * distance;
  float y = std::cos(-juce::degreesToRadians(azimuth)) *
           std::cos(juce::degreesToRadians(elevationDeg)) * distance;
  float z = std::sin(juce::degreesToRadians(elevationDeg)) * distance;

  float radiusScaled = kDotRadius * ((y + 4.0f) / 5.0f);
  float xProjected = x * sphereArea_.getWidth() / 2.0f;
  float yProjected = y * sphereArea_.getHeight() * kDepthFactor / 2.0f -
                     z * sphereArea_.getHeight() / 2.0f;

  juce::Rectangle<float> dotArea(0.0f, 0.0f, radiusScaled * 2.0f,
                                 radiusScaled * 2.0f);
  dotArea.translate(-radiusScaled, -radiusScaled);
  dotArea.translate(sphereArea_.getCentreX(), sphereArea_.getCentreY());
  dotArea.translate(xProjected, yProjected);

  g.setColour(colour.withAlpha(Emphasis::medium).darker(1.0f - (y + 1.0f) / 2.0f));
  g.fillEllipse(dotArea);
}

void MonitoringCbaSceneView::paint(juce::Graphics& g) {
  g.drawImageAt(cachedBackground_, 0, 0);

  for (auto const& slot : p_->getActiveInputSlots()) {
    auto info = p_->getInputSlotInfo(slot.slotIndex);
    drawDot(g, info.azimuthDeg, info.elevationDeg,
           juce::jlimit(0.0f, 1.0f, info.distance), juce::Colour(info.colour));
  }

  paintOrientationCompass(g, compassArea_);
}

int MonitoringCbaSceneView::getSectorIndex(float viewingAngleDeg) {
  int limited = static_cast<int>(std::round(viewingAngleDeg)) % 360;
  if (limited >= -315 && limited < -225) return 3;
  if (limited >= -225 && limited < -135) return 2;
  if (limited >= -135 && limited < -45) return 1;
  if (limited >= -45 && limited < 45) return 0;
  if (limited >= 45 && limited < 135) return 3;
  if (limited >= 135 && limited < 225) return 2;
  if (limited >= 225 && limited < 315) return 1;
  return 0;
}

void MonitoringCbaSceneView::paintOrientationCompass(juce::Graphics& g,
                                                     juce::Rectangle<float> area) {
  g.setColour(EarColours::Sphere);
  g.drawEllipse(area, 1.5f);
  auto flat = area.withSizeKeepingCentre(area.getWidth(),
                                         kDepthFactor * area.getHeight());
  g.drawEllipse(flat, 1.5f);

  g.setFont(EarFontsSingleton::instance().Description);
  g.setColour(EarColours::Label);
  std::vector<juce::String> labels = {
      "Front", juce::String::fromUTF8("+90\xc2\xb0"), "Back",
      juce::String::fromUTF8("-90\xc2\xb0")};
  int startIndex = getSectorIndex(endViewingAngle_);
  g.drawText(labels[static_cast<size_t>(startIndex)],
            flat.withY(flat.getY() - 16.0f).withHeight(14.0f),
            juce::Justification::centredBottom);
  g.drawText(labels[static_cast<size_t>((startIndex + 1) % 4)],
            juce::Rectangle<float>(area.getRight() + 4.0f, area.getCentreY() - 7.0f,
                                   50.0f, 14.0f),
            juce::Justification::left);
  g.drawText(labels[static_cast<size_t>((startIndex + 2) % 4)],
            flat.withY(flat.getBottom() + 2.0f).withHeight(14.0f),
            juce::Justification::centredTop);
  g.drawText(labels[static_cast<size_t>((startIndex + 3) % 4)],
            juce::Rectangle<float>(area.getX() - 54.0f, area.getCentreY() - 7.0f,
                                   50.0f, 14.0f),
            juce::Justification::right);
}

void MonitoringCbaSceneView::rebuildBackground() {
  if (getWidth() <= 0 || getHeight() <= 0) return;

  juce::Image img(juce::Image::PixelFormat::RGB, getWidth(), getHeight(), false);
  juce::Graphics g(img);

  auto area = getLocalBounds().toFloat();
  g.setColour(EarColours::Background);
  g.fillRect(area);

  float w = static_cast<float>(getWidth());
  float h = static_cast<float>(getHeight());
  g.setColour(EarColours::Sphere);

  float xPositionTop = w / 2.0f;
  float xPositionBottom = w / 2.0f;
  float distanceTop = w / 30.0f;
  float distanceBottom = w / 1.5f;
  for (int i = 0; i < 10; ++i) {
    g.drawLine(xPositionTop, 0.0f, xPositionBottom, h);
    distanceTop *= 0.9f;
    xPositionTop += distanceTop;
    distanceBottom *= 0.8f;
    xPositionBottom += distanceBottom;
  }
  xPositionTop = w / 2.0f;
  xPositionBottom = w / 2.0f;
  distanceTop = w / 20.0f;
  distanceBottom = w / 1.5f;
  for (int i = 1; i < 10; ++i) {
    g.drawLine(xPositionTop, 0.0f, xPositionBottom, h);
    distanceTop *= 0.9f;
    xPositionTop -= distanceTop;
    distanceBottom *= 0.8f;
    xPositionBottom -= distanceBottom;
  }

  float yPosition = (3.0f * h) / 4.0f;
  float yDistance = h / 4.0f;
  for (int i = 0; i < 15; ++i) {
    g.drawLine(0.0f, yPosition, w, yPosition);
    yDistance *= 0.68f;
    yPosition -= yDistance;
  }

  g.setGradientFill(juce::ColourGradient::vertical(
      EarColours::Background, EarColours::Background.withAlpha(0.0f),
      area.withTrimmedTop(h / 4.0f).withTrimmedBottom(h / 4.0f)));
  g.fillRect(area);

  area.removeFromBottom(70.0f);
  float sphereSize = juce::jmin(area.getWidth(), area.getHeight()) - 20.0f;
  sphereArea_ = area.withSizeKeepingCentre(sphereSize, sphereSize);

  g.setColour(EarColours::Sphere);
  g.drawEllipse(sphereArea_, 4.0f);
  auto flatSphere = sphereArea_.withSizeKeepingCentre(
      sphereArea_.getWidth(), kDepthFactor * sphereArea_.getHeight());
  g.drawEllipse(flatSphere, 4.0f);

  cachedBackground_ = img;
}

void MonitoringCbaSceneView::resized() {
  auto area = getLocalBounds().removeFromBottom(60).toFloat();
  auto buttonRow = area.removeFromTop(40.0f);
  rotateLeftButton_->setBounds(buttonRow.removeFromLeft(40.0f).toNearestInt());
  rotateRightButton_->setBounds(buttonRow.removeFromRight(40.0f).toNearestInt());

  compassArea_ = juce::Rectangle<float>(0.0f, 0.0f, 90.0f, 50.0f)
                     .withCentre(juce::Point<float>(getWidth() / 2.0f,
                                                     getHeight() - 38.0f));

  rebuildBackground();
}

void MonitoringCbaSceneView::rotate(float angleDeg) {
  if (isTimerRunning()) stopTimer();
  startViewingAngle_ = currentViewingAngle_;
  endViewingAngle_ += angleDeg;
  if (std::abs(endViewingAngle_ - startViewingAngle_) >= 1.0f)
    startTimerHz(50);
  else
    currentViewingAngle_ = endViewingAngle_;
  repaint();
}

void MonitoringCbaSceneView::timerCallback() {
  updateViewingAngle();
  repaint();
}

void MonitoringCbaSceneView::updateViewingAngle() {
  float range = endViewingAngle_ - startViewingAngle_;
  currentViewingAngle_ += range / 10.0f;
  if ((range >= 0.0f && currentViewingAngle_ > endViewingAngle_) ||
      (range < 0.0f && currentViewingAngle_ < endViewingAngle_)) {
    currentViewingAngle_ = endViewingAngle_;
    stopTimer();
  }
}
