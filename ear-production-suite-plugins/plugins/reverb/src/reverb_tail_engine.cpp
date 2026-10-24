#include "reverb_tail_engine.hpp"

#include "JuceHeader.h"

#include "../third_party/MVerb.h"

#include <algorithm>
#include <cmath>

namespace ear {
namespace plugin {
namespace reverbdsp {

// ---------------------------------------------------------------------
// FdnTailEngine
// ---------------------------------------------------------------------
void FdnTailEngine::prepare(double sampleRate) {
  sampleRate_ = sampleRate;
  fdn_.prepare(sampleRate);
  inputDiffuser_.prepare(sampleRate);
  lowpassState_ = 0.0f;
  setBandwidth(1.0f);  // default: fully open, no extra darkening
}

void FdnTailEngine::reset() {
  fdn_.reset();
  inputDiffuser_.reset();
  lowpassState_ = 0.0f;
}

void FdnTailEngine::setDecaySeconds(float rt60) { fdn_.setDecaySeconds(rt60); }
void FdnTailEngine::setDamping(float damping01) { fdn_.setDamping(damping01); }
void FdnTailEngine::setRoomSize(float scale) { fdn_.setRoomSize(scale); }
void FdnTailEngine::setDensity(float density01) { fdn_.setDensity(density01); }

void FdnTailEngine::setBandwidth(float bandwidth01) {
  bandwidth01 = juce::jlimit(0.0f, 1.0f, bandwidth01);
  // bandwidth 0 -> ~200Hz cutoff (dark/muffled input), bandwidth 1 ->
  // essentially full bandwidth (cutoff at Nyquist, no audible filtering).
  float nyquist = static_cast<float>(sampleRate_ * 0.5);
  float cutoffHz = 200.0f + bandwidth01 * bandwidth01 * juce::jmax(1.0f, nyquist - 200.0f);
  bandwidthCoeff_ = std::exp(-2.0f * juce::MathConstants<float>::pi * cutoffHz /
                            static_cast<float>(sampleRate_ > 0 ? sampleRate_ : 48000.0));
}

void FdnTailEngine::process(const float* monoInput, int numSamples) {
  diffusedInput_.resize(static_cast<size_t>(numSamples));
  // "Bandwidth": a one-pole lowpass on the raw input, same stage MVerb
  // has as BANDWIDTHFREQ - applied before pre-diffusion, so it shapes
  // what actually reaches the FDN rather than just colouring the output.
  for (int n = 0; n < numSamples; ++n) {
    lowpassState_ = (1.0f - bandwidthCoeff_) * monoInput[n] + bandwidthCoeff_ * lowpassState_;
    diffusedInput_[static_cast<size_t>(n)] = lowpassState_;
  }
  // Pre-diffuse before the FDN so the tail builds up as a smooth wash
  // rather than a few discrete echoes (same as the original single-engine
  // processBlock did).
  inputDiffuser_.process(diffusedInput_.data(), diffusedInput_.data(), numSamples);
  fdn_.process(diffusedInput_.data(), numSamples);
}

float FdnTailEngine::getTap(int pointIndex, int n) const {
  int line = pointIndex % FeedbackDelayNetwork::kNumLines;
  return fdn_.getTapSample(line, n);
}

// ---------------------------------------------------------------------
// MVerbTailEngine
// ---------------------------------------------------------------------
struct MVerbTailEngine::Impl {
  MVerb<float> verb;
};

MVerbTailEngine::MVerbTailEngine() : impl_(std::make_unique<Impl>()) {}
MVerbTailEngine::~MVerbTailEngine() = default;

void MVerbTailEngine::prepare(double sampleRate) {
  impl_->verb.setSampleRate(static_cast<float>(sampleRate));
  // MVerb has its own internal early-reflections/dry-mix stages, which we
  // don't want: this reverb already has its own direction-weighted early
  // reflections and its own Direct path (see ReverbAudioProcessor), so
  // MVerb should contribute only its late tank output, fully wet.
  impl_->verb.setParameter(MVerb<float>::MIX, 1.0f);
  impl_->verb.setParameter(MVerb<float>::EARLYMIX, 1.0f);
  impl_->verb.setParameter(MVerb<float>::GAIN, 1.0f);
  impl_->verb.setParameter(MVerb<float>::BANDWIDTHFREQ, 1.0f);
  // Pinned to 0 (not left at whatever MVerb's own constructor default
  // happens to be): this reverb already applies its own pre-delay
  // upstream (see ReverbAudioProcessor::diffusePreDelay_), so MVerb's own
  // internal pre-delay stage would otherwise double it up unpredictably.
  impl_->verb.setParameter(MVerb<float>::PREDELAY, 0.0f);
}

void MVerbTailEngine::reset() { impl_->verb.reset(); }

void MVerbTailEngine::setDecaySeconds(float rt60) {
  // MVerb's own DECAY is a 0..1 feedback-amount knob, not a literal RT60
  // in seconds (different topologies decay differently for "the same"
  // knob position) - mapped from our 0.2-10s range onto its 0..1 range so
  // the UI slider still does something sensible and monotonic, rather
  // than claiming an exact RT60 match between two different algorithms.
  float norm = juce::jlimit(0.0f, 1.0f, (rt60 - 0.2f) / (10.0f - 0.2f));
  impl_->verb.setParameter(MVerb<float>::DECAY, norm);
}

void MVerbTailEngine::setDamping(float damping01) {
  // MVerb's DAMPINGFREQ is inverted relative to the naming here (its
  // internal cutoff = 1 - value, i.e. higher value = more damping is
  // still the right direction once through its own math) - passed
  // straight through, see MVerb.h's own setParameter(DAMPINGFREQ, ...).
  impl_->verb.setParameter(MVerb<float>::DAMPINGFREQ, juce::jlimit(0.0f, 1.0f, damping01));
}

void MVerbTailEngine::setRoomSize(float scale) {
  float norm = juce::jlimit(0.0f, 1.0f, (scale - 0.5f) / (2.0f - 0.5f));
  impl_->verb.setParameter(MVerb<float>::SIZE, norm);
}

void MVerbTailEngine::setDensity(float density01) {
  impl_->verb.setParameter(MVerb<float>::DENSITY, juce::jlimit(0.0f, 1.0f, density01));
}

void MVerbTailEngine::setBandwidth(float bandwidth01) {
  impl_->verb.setParameter(MVerb<float>::BANDWIDTHFREQ,
                           juce::jlimit(0.0f, 1.0f, bandwidth01));
}

void MVerbTailEngine::process(const float* monoInput, int numSamples) {
  size_t n = static_cast<size_t>(numSamples);
  if (inLeft_.size() != n) {
    inLeft_.resize(n);
    inRight_.resize(n);
    outLeft_.resize(n);
    outRight_.resize(n);
  }
  std::copy(monoInput, monoInput + numSamples, inLeft_.begin());
  std::copy(monoInput, monoInput + numSamples, inRight_.begin());

  float* inputs[2] = {inLeft_.data(), inRight_.data()};
  float* outputs[2] = {outLeft_.data(), outRight_.data()};
  impl_->verb.process(inputs, outputs, numSamples);
}

float MVerbTailEngine::getTap(int pointIndex, int n) const {
  return (pointIndex % 2 == 0) ? outLeft_[static_cast<size_t>(n)]
                               : outRight_[static_cast<size_t>(n)];
}

}  // namespace reverbdsp
}  // namespace plugin
}  // namespace ear
