#pragma once

#include "lfe_send_plugin_processor.hpp"

#include "JuceHeader.h"

#include "lfe_send_component.hpp"

class LfeSendAudioProcessorEditor : public AudioProcessorEditor {
 public:
  LfeSendAudioProcessorEditor(LfeSendAudioProcessor*);
  ~LfeSendAudioProcessorEditor();

  void paint(Graphics&) override;
  void resized() override;

 private:
  LfeSendAudioProcessor* p_;

  std::unique_ptr<ear::plugin::ui::LfeSendComponent> content_;
  std::unique_ptr<Viewport> viewport_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
      LfeSendAudioProcessorEditor)
};
