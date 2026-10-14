#pragma once

#include "JuceHeader.h"

#include <ear/ear.hpp>
#include <memory>
#include <mutex>
#include <vector>

#include "components/level_meter_calculator.hpp"
#include <daw_channel_count.h>

namespace ear {
namespace plugin {
class MonitoringBackend;
class MonitoringAudioProcessor;
}  // namespace plugin
}  // namespace ear

struct CustomProcessorConfig {
  int inputChannels;
  int outputChannels;
  int blockSize;
  ear::Layout layout;
  // Bumped every time a layout is (re)loaded, so that two loads of files
  // that happen to share a name (e.g. reloading an edited file) are not
  // mistaken for "nothing changed" - the layout name alone isn't a reliable
  // fingerprint since ear::Layout has no equality operator.
  int layoutGeneration{-1};
};

inline bool operator==(CustomProcessorConfig const& lhs,
                       CustomProcessorConfig const& rhs) {
  return lhs.inputChannels == rhs.inputChannels &&
        lhs.outputChannels == rhs.outputChannels &&
        lhs.blockSize == rhs.blockSize &&
        lhs.layoutGeneration == rhs.layoutGeneration;
}
inline bool operator!=(CustomProcessorConfig const& lhs,
                       CustomProcessorConfig const& rhs) {
  return !(lhs == rhs);
}

/** @brief EAR Monitoring plugin variant that decodes to a loudspeaker layout
 * loaded at runtime from a simple text file, rather than to one of the fixed
 * BS.2051 layouts baked into the other `ear_monitoring_*` plugin targets.
 *
 * The object/DirectSpeakers/HOA -> loudspeaker gain calculation itself is
 * unchanged: it is still EAR's own (libear) gain calculator, just constructed
 * against an `ear::Layout` built from the loaded file instead of
 * `ear::getLayout("...")`.
 */
class EarCustomMonitoringAudioProcessor : public AudioProcessor,
                                          public VST3ClientExtensions {
 public:
  EarCustomMonitoringAudioProcessor();
  ~EarCustomMonitoringAudioProcessor();

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

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

  std::weak_ptr<ear::plugin::LevelMeterCalculator> getLevelMeter() {
    return levelMeter_;
  };

  void setIHostApplication(Steinberg::FUnknown* unknown) override;

  /** @brief Load a loudspeaker layout from a text/csv file (see
   * ear::plugin::loadCustomLayoutFromFile for the file format), reconfigure
   * the output bus and rebuild the gain calculator for it.
   * @throws ear::plugin::CustomLayoutFileError on a malformed file, which the
   *     caller (the editor) should catch and display to the user; on error,
   *     the previously active layout is left untouched.
   */
  void loadLayoutFile(const File& file);

  const ear::Layout& currentLayout() const { return processorConfig_.layout; }
  File currentLayoutFile() const { return currentLayoutFile_; }

  /** @brief Called on the message thread after a layout has been
   * successfully (re)loaded, so the editor can rebuild its speaker meters. */
  std::function<void()> onLayoutChanged;

 private:
  void configureProcessor(const CustomProcessorConfig& config);
  // isNewLayout selects whether this counts as a genuinely new layout (bumps
  // layoutGeneration_, forcing the DSP to be rebuilt) or a re-application of
  // the layout already in effect (e.g. bus width renegotiation) where a
  // rebuild is only needed if input/output channel counts actually changed.
  void applyLayout(const ear::Layout& layout,
                   std::vector<double> outputGains,
                   bool isNewLayout = true);

  CustomProcessorConfig processorConfig_{};
  int layoutGeneration_{0};
  std::vector<double> outputGains_;
  File currentLayoutFile_;

  // loadLayoutFile() rebuilds backend_/processor_/outputGains_ on the
  // message thread (in response to a UI button click) at arbitrary times,
  // including while the host's audio thread may be concurrently inside
  // processBlock() using the previous ones - guard both sides so a layout
  // reload can never race a running audio callback (that race is a
  // use-after-free: the old MonitoringBackend/MonitoringAudioProcessor get
  // destroyed while processBlock() is still calling into them).
  std::mutex processingMutex_;
  std::unique_ptr<ear::plugin::MonitoringBackend> backend_;
  std::unique_ptr<ear::plugin::MonitoringAudioProcessor> processor_;

  int samplerate_{0};
  int numDawChannels_{MAX_DAW_CHANNELS};
  int numOutputChannels_{0};
  std::shared_ptr<ear::plugin::LevelMeterCalculator> levelMeter_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
      EarCustomMonitoringAudioProcessor)
};
