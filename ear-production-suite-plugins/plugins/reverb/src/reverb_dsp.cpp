#include "reverb_dsp.hpp"

#include "JuceHeader.h"

#include <algorithm>
#include <cmath>

namespace ear {
namespace plugin {
namespace reverbdsp {

namespace {
constexpr float kPi = 3.14159265358979323846f;

// Base (48kHz) delay lengths for the 8 FDN lines, chosen mutually
// prime-ish to avoid audible periodicity, scaled to the actual sample
// rate at prepare() time.
constexpr int kBaseDelayLengths[FeedbackDelayNetwork::kNumLines] = {
    1511, 1627, 1747, 1861, 1993, 2131, 2273, 2423};

// Allpass pre-diffuser stage lengths (ms), mutually non-harmonic.
constexpr float kAllpassStageMs[4] = {4.7f, 7.3f, 11.1f, 17.9f};
}  // namespace

Direction Direction::fromAzEl(float azimuthDeg, float elevationDeg) {
  const float az = azimuthDeg * kPi / 180.0f;
  const float el = elevationDeg * kPi / 180.0f;
  return {std::cos(el) * std::cos(az), std::cos(el) * std::sin(az),
          std::sin(el)};
}

float angularWeight(Direction const& a, Direction const& b, float sharpness) {
  float dot = a.x * b.x + a.y * b.y + a.z * b.z;
  dot = juce::jlimit(-1.0f, 1.0f, dot);
  float concentration = (dot + 1.0f) * 0.5f;  // 0..1
  return std::pow(juce::jmax(0.0001f, concentration), sharpness);
}

void EarlyReflections::prepare(double sampleRate, int instanceSeed) {
  taps_.clear();
  // A denser set of pseudo-random, decorrelated-per-instance early
  // reflections spread over roughly the first 55ms, decaying in level -
  // few taps with a regular power-law gain spacing tend to read as a
  // ringing comb rather than a cluster of reflections; more taps with
  // irregular (randomised) delay AND gain spacing spreads their combined
  // spectral colouration out instead of reinforcing a handful of peaks.
  constexpr int kNumTaps = 12;
  uint32_t seed = static_cast<uint32_t>(instanceSeed) * 2654435761u + 1u;
  auto nextRandom01 = [&seed]() {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<float>(seed >> 8) / static_cast<float>(1u << 24);
  };
  for (int i = 0; i < kNumTaps; ++i) {
    float frac = (i + nextRandom01()) / static_cast<float>(kNumTaps);
    float delayMs = 2.0f + frac * 53.0f;
    int delaySamples =
        juce::jmax(1, static_cast<int>(delayMs * 0.001 * sampleRate));
    // Gain decay randomised per tap (not a fixed power-law ratio between
    // consecutive taps), so the taps don't line up into an evenly-spaced
    // comb.
    float gain = std::pow(0.72f, static_cast<float>(i)) *
                (0.6f + 0.6f * nextRandom01());
    taps_.push_back({delaySamples, gain});
  }

  int maxDelay = 1;
  for (auto const& t : taps_) maxDelay = juce::jmax(maxDelay, t.delaySamples);
  buffer_.assign(static_cast<size_t>(maxDelay) + 1, 0.0f);
  writePos_ = 0;
}

void EarlyReflections::reset() {
  std::fill(buffer_.begin(), buffer_.end(), 0.0f);
  writePos_ = 0;
  lowpassState_ = 0.0f;
}

void EarlyReflections::process(const float* in, float* out, int numSamples) {
  if (buffer_.empty()) return;
  const int bufSize = static_cast<int>(buffer_.size());
  for (int n = 0; n < numSamples; ++n) {
    buffer_[static_cast<size_t>(writePos_)] = in[n];

    float sum = 0.0f;
    for (auto const& tap : taps_) {
      int readPos = writePos_ - tap.delaySamples;
      while (readPos < 0) readPos += bufSize;
      sum += tap.gain * buffer_[static_cast<size_t>(readPos)];
    }

    // Real reflections lose high-frequency energy off absorptive
    // surfaces - a one-pole lowpass on the summed taps reads as soft
    // reflections rather than a stack of raw delayed copies.
    lowpassState_ = 0.5f * sum + 0.5f * lowpassState_;
    out[n] += lowpassState_;

    writePos_ = (writePos_ + 1) % bufSize;
  }
}

void AllpassDiffuser::prepare(double sampleRate) {
  for (int s = 0; s < kNumStages; ++s) {
    int length = juce::jmax(
        1, static_cast<int>(kAllpassStageMs[s] * 0.001 * sampleRate));
    stages_[static_cast<size_t>(s)].length = length;
    stages_[static_cast<size_t>(s)].buffer.assign(static_cast<size_t>(length),
                                                  0.0f);
    stages_[static_cast<size_t>(s)].writePos = 0;
  }
}

void AllpassDiffuser::reset() {
  for (auto& stage : stages_) {
    std::fill(stage.buffer.begin(), stage.buffer.end(), 0.0f);
    stage.writePos = 0;
  }
}

void AllpassDiffuser::process(const float* in, float* out, int numSamples) {
  for (int n = 0; n < numSamples; ++n) {
    float x = in[n];
    for (auto& stage : stages_) {
      if (stage.buffer.empty()) continue;
      float delayed = stage.buffer[static_cast<size_t>(stage.writePos)];
      float w = x + kCoefficient * delayed;
      float y = -kCoefficient * w + delayed;
      stage.buffer[static_cast<size_t>(stage.writePos)] = w;
      stage.writePos = (stage.writePos + 1) % stage.length;
      x = y;
    }
    out[n] = x;
  }
}

void FeedbackDelayNetwork::prepare(double sampleRate) {
  sampleRate_ = sampleRate;
  // A larger safety margin than roomSize alone needs, so the modulated
  // (fractionally offset) read position never runs out of buffer to look
  // back into even at the largest room size.
  constexpr float kModulationHeadroom = 1.05f;
  for (int l = 0; l < kNumLines; ++l) {
    int base = juce::jmax(
        1, static_cast<int>(kBaseDelayLengths[l] * sampleRate / 48000.0));
    baseDelayLength_[static_cast<size_t>(l)] = base;
    int capacity = juce::jmax(
        base + 16,
        static_cast<int>(base * kMaxRoomSize * kModulationHeadroom) + 16);
    delayBuffer_[static_cast<size_t>(l)].assign(static_cast<size_t>(capacity),
                                                0.0f);
    delayLength_[static_cast<size_t>(l)] = base;
    writePos_[static_cast<size_t>(l)] = 0;
    dampingState_[static_cast<size_t>(l)] = 0.0f;

    // Slow, mutually-irrational-ish modulation rates/phases so the 8
    // lines never drift back into sync with each other, and two summed
    // LFOs per line (rather than one) so each line's own modulation
    // doesn't itself read as periodic.
    modRateHz_[static_cast<size_t>(l)] = 0.13f + 0.091f * static_cast<float>(l);
    modDepthSamples_[static_cast<size_t>(l)] = 2.4f;
    modPhase_[static_cast<size_t>(l)] = static_cast<float>(l) * 0.91f;
    modRateHz2_[static_cast<size_t>(l)] = 0.27f + 0.137f * static_cast<float>(l);
    modDepthSamples2_[static_cast<size_t>(l)] = 1.3f;
    modPhase2_[static_cast<size_t>(l)] = static_cast<float>(l) * 1.53f + 2.0f;
  }

  // Energy-preserving Householder reflection matrix H = I - (2/N)*J
  // (J = all-ones matrix) - the generic equivalent of a Hadamard matrix
  // for any line count, used here so the line count isn't pinned to a
  // power of two.
  const float h = 2.0f / static_cast<float>(kNumLines);
  for (int i = 0; i < kNumLines; ++i)
    for (int j = 0; j < kNumLines; ++j)
      householder_[static_cast<size_t>(i)][static_cast<size_t>(j)] =
          (i == j ? 1.0f : 0.0f) - h;

  setRoomSize(roomSize_);
  setDamping(damping_);
}

void FeedbackDelayNetwork::reset() {
  for (int l = 0; l < kNumLines; ++l) {
    std::fill(delayBuffer_[static_cast<size_t>(l)].begin(),
              delayBuffer_[static_cast<size_t>(l)].end(), 0.0f);
    writePos_[static_cast<size_t>(l)] = 0;
    dampingState_[static_cast<size_t>(l)] = 0.0f;
  }
}

void FeedbackDelayNetwork::setDecaySeconds(float rt60) {
  rt60_ = juce::jmax(0.1f, rt60);
  for (int l = 0; l < kNumLines; ++l) {
    float delayTimeSeconds =
        static_cast<float>(delayLength_[static_cast<size_t>(l)]) /
        static_cast<float>(sampleRate_ > 0 ? sampleRate_ : 48000.0);
    // Standard comb/FDN per-line feedback gain for a target RT60.
    feedbackGain_[static_cast<size_t>(l)] =
        std::pow(10.0f, -3.0f * delayTimeSeconds / rt60_);
  }
}

void FeedbackDelayNetwork::setDamping(float damping01) {
  damping_ = juce::jlimit(0.0f, 0.95f, damping01);
}

void FeedbackDelayNetwork::setRoomSize(float scale) {
  roomSize_ = juce::jlimit(0.5f, kMaxRoomSize, scale);
  for (int l = 0; l < kNumLines; ++l) {
    int capacity = static_cast<int>(delayBuffer_[static_cast<size_t>(l)].size());
    int newLength = juce::jlimit(
        1, capacity - 1,
        static_cast<int>(baseDelayLength_[static_cast<size_t>(l)] * roomSize_));
    delayLength_[static_cast<size_t>(l)] = newLength;
    writePos_[static_cast<size_t>(l)] %= capacity;
  }
  setDecaySeconds(rt60_);  // feedback gain depends on each line's delay time
}

void FeedbackDelayNetwork::setDensity(float density01) {
  density_ = juce::jlimit(0.0f, 1.0f, density01);
}

void FeedbackDelayNetwork::process(const float* input, int numSamples) {
  for (int l = 0; l < kNumLines; ++l)
    blockOutput_[static_cast<size_t>(l)].resize(static_cast<size_t>(numSamples));

  const float inputGain = 1.0f / std::sqrt(static_cast<float>(kNumLines));

  for (int n = 0; n < numSamples; ++n) {
    std::array<float, kNumLines> lineOut{};
    for (int l = 0; l < kNumLines; ++l) {
      auto& buf = delayBuffer_[static_cast<size_t>(l)];
      const int bufSize = static_cast<int>(buf.size());

      // Read with a slowly-modulated fractional offset (linear
      // interpolation) rather than a fixed integer delay: this is what
      // keeps an FDN this small from settling into a handful of ringing
      // resonant frequencies - every round trip lands on a slightly
      // different sample position instead of reinforcing the same one.
      float modOffset = modDepthSamples_[static_cast<size_t>(l)] *
                            std::sin(modPhase_[static_cast<size_t>(l)]) +
                        modDepthSamples2_[static_cast<size_t>(l)] *
                            std::sin(modPhase2_[static_cast<size_t>(l)]);
      float readPosF = static_cast<float>(writePos_[static_cast<size_t>(l)]) -
                       static_cast<float>(delayLength_[static_cast<size_t>(l)]) -
                       modOffset;
      readPosF = std::fmod(readPosF, static_cast<float>(bufSize));
      if (readPosF < 0.0f) readPosF += static_cast<float>(bufSize);
      int i0 = static_cast<int>(readPosF);
      int i1 = (i0 + 1) % bufSize;
      float frac = readPosF - static_cast<float>(i0);
      lineOut[static_cast<size_t>(l)] =
          buf[static_cast<size_t>(i0)] * (1.0f - frac) +
          buf[static_cast<size_t>(i1)] * frac;
      blockOutput_[static_cast<size_t>(l)][static_cast<size_t>(n)] =
          lineOut[static_cast<size_t>(l)];

      float srInv = 1.0f / static_cast<float>(sampleRate_ > 0 ? sampleRate_ : 48000.0);
      modPhase_[static_cast<size_t>(l)] +=
          2.0f * kPi * modRateHz_[static_cast<size_t>(l)] * srInv;
      if (modPhase_[static_cast<size_t>(l)] > 2.0f * kPi)
        modPhase_[static_cast<size_t>(l)] -= 2.0f * kPi;
      modPhase2_[static_cast<size_t>(l)] +=
          2.0f * kPi * modRateHz2_[static_cast<size_t>(l)] * srInv;
      if (modPhase2_[static_cast<size_t>(l)] > 2.0f * kPi)
        modPhase2_[static_cast<size_t>(l)] -= 2.0f * kPi;
    }

    for (int l = 0; l < kNumLines; ++l) {
      // density_ == 1: full Householder cross-mixing (smooth/dense tail).
      // density_ == 0: each line feeds back only into itself, i.e. a
      // plain comb filter per line (sparse, more resonant character).
      float crossMixed = 0.0f;
      for (int k = 0; k < kNumLines; ++k)
        crossMixed += householder_[static_cast<size_t>(l)][static_cast<size_t>(k)] *
                     lineOut[static_cast<size_t>(k)];
      float mixed = density_ * crossMixed +
                   (1.0f - density_) * lineOut[static_cast<size_t>(l)];
      mixed *= feedbackGain_[static_cast<size_t>(l)];

      // One-pole lowpass in the feedback path approximates HF air
      // absorption so the tail darkens as it decays.
      float& damp = dampingState_[static_cast<size_t>(l)];
      damp = (1.0f - damping_) * mixed + damping_ * damp;

      float inputToLine = input[n] * inputGain;
      auto& buf = delayBuffer_[static_cast<size_t>(l)];
      buf[static_cast<size_t>(writePos_[static_cast<size_t>(l)])] =
          inputToLine + damp;
      writePos_[static_cast<size_t>(l)] =
          (writePos_[static_cast<size_t>(l)] + 1) %
          static_cast<int>(buf.size());
    }
  }
}

float FeedbackDelayNetwork::getTapSample(int line, int n) const {
  return blockOutput_[static_cast<size_t>(line)][static_cast<size_t>(n)];
}

void SimpleDelayLine::prepare(double sampleRate, float maxDelayMs) {
  sampleRate_ = sampleRate;
  int capacity = juce::jmax(
      2, static_cast<int>(maxDelayMs * 0.001 * sampleRate) + 1);
  buffer_.assign(static_cast<size_t>(capacity), 0.0f);
  writePos_ = 0;
  delaySamples_ = 0;
}

void SimpleDelayLine::reset() {
  std::fill(buffer_.begin(), buffer_.end(), 0.0f);
  writePos_ = 0;
}

void SimpleDelayLine::setDelayMs(float ms) {
  int capacity = static_cast<int>(buffer_.size());
  delaySamples_ = juce::jlimit(
      0, juce::jmax(0, capacity - 1),
      static_cast<int>(ms * 0.001 * (sampleRate_ > 0 ? sampleRate_ : 48000.0)));
}

void SimpleDelayLine::process(const float* in, float* out, int numSamples) {
  if (buffer_.empty()) {
    if (out != in) std::copy(in, in + numSamples, out);
    return;
  }
  const int bufSize = static_cast<int>(buffer_.size());
  for (int n = 0; n < numSamples; ++n) {
    // Write first, then read back delaySamples_ behind the position just
    // written - that way delaySamples_ == 0 reads the sample that was
    // just written (true zero-latency passthrough). Reading before
    // writing (the previous order) made 0 read whatever was left over
    // from a full buffer-length ago instead, i.e. a fixed ~200ms delay
    // no matter what the pre-delay control was set to.
    buffer_[static_cast<size_t>(writePos_)] = in[n];
    int readPos = writePos_ - delaySamples_;
    while (readPos < 0) readPos += bufSize;
    out[n] = buffer_[static_cast<size_t>(readPos)];
    writePos_ = (writePos_ + 1) % bufSize;
  }
}

}  // namespace reverbdsp
}  // namespace plugin
}  // namespace ear
