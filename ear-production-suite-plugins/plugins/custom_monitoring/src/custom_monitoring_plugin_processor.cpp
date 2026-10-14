#include "custom_monitoring_plugin_processor.hpp"
#include "custom_monitoring_plugin_editor.hpp"
#include "variable_block_adapter.hpp"

#include "reaper_vst3_interfaces.h"
#include "reaper_integration.hpp"

#include <ear/bs2051.hpp>
#include "helper/custom_layout_file.hpp"

#include "monitoring_audio_processor.hpp"
#include "monitoring_backend.hpp"

#include <cassert>

namespace ear {
namespace plugin {
template <>
struct BufferTraits<juce::AudioBuffer<float>> {
  using SampleType = float;
  using Buffer = juce::AudioBuffer<float>;
  static Eigen::Index channelCount(const Buffer& b) {
    return b.getNumChannels();
  }
  static Eigen::Index size(const Buffer& b) { return b.getNumSamples(); }
  static SampleType* getChannel(Buffer& b, std::size_t n) {
    return b.getWritePointer(static_cast<int>(n));
  }
  static const SampleType* getChannel(Buffer const& b, std::size_t n) {
    return b.getReadPointer(static_cast<int>(n));
  }
};

}  // namespace plugin
}  // namespace ear

namespace {
// Used until the user loads a real layout, so the plugin is usable
// immediately after being inserted.
ear::Layout defaultLayout() { return ear::getLayout("0+2+0"); }
}  // namespace

void EarCustomMonitoringAudioProcessor::setIHostApplication(
    Steinberg::FUnknown* unknown) {
  auto reaperHost = dynamic_cast<IReaperHostApplication*>(unknown);
  VST3ClientExtensions::setIHostApplication(unknown);
  if (reaperHost) {
    numDawChannels_ = DetermineChannelCount(reaperHost);
    auto retI = setChannelLayoutOfBus(
        true, 0, AudioChannelSet::discreteChannels(numDawChannels_));
    auto retO = setChannelLayoutOfBus(
        false, 0, AudioChannelSet::discreteChannels(numDawChannels_));
    assert(retI && retO);
    applyLayout(processorConfig_.layout, outputGains_, false);
  }
}

//==============================================================================
EarCustomMonitoringAudioProcessor::EarCustomMonitoringAudioProcessor()
    : AudioProcessor(
          BusesProperties()
              .withInput("Input",
                         AudioChannelSet::discreteChannels(MAX_DAW_CHANNELS),
                         true)
              .withOutput("Output",
                          AudioChannelSet::discreteChannels(MAX_DAW_CHANNELS),
                          true)) {
  levelMeter_ = std::make_shared<ear::plugin::LevelMeterCalculator>(0, 0);
  applyLayout(defaultLayout(), {}, true);
}

EarCustomMonitoringAudioProcessor::~EarCustomMonitoringAudioProcessor() {}

//==============================================================================
const String EarCustomMonitoringAudioProcessor::getName() const {
  return JucePlugin_Name;
}

bool EarCustomMonitoringAudioProcessor::acceptsMidi() const { return false; }

bool EarCustomMonitoringAudioProcessor::producesMidi() const { return false; }

bool EarCustomMonitoringAudioProcessor::isMidiEffect() const { return false; }

double EarCustomMonitoringAudioProcessor::getTailLengthSeconds() const {
  return 0.0;
}

int EarCustomMonitoringAudioProcessor::getNumPrograms() {
  return 1;  // NB: some hosts don't cope very well if you tell them there are
             // 0 programs, so this should be at least 1, even if you're not
             // really implementing programs.
}

int EarCustomMonitoringAudioProcessor::getCurrentProgram() { return 0; }

void EarCustomMonitoringAudioProcessor::setCurrentProgram(int index) {}

const String EarCustomMonitoringAudioProcessor::getProgramName(int index) {
  return {};
}

void EarCustomMonitoringAudioProcessor::changeProgramName(
    int index, const String& newName) {}

//==============================================================================
void EarCustomMonitoringAudioProcessor::prepareToPlay(double sampleRate,
                                                       int samplesPerBlock) {
  CustomProcessorConfig newConfig{
      getTotalNumInputChannels(), getTotalNumOutputChannels(),
      samplesPerBlock, processorConfig_.layout, layoutGeneration_};
  configureProcessor(newConfig);
  samplerate_ = static_cast<int>(sampleRate);
  levelMeter_->setup(newConfig.layout.channels().size(), sampleRate);
}

void EarCustomMonitoringAudioProcessor::releaseResources() {
  // When playback stops, you can use this as an opportunity to free up any
  // spare memory, etc.
}

bool EarCustomMonitoringAudioProcessor::isBusesLayoutSupported(
    const BusesLayout& layouts) const {
  auto i = layouts.getMainInputChannels();
  if (i != numDawChannels_) return false;
  auto o = layouts.getMainOutputChannels();
  if (o != numDawChannels_) return false;

  return true;
}

void EarCustomMonitoringAudioProcessor::processBlock(
    AudioBuffer<float>& buffer, MidiBuffer&) {
  ScopedNoDenormals noDenormals;

  if (backend_ && backend_->isExporting()) {
    if (getTotalNumOutputChannels() > 0 && getTotalNumInputChannels() > 0) {
      // Sum all in to first channel
      for (int srcChn = 1; srcChn < buffer.getNumChannels(); srcChn++) {
        buffer.addFrom(0, 0, buffer.getReadPointer(srcChn),
                       buffer.getNumSamples());
      }
      // Copy to/zero others
      for (int destChn = 1; destChn < buffer.getNumChannels(); destChn++) {
        if (destChn < getTotalNumOutputChannels()) {
          buffer.copyFrom(destChn, 0, buffer.getReadPointer(0),
                          buffer.getNumSamples());
        } else {
          buffer.clear(destChn, 0, buffer.getNumSamples());
        }
      }
    }
    return;
  }

  // Do EAR render. Locking here means a layout reload on the message
  // thread (see applyLayout()) can very briefly block the audio thread
  // instead of racing it - see the processingMutex_ comment in the header.
  std::unique_lock<std::mutex> processingLock(processingMutex_);
  if (backend_ && processor_) {
    auto gains = backend_->currentGains();
    // Apply the per-speaker trim gain from the layout file (libear's Layout
    // has no notion of output trim, so this is done on top of its result).
    auto rows = static_cast<int>(std::min<std::size_t>(
        outputGains_.size(), static_cast<std::size_t>(gains.direct.rows())));
    for (int r = 0; r < rows; ++r) {
      auto g = static_cast<float>(outputGains_[static_cast<std::size_t>(r)]);
      gains.direct.row(r) *= g;
      gains.diffuse.row(r) *= g;
    }
    processor_->process(buffer, buffer, gains.direct, gains.diffuse);
  }
  int numOutputChannels = numOutputChannels_;
  processingLock.unlock();

  if (getActiveEditor()) {
    levelMeter_->process(buffer);
  }

  // Clear unused output channels
  for (int outChn = numOutputChannels; outChn < buffer.getNumChannels();
      ++outChn) {
    buffer.clear(outChn, 0, buffer.getNumSamples());
  }
}

//==============================================================================
bool EarCustomMonitoringAudioProcessor::hasEditor() const {
  return true;  // (change this to false if you choose to not supply an
                // editor)
}

AudioProcessorEditor* EarCustomMonitoringAudioProcessor::createEditor() {
  levelMeter_->resetLevels();
  return new EarCustomMonitoringAudioProcessorEditor(this);
}

//==============================================================================
void EarCustomMonitoringAudioProcessor::getStateInformation(
    MemoryBlock& destData) {
  // Only the path of the last successfully loaded layout file is persisted;
  // its content is re-read from disk on load (see setStateInformation()).
  auto path = currentLayoutFile_.getFullPathName();
  destData.replaceAll(path.toRawUTF8(), path.getNumBytesAsUTF8() + 1);
}

void EarCustomMonitoringAudioProcessor::setStateInformation(const void* data,
                                                            int sizeInBytes) {
  if (sizeInBytes <= 0) return;
  String path = String::fromUTF8(static_cast<const char*>(data), sizeInBytes);
  if (path.isEmpty()) return;
  File file(path);
  if (file.existsAsFile()) {
    try {
      loadLayoutFile(file);
    } catch (const std::exception&) {
      // Layout file may have moved/changed since the session was saved;
      // fall back to the default layout rather than failing to load.
    }
  }
}

//==============================================================================
void EarCustomMonitoringAudioProcessor::loadLayoutFile(const File& file) {
  auto customLayout = ear::plugin::loadCustomLayoutFromFile(
      file.getFullPathName().toStdString());

  if (static_cast<int>(customLayout.layout.channels().size()) >
      numDawChannels_) {
    throw ear::plugin::CustomLayoutFileError(
        "Layout has more speakers (" +
        std::to_string(customLayout.layout.channels().size()) +
        ") than the DAW channel width available to this plugin (" +
        std::to_string(numDawChannels_) + ")");
  }

  applyLayout(customLayout.layout, customLayout.outputGains, true);
  currentLayoutFile_ = file;

  if (onLayoutChanged) onLayoutChanged();
}

void EarCustomMonitoringAudioProcessor::applyLayout(
    const ear::Layout& layout, std::vector<double> outputGains,
    bool isNewLayout) {
  auto newOutputChannels = static_cast<int>(layout.channels().size());
  auto newOutputGains = outputGains.empty()
                            ? std::vector<double>(newOutputChannels, 1.0)
                            : std::move(outputGains);

  if (isNewLayout) ++layoutGeneration_;

  CustomProcessorConfig newConfig{
      getTotalNumInputChannels(), getTotalNumOutputChannels(),
      processorConfig_.blockSize > 0 ? processorConfig_.blockSize : 512,
      layout, layoutGeneration_};

  // Build the replacement backend/processor first, then swap them in under
  // the lock - this keeps the lock held only for pointer/field assignment,
  // not for the (much slower) construction work, minimising how long the
  // audio thread can be blocked in processBlock() waiting for it.
  auto newBackend = std::make_unique<ear::plugin::MonitoringBackend>(
      nullptr, layout, numDawChannels_);
  std::unique_ptr<ear::plugin::MonitoringAudioProcessor> newProcessor;
  if (!processor_ || newConfig != processorConfig_) {
    newProcessor = std::make_unique<ear::plugin::MonitoringAudioProcessor>(
        newConfig.inputChannels > 0 ? newConfig.inputChannels
                                    : numDawChannels_,
        newConfig.layout, newConfig.blockSize);
  }

  {
    std::lock_guard<std::mutex> lock(processingMutex_);
    numOutputChannels_ = newOutputChannels;
    outputGains_ = std::move(newOutputGains);
    backend_ = std::move(newBackend);
    if (newProcessor) processor_ = std::move(newProcessor);
    processorConfig_ = newConfig;
  }

  if (samplerate_ > 0) {
    levelMeter_->setup(numOutputChannels_, samplerate_);
  }
}

void EarCustomMonitoringAudioProcessor::configureProcessor(
    const CustomProcessorConfig& config) {
  // Rebuilding MonitoringAudioProcessor resets its internal DSP state
  // (delay lines, decorrelators sized/designed from the layout's speaker
  // positions), so only do it when something that actually affects that
  // state has changed.
  if (!processor_ || config != processorConfig_) {
    processor_ = std::make_unique<ear::plugin::MonitoringAudioProcessor>(
        config.inputChannels > 0 ? config.inputChannels : numDawChannels_,
        config.layout, config.blockSize);
    processorConfig_ = config;
  }
}

//==============================================================================
// This creates new instances of the plugin..
AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
  return new EarCustomMonitoringAudioProcessor();
}
