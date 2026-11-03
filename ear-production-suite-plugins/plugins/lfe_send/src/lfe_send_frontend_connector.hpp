#pragma once

#include "JuceHeader.h"

#include "lfe_send_plugin_processor.hpp"
#include "ui/direct_speakers_frontend_backend_connector.hpp"
#include "components/ear_combo_box.hpp"
#include "components/ear_name_text_editor.hpp"
#include "helper/multi_async_updater.h"
#include "channel_meter_layout.hpp"
#include "value_box_speaker_layer.hpp"

namespace ear {
namespace plugin {
namespace ui {

// Reuses the same DirectSpeakers metadata protocol (ParameterId enum,
// DirectSpeakersBackend, DirectSpeakersMetadataSender) as the full
// DirectSpeakers plugin - see lib/include/ui/direct_speakers_frontend_backend_connector.hpp.
// The pack format is permanently fixed to kLfeSendPackFormatIdValue (see
// lfe_send_plugin_processor.hpp), so unlike DirectSpeakersJuceFrontendConnector
// there is no speaker-setup combo box to wire up.
class LfeSendJuceFrontendConnector
    : public ear::plugin::ui::DirectSpeakersFrontendBackendConnector,
      private AudioProcessorParameter::Listener,
      TextEditor::Listener,
      Button::Listener,
      ear::plugin::ui::EarComboBox::Listener {
 public:
  /**
   * Note: Make sure that all AudioProcessorParameters are already
   * added to the LfeSendAudioProcessor at the time the ctor
   * is called.
   */
  LfeSendJuceFrontendConnector(LfeSendAudioProcessor* processor);
  ~LfeSendJuceFrontendConnector();

  void parameterValueChanged(int parameterIndex, float newValue) override;
  void parameterGestureChanged(int parameterIndex,
                               bool gestureIsStarting) override{};

  void trackPropertiesChanged(
      const AudioProcessor::TrackProperties& properties);

  void setStatusBarLabel(std::shared_ptr<Label> label);
  void setColourComboBox(std::shared_ptr<EarComboBox> comboBox);
  void setNameTextEditor(std::shared_ptr<EarNameTextEditor> textEditor);
  void setUseTrackNameCheckbox(std::shared_ptr<ToggleButton> useTrackNameCheckbox);
  void setRoutingComboBox(std::shared_ptr<EarComboBox> comboBox);
  void setLfeLayerValueBox(std::shared_ptr<ValueBoxSpeakerLayer> layer);
  void setChannelGainsValueBox(std::shared_ptr<ChannelMeterLayout> gains);

  void setName(const std::string& name);
  void setColour(Colour colour);
  void setUseTrackName(bool value);
  void setRouting(int routing);

  std::string getActiveName();

 protected:
  // ear::plugin::ui::InputFrontendBackendConnector
  void doSetStatusBarText(const std::string& text) override;

  // EarComboBox::Listener
  void comboBoxChanged(
      ear::plugin::ui::EarComboBox* comboBoxThatHasChanged) override;

  // Button::Listener
  void buttonClicked(Button*) override;

  //TextEditor::Listener
  void textEditorTextChanged(TextEditor&) override;

 private:
  void updateNameState();

  LfeSendAudioProcessor* p_;

  MultiAsyncUpdater updater_;

  std::map<int, RangedAudioParameter*> parameters_;

  std::weak_ptr<Label> statusBarLabel_;
  std::string cachedStatusBarText_;
  std::weak_ptr<EarNameTextEditor> nameTextEditor_;
  std::weak_ptr<ToggleButton> useTrackNameCheckbox_;
  std::string cachedName_{};
  std::string lastKnownTrackName_{};
  std::string lastKnownCustomName_{};
  bool cachedUseTrackName_{ true };
  std::weak_ptr<EarComboBox> colourComboBox_;
  Colour cachedColour_{ juce::Colours::transparentBlack };
  std::weak_ptr<EarComboBox> routingComboBox_;
  int cachedRouting_{ -1 };

  std::weak_ptr<ValueBoxSpeakerLayer> lfeLayer_;
  std::weak_ptr<ChannelMeterLayout> channelGains_;
};

}  // namespace ui
}  // namespace plugin
}  // namespace ear
