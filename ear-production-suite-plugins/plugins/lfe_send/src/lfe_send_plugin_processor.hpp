#pragma once

#include "JuceHeader.h"

#include "direct_speakers_backend.hpp"
#include "components/level_meter_calculator.hpp"
#include "reaper_vst3_interfaces.h"
#include "components/read_only_audio_parameter_int.hpp"
#include <daw_channel_count.h>

#include <memory>

namespace ear {
namespace plugin {
namespace ui {
class LfeSendJuceFrontendConnector;
}
class DirectSpeakersBackend;
}  // namespace plugin
}  // namespace ear

// Pack format idValue for the "LFE Only" supplementary pack format
// (AP_0001110c, see shared/helper/supplementary_definitions.hpp). A single
// DirectSpeakers channel referencing the common-definitions LFE channel
// format (AC_00010004) - reused unchanged, nothing bespoke about the ADM
// metadata itself, just a one-speaker pack so this plugin never claims more
// DAW/Scene channel slots than the single LFE feed it actually carries.
constexpr int kLfeSendPackFormatIdValue = 0x110c;

/** @brief Cut-down sibling of the DirectSpeakers input plugin, permanently
 * locked to the single-channel "LFE Only" pack format. Exists because object
 * panners can't target the LFE channel at all, so an LFE bed feed has to
 * come in via a DirectSpeakers-type element - but the full DirectSpeakers
 * plugin forces picking (and routing/allocating channels for) a whole bed
 * layout just to reach that one channel. This plugin sends the same
 * DirectSpeakersTypeMetadata protocol to the Scene backend as
 * DirectSpeakers does, with packFormatIdValue fixed to the LFE-only pack so
 * only a single DAW channel/slot is ever consumed.
 */
class LfeSendAudioProcessor : public AudioProcessor, public VST3ClientExtensions {
 public:
  LfeSendAudioProcessor();
  ~LfeSendAudioProcessor();

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;

  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

  void processBlock(AudioBuffer<float>&, MidiBuffer&) override;

  AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override;

  const String getName() const override;

  bool acceptsMidi() const override;
  bool producesMidi() const override;
  bool isMidiEffect() const override;
  double getTailLengthSeconds() const override;

  int getNumPrograms() override;
  int getCurrentProgram() override;
  void setCurrentProgram(int index) override;
  const String getProgramName(int index) override;
  void changeProgramName(int index, const String& newName) override;

  void getStateInformation(MemoryBlock& destData) override;
  void setStateInformation(const void* data, int sizeInBytes) override;
  void setStateInformation(XmlElement* xmlState, bool useDefaultsIfUnspecified = true);

  void updateTrackProperties(const TrackProperties& properties) override;

  AudioParameterInt* getRouting() { return routing_; }
  AudioParameterInt* getPackFormatIdValue() { return packFormatIdValue_; }
  AudioParameterBool* getUseTrackName() { return useTrackName_; }
  AudioParameterInt* getInputInstanceId() { return inputInstanceId_; }

  AudioProcessorParameter* getBypassParameter() {
    return bypass_;
  }

  std::weak_ptr<ear::plugin::LevelMeterCalculator> getLevelMeter() {
    return levelMeter_;
  };

  ear::plugin::ui::LfeSendJuceFrontendConnector* getFrontendConnector() {
    return connector_.get();
  }

  void setIHostApplication(Steinberg::FUnknown *unknown) override;
  void extensionSetState(std::string const& xmlState);

  int getNumDawChannels() { return numDawChannels_; }

 private:
  IReaperHostApplication* reaperHost{ nullptr };
  ear::plugin::communication::ConnectionId connectionId_;

  AudioParameterInt* routing_;
  AudioParameterInt* packFormatIdValue_;
  AudioParameterBool* bypass_;
  AudioParameterBool* useTrackName_;
  ReadOnlyAudioParameterInt* inputInstanceId_;

  std::unique_ptr<ear::plugin::ui::LfeSendJuceFrontendConnector>
      connector_;
  std::unique_ptr<ear::plugin::DirectSpeakersBackend> backend_;

  int samplerate_;
  int numDawChannels_{MAX_DAW_CHANNELS};
  std::shared_ptr<ear::plugin::LevelMeterCalculator> levelMeter_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LfeSendAudioProcessor)
};
