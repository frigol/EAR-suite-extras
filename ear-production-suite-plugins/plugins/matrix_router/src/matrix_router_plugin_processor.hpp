#pragma once

#include "JuceHeader.h"

#include <mutex>
#include <vector>

namespace matrix_router {

constexpr int kMaxChannels = 128;
constexpr int kDefaultChannels = 8;

/** @brief Routing matrix VST3 plugin: an N-input x M-output patchbay, like
 * REAPER's own track routing matrix, but insertable as an FX so its
 * crosspoints (and channel counts) are part of the plugin's own state and
 * therefore save/recall as an ordinary host preset.
 *
 * Each output is the sum of every input whose crosspoint with it is
 * enabled (fan-in); a single input can likewise feed any number of outputs
 * (fan-out) - the same many-to-many behaviour as REAPER's native matrix.
 *
 * The host-negotiated bus is always fixed at kMaxChannels (128) wide on
 * both sides; "Inputs"/"Outputs" in the UI just select how many of those
 * 128 physical channels are currently active (unused outputs are cleared,
 * unused inputs are simply never read). This sidesteps asking the host to
 * renegotiate channel counts interactively, which most hosts tolerate
 * poorly - REAPER included - and matches how the other plugins in this
 * family handle a channel count that can change at runtime.
 */
class MatrixRouterAudioProcessor : public AudioProcessor {
 public:
  MatrixRouterAudioProcessor();
  ~MatrixRouterAudioProcessor() override;

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

  // --- Matrix editing API, called from the editor (message thread) ---

  int numInputs() const { return numInputs_; }
  int numOutputs() const { return numOutputs_; }

  /** @brief Request a new input/output channel count (clamped to
   * [1, kMaxChannels]). Existing crosspoints that are still in range are
   * kept. This does not touch the host bus layout - see the class comment
   * on why the bus itself is always the full kMaxChannels wide. */
  void setNumInputs(int n);
  void setNumOutputs(int n);

  bool crosspoint(int input, int output) const;
  void setCrosspoint(int input, int output, bool enabled);
  void clearAllCrosspoints();

  /** @brief Called on the message thread whenever the matrix size or
   * content changes, so the editor can refresh. */
  std::function<void()> onMatrixChanged;

 private:
  int indexOf(int input, int output) const { return output * kMaxChannels + input; }
  void notifyMatrixChanged();

  // matrix_[output * kMaxChannels + input]; fixed kMaxChannels x
  // kMaxChannels so growing numInputs_/numOutputs_ never has to migrate
  // data - cells outside the current numInputs_ x numOutputs_ window are
  // simply not addressed by the UI or processBlock(), but stay intact if
  // the window later grows back, so shrinking then growing restores them.
  std::vector<bool> matrix_;
  int numInputs_{kDefaultChannels};
  int numOutputs_{kDefaultChannels};

  // Guards numInputs_/numOutputs_/matrix_ against a UI edit (message
  // thread) landing while processBlock() (audio thread) is reading them.
  mutable std::mutex matrixMutex_;

  AudioBuffer<float> inputScratch_;

  // notifyMatrixChanged() defers to the message thread (setStateInformation
  // is not guaranteed to run there in every host) via a captured weak_ptr
  // to this flag, so a processor destroyed before that callback fires is
  // never touched by it.
  std::shared_ptr<bool> aliveFlag_ = std::make_shared<bool>(true);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MatrixRouterAudioProcessor)
};

}  // namespace matrix_router
