#include "custom_monitoring_plugin_processor.hpp"
#include "custom_monitoring_plugin_editor.hpp"
#include "variable_block_adapter.hpp"

#include "reaper_vst3_interfaces.h"
#include "reaper_integration.hpp"

#include <ear/bs2051.hpp>
#include "helper/custom_layout_file.hpp"

#include "monitoring_audio_processor.hpp"
#include "monitoring_backend.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

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

// Dry air, ~20C - near enough for a delay-compensation estimate; a few
// degrees' error moves the result by well under a sample at any plausible
// room size.
constexpr double kSpeedOfSoundMetresPerSecond = 343.0;
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
  updateDelayLines();
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

    // Per-channel distance delay compensation (see distanceDelaySeconds_) -
    // a no-op pass-through on any channel whose delay line was set to 0
    // samples, which is every channel when the loaded layout's speakers are
    // all at the same distance.
    auto delayChannels = static_cast<int>(
        std::min<std::size_t>(delayLines_.size(),
                              static_cast<std::size_t>(buffer.getNumChannels())));
    for (int ch = 0; ch < delayChannels; ++ch) {
      auto* data = buffer.getWritePointer(ch);
      auto& line = delayLines_[static_cast<std::size_t>(ch)];
      for (int n = 0; n < buffer.getNumSamples(); ++n) {
        line.pushSample(0, data[n]);
        data[n] = line.popSample(0);
      }
    }
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

  // Distance calibration: attenuate/delay every speaker as if it were at the
  // layout's single furthest real distance (never boost, to avoid eating
  // headroom). A layout with every speaker at the same distance - including
  // the common case of a plain azimuth/elevation layout, where
  // loadCustomLayoutFromFile() defaults radius to 1.0 for every line - makes
  // this a no-op: gain 1.0, delay 0 for every channel. See the
  // distanceDelaySeconds_ member comment.
  double maxDistance = 0.0;
  for (auto const& channel : layout.channels()) {
    maxDistance = std::max(maxDistance, channel.polarPosition().distance);
  }
  if (maxDistance <= 0.0) maxDistance = 1.0;

  std::vector<double> newDistanceDelaySeconds(newOutputChannels, 0.0);
  for (int i = 0; i < newOutputChannels; ++i) {
    auto distance = layout.channels()[static_cast<std::size_t>(i)]
                        .polarPosition()
                        .distance;
    newOutputGains[static_cast<std::size_t>(i)] *= distance / maxDistance;
    newDistanceDelaySeconds[static_cast<std::size_t>(i)] =
        (maxDistance - distance) / kSpeedOfSoundMetresPerSecond;
  }

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
    distanceDelaySeconds_ = std::move(newDistanceDelaySeconds);
    backend_ = std::move(newBackend);
    if (newProcessor) processor_ = std::move(newProcessor);
    processorConfig_ = newConfig;
  }

  if (samplerate_ > 0) {
    levelMeter_->setup(numOutputChannels_, samplerate_);
  }
  // Takes processingMutex_ itself - must run after the block above has
  // released it.
  updateDelayLines();
}

void EarCustomMonitoringAudioProcessor::updateDelayLines() {
  if (samplerate_ <= 0) return;

  auto blockSize =
      processorConfig_.blockSize > 0 ? processorConfig_.blockSize : 512;
  juce::dsp::ProcessSpec spec{static_cast<double>(samplerate_),
                              static_cast<juce::uint32>(blockSize), 1u};

  using DelayLineType =
      juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None>;
  std::vector<DelayLineType> newDelayLines;
  newDelayLines.reserve(distanceDelaySeconds_.size());
  for (double delaySeconds : distanceDelaySeconds_) {
    auto delaySamples =
        static_cast<int>(std::lround(delaySeconds * samplerate_));
    DelayLineType line(std::max(1, delaySamples));
    line.prepare(spec);
    line.setDelay(static_cast<float>(delaySamples));
    newDelayLines.push_back(std::move(line));
  }

  std::lock_guard<std::mutex> lock(processingMutex_);
  delayLines_ = std::move(newDelayLines);
}

void EarCustomMonitoringAudioProcessor::configureProcessor(
    const CustomProcessorConfig& config) {
  // Rebuilding MonitoringAudioProcessor resets its internal DSP state
  // (delay lines, decorrelators sized/designed from the layout's speaker
  // positions), so only do it when something that actually affects that
  // state has changed.
  {
    std::lock_guard<std::mutex> lock(processingMutex_);
    if (processor_ && config == processorConfig_) return;
  }

  // This is called from prepareToPlay(), which a host is not guaranteed
  // to only ever call strictly between audio callbacks - REAPER in
  // particular can call it while reconfiguring a shared audio device
  // buffer size/sample rate with the transport still rolling, i.e.
  // potentially concurrently with processBlock() still running on the
  // audio thread. processBlock() reads processor_/processorConfig_
  // under processingMutex_ (see the member comment), so writing them
  // here without the same lock was a real data race - a torn or
  // use-after-freed read of processor_ on the audio thread, which is
  // exactly the kind of sporadic glitch/crash on insert this fixes.
  // Mirrors applyLayout()'s pattern: build the (slow to construct)
  // replacement outside the lock, then swap it in while holding it only
  // for the pointer/field assignment.
  auto newProcessor = std::make_unique<ear::plugin::MonitoringAudioProcessor>(
      config.inputChannels > 0 ? config.inputChannels : numDawChannels_,
      config.layout, config.blockSize);

  std::lock_guard<std::mutex> lock(processingMutex_);
  processor_ = std::move(newProcessor);
  processorConfig_ = config;
}

//==============================================================================
// This creates new instances of the plugin..
AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
  return new EarCustomMonitoringAudioProcessor();
}
