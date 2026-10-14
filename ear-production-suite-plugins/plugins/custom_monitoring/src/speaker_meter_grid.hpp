#pragma once

#include "JuceHeader.h"

#include "components/look_and_feel/colours.hpp"
#include "components/look_and_feel/fonts.hpp"
#include "speaker_meter.hpp"

namespace ear {
namespace plugin {
namespace ui {

/** @brief Scrollable grid of SpeakerMeters, sized for the arbitrary (and
 * possibly large) speaker counts that a custom-loaded layout can have -
 * unlike the fixed 12+12 top/bottom split used for the standard BS.2051
 * monitoring plugins. */
class SpeakerMeterGrid : public Component {
 public:
  static constexpr int meterWidth = 50;
  static constexpr int meterSpacing = 5;
  static constexpr int meterHeight = 140;

  SpeakerMeterGrid() {
    content_ = std::make_unique<Component>();
    viewport_.setViewedComponent(content_.get(), false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
  }

  void setMeterCount(int count) {
    meters_.clear();
    for (int i = 0; i < count; ++i) {
      meters_.push_back(std::make_unique<SpeakerMeter>(String(i + 1), "", ""));
      content_->addAndMakeVisible(meters_.back().get());
    }
    resized();
  }

  void setSpeakerInfo(int index, const String& name, const String& label) {
    if (index >= 0 && index < static_cast<int>(meters_.size())) {
      meters_[static_cast<size_t>(index)] =
          std::make_unique<SpeakerMeter>(String(index + 1), name, label);
      content_->addAndMakeVisible(meters_[static_cast<size_t>(index)].get());
      resized();
    }
  }

  ear::plugin::ui::SpeakerMeter* meterAt(int index) {
    return index >= 0 && index < static_cast<int>(meters_.size())
              ? meters_[static_cast<size_t>(index)].get()
              : nullptr;
  }

  void paint(Graphics& g) override { g.fillAll(EarColours::Area01dp); }

  void resized() override {
    auto area = getLocalBounds();
    viewport_.setBounds(area);

    int availableWidth = area.getWidth() - viewport_.getScrollBarThickness();
    int perRow = std::max(
        1, (availableWidth + meterSpacing) / (meterWidth + meterSpacing));

    int numRows =
        meters_.empty() ? 0 : (static_cast<int>(meters_.size()) + perRow - 1) / perRow;
    int contentHeight = numRows * (meterHeight + meterSpacing);
    content_->setSize(availableWidth, std::max(contentHeight, area.getHeight()));

    for (size_t i = 0; i < meters_.size(); ++i) {
      int row = static_cast<int>(i) / perRow;
      int col = static_cast<int>(i) % perRow;
      meters_[i]->setBounds(col * (meterWidth + meterSpacing),
                            row * (meterHeight + meterSpacing), meterWidth,
                            meterHeight);
    }
  }

 private:
  Viewport viewport_;
  std::unique_ptr<Component> content_;
  std::vector<std::unique_ptr<ear::plugin::ui::SpeakerMeter>> meters_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpeakerMeterGrid)
};

}  // namespace ui
}  // namespace plugin
}  // namespace ear
