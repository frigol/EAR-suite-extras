#include "reverb_point_controls_row.hpp"
#include "components/look_and_feel/colours.hpp"
#include <daw_channel_count.h>

using namespace ear::plugin::ui;

namespace {
void setupSlider(juce::Slider& s, double min, double max, double step,
                 double initial) {
  s.setSliderStyle(juce::Slider::LinearHorizontal);
  s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 16);
  s.setRange(min, max, step);
  s.setValue(initial, juce::dontSendNotification);
}
}  // namespace

ReverbPointControlsRow::ReverbPointControlsRow(ReverbAudioProcessor* p,
                                               int pointIndex)
    : p_(p), index_(pointIndex) {
  auto const& layout = p_->getPointLayout();
  auto const& pt = layout[static_cast<size_t>(index_)];
  auto params = p_->getPointParams(index_);

  addAndMakeVisible(nameLabel_);
  refreshName();

  routingCombo_.addItem("(none)", 1);
  for (int ch = 0; ch < MAX_DAW_CHANNELS; ++ch)
    routingCombo_.addItem("Channel " + juce::String(ch + 1), ch + 2);
  int currentRouting = p_->getPointRouting(index_);
  routingCombo_.setSelectedId(currentRouting < 0 ? 1 : currentRouting + 2,
                              juce::dontSendNotification);
  routingCombo_.onChange = [this] {
    int sel = routingCombo_.getSelectedId();
    p_->setPointRouting(index_, sel <= 1 ? -1 : sel - 2);
    if (onChanged) onChanged();
  };
  addAndMakeVisible(routingCombo_);

  setupSlider(azSlider_, -180.0, 180.0, 1.0, params.azimuthDeg);
  azSlider_.setDoubleClickReturnValue(true, pt.nominalAzimuthDeg);
  azSlider_.setTooltip("Azimuth in degrees (-180 to 180, 0 = front). Double-click to reset.");
  azSlider_.onValueChange = [this] {
    auto cur = p_->getPointParams(index_);
    p_->setPointPosition(index_, static_cast<float>(azSlider_.getValue()),
                         cur.elevationDeg, cur.distance);
    if (onChanged) onChanged();
  };

  setupSlider(elSlider_, -90.0, 90.0, 1.0, params.elevationDeg);
  elSlider_.setDoubleClickReturnValue(true, pt.nominalElevationDeg);
  elSlider_.setTooltip("Elevation in degrees (-90 to 90, 0 = horizon). Double-click to reset.");
  elSlider_.onValueChange = [this] {
    auto cur = p_->getPointParams(index_);
    p_->setPointPosition(index_, cur.azimuthDeg,
                         static_cast<float>(elSlider_.getValue()),
                         cur.distance);
    if (onChanged) onChanged();
  };

  setupSlider(distSlider_, 0.0, 10.0, 0.01, params.distance);
  distSlider_.setDoubleClickReturnValue(true, 1.0);
  distSlider_.setTooltip(
      "ADM distance, normalized (1.0 = unit sphere / nominal distance). "
      "Double-click to reset.");
  distSlider_.onValueChange = [this] {
    auto cur = p_->getPointParams(index_);
    p_->setPointPosition(index_, cur.azimuthDeg, cur.elevationDeg,
                         static_cast<float>(distSlider_.getValue()));
    if (onChanged) onChanged();
  };

  setupSlider(gainSlider_, 0.0, 2.0, 0.01, params.gain);
  gainSlider_.setDoubleClickReturnValue(true, 1.0);
  gainSlider_.setTooltip(
      "Linear gain multiplier sent to Scene as ADM metadata (1.0 = unity). "
      "Double-click to reset.");
  gainSlider_.onValueChange = [this] {
    p_->setPointGain(index_, static_cast<float>(gainSlider_.getValue()));
  };

  setupSlider(widthSlider_, 0.0, 1.0, 0.01, params.width);
  widthSlider_.setDoubleClickReturnValue(true, 0.0);
  widthSlider_.setTooltip("ADM object width, 0-1 fraction of the sphere. Double-click to reset.");
  widthSlider_.onValueChange = [this] {
    p_->setPointWidth(index_, static_cast<float>(widthSlider_.getValue()));
  };

  setupSlider(heightSlider_, 0.0, 1.0, 0.01, params.height);
  heightSlider_.setDoubleClickReturnValue(true, 0.0);
  heightSlider_.setTooltip("ADM object height, 0-1 fraction of the sphere. Double-click to reset.");
  heightSlider_.onValueChange = [this] {
    p_->setPointHeight(index_, static_cast<float>(heightSlider_.getValue()));
  };

  setupSlider(depthSlider_, 0.0, 1.0, 0.01, params.depth);
  depthSlider_.setDoubleClickReturnValue(true, 0.0);
  depthSlider_.setTooltip("ADM object depth, 0-1 fraction of the sphere. Double-click to reset.");
  depthSlider_.onValueChange = [this] {
    p_->setPointDepth(index_, static_cast<float>(depthSlider_.getValue()));
  };

  setupSlider(diffuseSlider_, 0.0, 1.0, 0.01, params.diffuse);
  diffuseSlider_.setDoubleClickReturnValue(true, 0.0);
  diffuseSlider_.setTooltip(
      "ADM diffuseness, 0 = point source, 1 = fully diffuse. Double-click to reset.");
  diffuseSlider_.onValueChange = [this] {
    p_->setPointDiffuse(index_, static_cast<float>(diffuseSlider_.getValue()));
  };

  for (auto* slider : {&azSlider_, &elSlider_, &distSlider_, &gainSlider_,
                       &widthSlider_, &heightSlider_, &depthSlider_,
                       &diffuseSlider_})
    addAndMakeVisible(*slider);

  resetButton_.onClick = [this] {
    p_->resetPointToNominal(index_);
    refreshFromProcessor();
    if (onChanged) onChanged();
  };
  addAndMakeVisible(resetButton_);

  setSize(1100, kPreferredHeight);
}

void ReverbPointControlsRow::refreshName() {
  auto const& pt = p_->getPointLayout()[static_cast<size_t>(index_)];
  nameLabel_.setText(
      p_->getPointDisplayName(index_) + "  (nominal " +
          juce::String(pt.nominalAzimuthDeg, 0) + " az, " +
          juce::String(pt.nominalElevationDeg, 0) + " el)",
      juce::dontSendNotification);
}

void ReverbPointControlsRow::refreshFromProcessor() {
  auto params = p_->getPointParams(index_);
  azSlider_.setValue(params.azimuthDeg, juce::dontSendNotification);
  elSlider_.setValue(params.elevationDeg, juce::dontSendNotification);
  distSlider_.setValue(params.distance, juce::dontSendNotification);
  gainSlider_.setValue(params.gain, juce::dontSendNotification);
  widthSlider_.setValue(params.width, juce::dontSendNotification);
  heightSlider_.setValue(params.height, juce::dontSendNotification);
  depthSlider_.setValue(params.depth, juce::dontSendNotification);
  diffuseSlider_.setValue(params.diffuse, juce::dontSendNotification);
}

void ReverbPointControlsRow::paint(juce::Graphics& g) {
  // Alternating background by point index, so a value in a crowded list
  // of 10 rows can be tracked back to its own row/name at a glance.
  g.fillAll((index_ % 2 == 0) ? EarColours::Area01dp
                              : EarColours::Area01dp.darker(0.15f));
}

void ReverbPointControlsRow::resized() {
  auto area = getLocalBounds();
  auto topRow = area.removeFromTop(kTopRowHeight);
  nameLabel_.setBounds(topRow.removeFromLeft(kNameWidth));
  topRow.removeFromLeft(8);
  routingCombo_.setBounds(topRow.removeFromLeft(kRoutingWidth));

  auto bottomRow = area.removeFromTop(kSliderRowHeight);
  for (auto* slider : {&azSlider_, &elSlider_, &distSlider_, &gainSlider_,
                       &widthSlider_, &heightSlider_, &depthSlider_,
                       &diffuseSlider_}) {
    slider->setBounds(bottomRow.removeFromLeft(kColumnWidth));
    bottomRow.removeFromLeft(kColumnGap);
  }
  bottomRow.removeFromLeft(8);
  resetButton_.setBounds(bottomRow.removeFromLeft(kResetWidth).reduced(0, 6));
}
