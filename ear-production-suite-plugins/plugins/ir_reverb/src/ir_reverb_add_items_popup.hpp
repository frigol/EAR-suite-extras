#pragma once

#include "JuceHeader.h"
#include "ir_reverb_plugin_processor.hpp"
#include <vector>

/** @brief The "+ Item" popup, copied from the algorithmic Reverb's own
 * ReverbAddItemsPopup: three columns (Objects / Direct Speakers / Higher
 * Order Ambisonics) under one header, Add button along the bottom. Only
 * Objects are selectable - DirectSpeakers/HOA are listed but greyed out,
 * same reasoning as that plugin's own (see IrReverbAudioProcessor::
 * UnsupportedInputInfo). */
class IrReverbAddItemsPopup : public juce::Component {
 public:
  explicit IrReverbAddItemsPopup(IrReverbAudioProcessor* p);
  ~IrReverbAddItemsPopup() override;

  void paint(juce::Graphics&) override;
  void resized() override;

  void refresh();

  std::function<void()> onClose;

 private:
  class ObjectsListModel;
  class UnsupportedListModel;

  IrReverbAudioProcessor* p_;
  juce::Label titleLabel_;
  juce::TextButton closeButton_{juce::String::fromUTF8("\xc3\x97")};

  juce::Label objectsColumnLabel_{{}, "Objects"};
  juce::Label directSpeakersColumnLabel_{{}, "Direct Speakers"};
  juce::Label hoaColumnLabel_{{}, "Higher Order Ambisonics"};

  std::unique_ptr<ObjectsListModel> objectsModel_;
  std::unique_ptr<juce::ListBox> objectsList_;
  std::unique_ptr<UnsupportedListModel> directSpeakersModel_;
  std::unique_ptr<juce::ListBox> directSpeakersList_;
  std::unique_ptr<UnsupportedListModel> hoaModel_;
  std::unique_ptr<juce::ListBox> hoaList_;

  juce::TextButton addButton_{"Add"};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IrReverbAddItemsPopup)
};
