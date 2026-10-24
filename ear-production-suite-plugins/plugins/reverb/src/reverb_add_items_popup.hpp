#pragma once

#include "JuceHeader.h"
#include "reverb_plugin_processor.hpp"
#include <vector>

/** @brief The "+ Item" popup, laid out like EAR Scene's own: three columns
 * (Objects / Direct Speakers / Higher Order Ambisonics) under one header,
 * with an Add button spanning the bottom. Only the Objects column is
 * actually selectable - DirectSpeakers and HOA items are listed (for
 * visual/informational parity with Scene, and so the user can see they
 * exist rather than wondering where they went) but greyed out, since this
 * reverb's DSP needs one point position per input to weight its
 * direction, which only Objects have (see ReverbAudioProcessor::
 * UnsupportedInputInfo for why). */
class ReverbAddItemsPopup : public juce::Component {
 public:
  explicit ReverbAddItemsPopup(ReverbAudioProcessor* p);
  ~ReverbAddItemsPopup() override;

  void paint(juce::Graphics&) override;
  void resized() override;

  // Call if the available-items set may have changed while this is open.
  void refresh();

  std::function<void()> onClose;

 private:
  class ObjectsListModel;
  class UnsupportedListModel;

  ReverbAudioProcessor* p_;
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

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbAddItemsPopup)
};
