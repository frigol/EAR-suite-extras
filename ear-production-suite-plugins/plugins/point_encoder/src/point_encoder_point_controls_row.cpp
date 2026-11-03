#include "point_encoder_point_controls_row.hpp"
#include "components/look_and_feel/colours.hpp"
#include <daw_channel_count.h>

using namespace ear::plugin::ui;

namespace {
void setupSlider(juce::Slider& s, double min, double max, double step,
                 double initial) {
  s.setSliderStyle(juce::Slider::LinearHorizontal);
  s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 16);
  s.setScrollWheelEnabled(false);
  s.setRange(min, max, step);
  s.setValue(initial, juce::dontSendNotification);
}

// Wires a small numeric TextEditor so typing a channel number and pressing
// Enter (or clicking away) jumps `combo` straight to the matching item -
// see the algorithmic Reverb's own identical helper
// (reverb_point_controls_row.cpp) for the full explanation.
void setupChannelJumpField(juce::TextEditor& field, juce::ComboBox& combo,
                           std::function<int(int)> numberToId) {
  field.setInputRestrictions(3, "0123456789");
  field.setTextToShowWhenEmpty("#", juce::Colours::grey);
  field.setJustification(juce::Justification::centred);
  field.setTooltip("Type a channel number and press Enter to jump straight to it.");
  auto jump = [&field, &combo, numberToId] {
    if (field.getText().isEmpty()) return;
    int id = numberToId(field.getText().getIntValue());
    if (id > 0) combo.setSelectedId(id, juce::sendNotification);
    field.clear();
  };
  field.onReturnKey = jump;
  field.onFocusLost = jump;
}
}  // namespace

PointEncoderPointControlsRow::PointEncoderPointControlsRow(
    PointEncoderAudioProcessor* p, int pointIndex)
    : p_(p), index_(pointIndex) {
  auto st = p_->getPointState(index_);

  activeToggle_.setToggleState(st.active, juce::dontSendNotification);
  activeToggle_.onClick = [this] {
    p_->setPointActive(index_, activeToggle_.getToggleState());
    if (onChanged) onChanged();
  };
  addAndMakeVisible(activeToggle_);

  nameEditor_.setText(st.name, juce::dontSendNotification);
  nameEditor_.onFocusLost = [this] {
    p_->setPointName(index_, nameEditor_.getText());
    if (onChanged) onChanged();
  };
  nameEditor_.onReturnKey = [this] {
    p_->setPointName(index_, nameEditor_.getText());
    if (onChanged) onChanged();
  };
  addAndMakeVisible(nameEditor_);

  for (int ch = 0; ch < PointEncoderAudioProcessor::kMaxPoints; ++ch)
    channelCombo_.addItem("Input ch " + juce::String(ch + 1), ch + 1);
  channelCombo_.setSelectedId(st.inputChannel + 1, juce::dontSendNotification);
  channelCombo_.setTooltip(
      "Which of this plugin's own input channels feeds this point's audio "
      "- independent of the point's own slot number, so several points "
      "can be re-mapped to wherever the audio actually lands after "
      "REAPER's own routing/summing.");
  channelCombo_.onChange = [this] {
    p_->setPointInputChannel(index_, channelCombo_.getSelectedId() - 1);
    if (onChanged) onChanged();
  };
  addAndMakeVisible(channelCombo_);
  setupChannelJumpField(
      channelJumpField_, channelCombo_,
      [](int n) { return (n >= 1 && n <= PointEncoderAudioProcessor::kMaxPoints) ? n : -1; });
  addAndMakeVisible(channelJumpField_);

  // Lists all MAX_DAW_CHANNELS (128, REAPER's own routing cap) channels,
  // not just this plugin's own kMaxPoints - same convention the stock EAR
  // Object plugin and the algorithmic Reverb's own (now-retired) routing
  // combo used: the channel Scene should read is wherever REAPER's own
  // track routing places this point's audio, which can be any channel on
  // the feeding track, independent of this plugin's own 24-wide bus.
  earChannelCombo_.addItem("EAR ch: (none)", 1);
  for (int ch = 0; ch < MAX_DAW_CHANNELS; ++ch)
    earChannelCombo_.addItem("EAR ch " + juce::String(ch + 1), ch + 2);
  earChannelCombo_.setSelectedId(st.earChannel < 0 ? 1 : st.earChannel + 2,
                                 juce::dontSendNotification);
  earChannelCombo_.setTooltip(
      "Which channel this point reports itself on to EAR Scene - set this "
      "to match wherever REAPER's own routing actually places this "
      "point's audio on the track feeding Scene (not necessarily the same "
      "number as \"Input ch\" above, which is this plugin's own input "
      "mapping). The point is not sent to Scene at all until this is set.");
  earChannelCombo_.onChange = [this] {
    int sel = earChannelCombo_.getSelectedId();
    p_->setPointEarChannel(index_, sel <= 1 ? -1 : sel - 2);
    if (onChanged) onChanged();
  };
  addAndMakeVisible(earChannelCombo_);
  setupChannelJumpField(earChannelJumpField_, earChannelCombo_,
                        [](int n) { return (n >= 1 && n <= MAX_DAW_CHANNELS) ? n + 1 : -1; });
  addAndMakeVisible(earChannelJumpField_);

  setupSlider(azSlider_, -180.0, 180.0, 1.0, st.azimuthDeg);
  azSlider_.setDoubleClickReturnValue(true, 0.0);
  azSlider_.setTooltip(
      "Azimuth in degrees (-180 to 180, 0 = front, positive = left). "
      "Double-click to reset.");
  azSlider_.onValueChange = [this] {
    auto cur = p_->getPointState(index_);
    p_->setPointPosition(index_, static_cast<float>(azSlider_.getValue()),
                         cur.elevationDeg, cur.distance);
    if (onChanged) onChanged();
  };

  setupSlider(elSlider_, -90.0, 90.0, 1.0, st.elevationDeg);
  elSlider_.setDoubleClickReturnValue(true, 0.0);
  elSlider_.setTooltip("Elevation in degrees (-90 to 90, 0 = horizon). Double-click to reset.");
  elSlider_.onValueChange = [this] {
    auto cur = p_->getPointState(index_);
    p_->setPointPosition(index_, cur.azimuthDeg,
                         static_cast<float>(elSlider_.getValue()),
                         cur.distance);
    if (onChanged) onChanged();
  };

  setupSlider(distSlider_, 0.0, 10.0, 0.01, st.distance);
  distSlider_.setDoubleClickReturnValue(true, 1.0);
  distSlider_.setTooltip("ADM distance, normalized (1.0 = unit sphere). Double-click to reset.");
  distSlider_.onValueChange = [this] {
    auto cur = p_->getPointState(index_);
    p_->setPointPosition(index_, cur.azimuthDeg, cur.elevationDeg,
                         static_cast<float>(distSlider_.getValue()));
    if (onChanged) onChanged();
  };

  setupSlider(gainSlider_, 0.0, 2.0, 0.01, st.gain);
  gainSlider_.setDoubleClickReturnValue(true, 1.0);
  gainSlider_.setTooltip(
      "Linear gain multiplier sent to Scene as ADM metadata (1.0 = unity). "
      "Double-click to reset.");
  gainSlider_.onValueChange = [this] {
    p_->setPointGain(index_, static_cast<float>(gainSlider_.getValue()));
  };

  setupSlider(widthSlider_, 0.0, 1.0, 0.01, st.width);
  widthSlider_.setDoubleClickReturnValue(true, 0.0);
  widthSlider_.setTooltip("ADM object width, 0-1 fraction of the sphere. Double-click to reset.");
  widthSlider_.onValueChange = [this] {
    p_->setPointWidth(index_, static_cast<float>(widthSlider_.getValue()));
  };

  setupSlider(heightSlider_, 0.0, 1.0, 0.01, st.height);
  heightSlider_.setDoubleClickReturnValue(true, 0.0);
  heightSlider_.setTooltip("ADM object height, 0-1 fraction of the sphere. Double-click to reset.");
  heightSlider_.onValueChange = [this] {
    p_->setPointHeight(index_, static_cast<float>(heightSlider_.getValue()));
  };

  setupSlider(depthSlider_, 0.0, 1.0, 0.01, st.depth);
  depthSlider_.setDoubleClickReturnValue(true, 0.0);
  depthSlider_.setTooltip("ADM object depth, 0-1 fraction of the sphere. Double-click to reset.");
  depthSlider_.onValueChange = [this] {
    p_->setPointDepth(index_, static_cast<float>(depthSlider_.getValue()));
  };

  setupSlider(diffuseSlider_, 0.0, 1.0, 0.01, st.diffuse);
  diffuseSlider_.setDoubleClickReturnValue(true, 0.0);
  diffuseSlider_.setTooltip(
      "ADM diffuseness, 0 = point source, 1 = fully diffuse. Double-click to reset.");
  diffuseSlider_.onValueChange = [this] {
    p_->setPointDiffuse(index_, static_cast<float>(diffuseSlider_.getValue()));
  };

  for (juce::Slider* slider : {static_cast<juce::Slider*>(&azSlider_), &elSlider_, &distSlider_, &gainSlider_,
                       &widthSlider_, &heightSlider_, &depthSlider_,
                       &diffuseSlider_})
    addAndMakeVisible(*slider);

  resetButton_.onClick = [this] {
    p_->resetPointToDefault(index_);
    refreshFromProcessor();
    if (onChanged) onChanged();
  };
  addAndMakeVisible(resetButton_);

  setSize(1150, kPreferredHeight);
}

void PointEncoderPointControlsRow::refreshFromProcessor() {
  auto st = p_->getPointState(index_);
  activeToggle_.setToggleState(st.active, juce::dontSendNotification);
  nameEditor_.setText(st.name, juce::dontSendNotification);
  channelCombo_.setSelectedId(st.inputChannel + 1, juce::dontSendNotification);
  earChannelCombo_.setSelectedId(st.earChannel < 0 ? 1 : st.earChannel + 2,
                                 juce::dontSendNotification);
  azSlider_.setValue(st.azimuthDeg, juce::dontSendNotification);
  elSlider_.setValue(st.elevationDeg, juce::dontSendNotification);
  distSlider_.setValue(st.distance, juce::dontSendNotification);
  gainSlider_.setValue(st.gain, juce::dontSendNotification);
  widthSlider_.setValue(st.width, juce::dontSendNotification);
  heightSlider_.setValue(st.height, juce::dontSendNotification);
  depthSlider_.setValue(st.depth, juce::dontSendNotification);
  diffuseSlider_.setValue(st.diffuse, juce::dontSendNotification);
}

void PointEncoderPointControlsRow::paint(juce::Graphics& g) {
  g.fillAll((index_ % 2 == 0) ? EarColours::Area01dp
                              : EarColours::Area01dp.darker(0.15f));
}

void PointEncoderPointControlsRow::resized() {
  auto area = getLocalBounds();
  auto topRow = area.removeFromTop(kTopRowHeight);
  activeToggle_.setBounds(topRow.removeFromLeft(kActiveWidth));
  topRow.removeFromLeft(4);
  nameEditor_.setBounds(topRow.removeFromLeft(kNameWidth));
  topRow.removeFromLeft(8);
  channelCombo_.setBounds(topRow.removeFromLeft(kChannelWidth));
  topRow.removeFromLeft(4);
  channelJumpField_.setBounds(topRow.removeFromLeft(kJumpWidth));
  topRow.removeFromLeft(8);
  earChannelCombo_.setBounds(topRow.removeFromLeft(kEarChannelWidth));
  topRow.removeFromLeft(4);
  earChannelJumpField_.setBounds(topRow.removeFromLeft(kJumpWidth));

  auto bottomRow = area.removeFromTop(kSliderRowHeight);
  for (juce::Slider* slider : {static_cast<juce::Slider*>(&azSlider_), &elSlider_, &distSlider_, &gainSlider_,
                       &widthSlider_, &heightSlider_, &depthSlider_,
                       &diffuseSlider_}) {
    slider->setBounds(bottomRow.removeFromLeft(kColumnWidth));
    bottomRow.removeFromLeft(kColumnGap);
  }
  bottomRow.removeFromLeft(8);
  resetButton_.setBounds(bottomRow.removeFromLeft(kResetWidth).reduced(0, 6));
}
