#pragma once

#include "JuceHeader.h"
#include "object_stereo_component.hpp"
#include "object_stereo_plugin_processor.hpp"

class ObjectStereoAudioProcessorEditor : public AudioProcessorEditor {
 public:
  ObjectStereoAudioProcessorEditor(ObjectStereoAudioProcessor*);
  ~ObjectStereoAudioProcessorEditor();

  void paint(Graphics&) override;
  void resized() override;

 private:
  ObjectStereoAudioProcessor* p_;
  std::unique_ptr<ear::plugin::ui::ObjectStereoComponent> content_;
  std::unique_ptr<Viewport> viewport_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ObjectStereoAudioProcessorEditor)
};
