#include "matrix_router_plugin_processor.hpp"
#include "matrix_router_plugin_editor.hpp"

namespace matrix_router {

MatrixRouterAudioProcessor::MatrixRouterAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input",
                                    AudioChannelSet::discreteChannels(
                                        kMaxChannels),
                                    true)
                         .withOutput("Output",
                                     AudioChannelSet::discreteChannels(
                                         kMaxChannels),
                                     true)),
      matrix_(static_cast<size_t>(kMaxChannels) * kMaxChannels, false) {}

MatrixRouterAudioProcessor::~MatrixRouterAudioProcessor() { *aliveFlag_ = false; }

void MatrixRouterAudioProcessor::prepareToPlay(double, int samplesPerBlock) {
  inputScratch_.setSize(kMaxChannels, samplesPerBlock);
}

void MatrixRouterAudioProcessor::releaseResources() {}

bool MatrixRouterAudioProcessor::isBusesLayoutSupported(
    const BusesLayout& layouts) const {
  return layouts.getMainInputChannels() == kMaxChannels &&
        layouts.getMainOutputChannels() == kMaxChannels;
}

void MatrixRouterAudioProcessor::processBlock(AudioBuffer<float>& buffer,
                                              MidiBuffer&) {
  ScopedNoDenormals noDenormals;
  auto numSamples = buffer.getNumSamples();
  // Guards against a host calling processBlock() with a larger block than
  // it negotiated in prepareToPlay(), which would otherwise overrun
  // inputScratch_'s allocation below.
  if (numSamples > inputScratch_.getNumSamples()) {
    inputScratch_.setSize(kMaxChannels, numSamples, false, false, true);
  }

  std::lock_guard<std::mutex> lock(matrixMutex_);

  // buffer holds both input and output (in place), so the inputs must be
  // copied out before any output is written, in case an input channel is
  // also used as an output channel index.
  for (int i = 0; i < numInputs_; ++i) {
    inputScratch_.copyFrom(i, 0, buffer, i, 0, numSamples);
  }

  for (int o = 0; o < numOutputs_; ++o) {
    buffer.clear(o, 0, numSamples);
    for (int i = 0; i < numInputs_; ++i) {
      if (matrix_[static_cast<size_t>(indexOf(i, o))]) {
        buffer.addFrom(o, 0, inputScratch_, i, 0, numSamples);
      }
    }
  }
  for (int o = numOutputs_; o < buffer.getNumChannels(); ++o) {
    buffer.clear(o, 0, numSamples);
  }
}

AudioProcessorEditor* MatrixRouterAudioProcessor::createEditor() {
  return new MatrixRouterAudioProcessorEditor(this);
}

bool MatrixRouterAudioProcessor::hasEditor() const { return true; }

const String MatrixRouterAudioProcessor::getName() const {
  return JucePlugin_Name;
}

bool MatrixRouterAudioProcessor::acceptsMidi() const { return false; }
bool MatrixRouterAudioProcessor::producesMidi() const { return false; }
bool MatrixRouterAudioProcessor::isMidiEffect() const { return false; }
double MatrixRouterAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int MatrixRouterAudioProcessor::getNumPrograms() { return 1; }
int MatrixRouterAudioProcessor::getCurrentProgram() { return 0; }
void MatrixRouterAudioProcessor::setCurrentProgram(int) {}
const String MatrixRouterAudioProcessor::getProgramName(int) { return {}; }
void MatrixRouterAudioProcessor::changeProgramName(int, const String&) {}

void MatrixRouterAudioProcessor::getStateInformation(MemoryBlock& destData) {
  std::lock_guard<std::mutex> lock(matrixMutex_);
  MemoryOutputStream stream(destData, false);
  stream.writeInt(1);  // state format version
  stream.writeInt(numInputs_);
  stream.writeInt(numOutputs_);
  // Only the active numInputs_ x numOutputs_ window is saved; cells outside
  // it are always false when loaded back (see setStateInformation()).
  for (int o = 0; o < numOutputs_; ++o) {
    for (int i = 0; i < numInputs_; ++i) {
      stream.writeBool(matrix_[static_cast<size_t>(indexOf(i, o))]);
    }
  }
}

void MatrixRouterAudioProcessor::setStateInformation(const void* data,
                                                      int sizeInBytes) {
  MemoryInputStream stream(data, static_cast<size_t>(sizeInBytes), false);
  auto version = stream.readInt();
  if (version != 1) return;
  auto newNumInputs = jlimit(1, kMaxChannels, stream.readInt());
  auto newNumOutputs = jlimit(1, kMaxChannels, stream.readInt());

  std::vector<bool> newMatrix(static_cast<size_t>(kMaxChannels) * kMaxChannels,
                              false);
  for (int o = 0; o < newNumOutputs && !stream.isExhausted(); ++o) {
    for (int i = 0; i < newNumInputs && !stream.isExhausted(); ++i) {
      newMatrix[static_cast<size_t>(o) * kMaxChannels + i] =
          stream.readBool();
    }
  }

  {
    std::lock_guard<std::mutex> lock(matrixMutex_);
    numInputs_ = newNumInputs;
    numOutputs_ = newNumOutputs;
    matrix_ = std::move(newMatrix);
  }
  notifyMatrixChanged();
}

void MatrixRouterAudioProcessor::setNumInputs(int n) {
  n = jlimit(1, kMaxChannels, n);
  {
    std::lock_guard<std::mutex> lock(matrixMutex_);
    if (n == numInputs_) return;
    numInputs_ = n;
  }
  notifyMatrixChanged();
}

void MatrixRouterAudioProcessor::setNumOutputs(int n) {
  n = jlimit(1, kMaxChannels, n);
  {
    std::lock_guard<std::mutex> lock(matrixMutex_);
    if (n == numOutputs_) return;
    numOutputs_ = n;
  }
  notifyMatrixChanged();
}

bool MatrixRouterAudioProcessor::crosspoint(int input, int output) const {
  std::lock_guard<std::mutex> lock(matrixMutex_);
  if (input < 0 || input >= numInputs_ || output < 0 || output >= numOutputs_)
    return false;
  return matrix_[static_cast<size_t>(indexOf(input, output))];
}

void MatrixRouterAudioProcessor::setCrosspoint(int input, int output,
                                               bool enabled) {
  {
    std::lock_guard<std::mutex> lock(matrixMutex_);
    if (input < 0 || input >= numInputs_ || output < 0 ||
        output >= numOutputs_)
      return;
    matrix_[static_cast<size_t>(indexOf(input, output))] = enabled;
  }
  notifyMatrixChanged();
}

void MatrixRouterAudioProcessor::clearAllCrosspoints() {
  {
    std::lock_guard<std::mutex> lock(matrixMutex_);
    std::fill(matrix_.begin(), matrix_.end(), false);
  }
  notifyMatrixChanged();
}

void MatrixRouterAudioProcessor::notifyMatrixChanged() {
  // setStateInformation() (preset recall) is not guaranteed to run on the
  // message thread in every host, but onMatrixChanged rebuilds JUCE
  // Components in the editor, which must happen on the message thread. The
  // weak alive-flag guards against the processor being destroyed (e.g. the
  // plugin removed) before this async callback runs.
  std::weak_ptr<bool> weakAlive = aliveFlag_;
  MessageManager::callAsync([this, weakAlive]() {
    if (weakAlive.expired()) return;
    if (onMatrixChanged) onMatrixChanged();
  });
}

}  // namespace matrix_router

//==============================================================================
AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
  return new matrix_router::MatrixRouterAudioProcessor();
}
