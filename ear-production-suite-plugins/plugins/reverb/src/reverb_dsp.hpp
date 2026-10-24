#pragma once

#include <array>
#include <vector>

namespace ear {
namespace plugin {
namespace reverbdsp {

/** @brief Unit direction vector for a source or listening point, so their
 * angular separation can be found with a plain dot product regardless of
 * the absolute azimuth/elevation sign convention (both sides use the same
 * fromAzEl(), so only the *relative* angle between them matters). */
struct Direction {
  float x, y, z;
  static Direction fromAzEl(float azimuthDeg, float elevationDeg);
};

/** @brief Direction concentration weight in [0,1]: 1 when a and b point
 * the same way, falling off as they diverge. `sharpness` controls how
 * concentrated the falloff is - low sharpness (large/diffuse source)
 * spreads the weight almost evenly over all directions, high sharpness
 * (small/point source) concentrates it tightly around a's direction. */
float angularWeight(Direction const& a, Direction const& b, float sharpness);

/** @brief A short decorrelated multi-tap delay line: the early-reflection
 * generator for one of the 9 output points. Each point owns its own
 * instance (seeded by point index so their tap patterns differ), fed by
 * that point's own direction-weighted mix of the active sources - the
 * directionality comes from what's fed in, not from the tap pattern
 * itself. Each tap is softened by a shared one-pole lowpass (real room
 * reflections lose HF energy off absorptive surfaces), so the cluster
 * reads as reflections rather than a stack of raw delayed copies. */
class EarlyReflections {
 public:
  void prepare(double sampleRate, int instanceSeed);
  void reset();
  // Adds this block's early-reflection output into `out` (out[n] += ...).
  void process(const float* in, float* out, int numSamples);

 private:
  struct Tap {
    int delaySamples;
    float gain;
  };
  std::vector<Tap> taps_;
  std::vector<float> buffer_;
  int writePos_ = 0;
  float lowpassState_ = 0.0f;
};

/** @brief A short chain of Schroeder allpass filters: smears a signal in
 * time without colouring its spectrum, the standard way to pre-diffuse a
 * reverb's input before it hits the feedback network so the tail builds
 * up as a smooth wash instead of a few discrete, audible echoes
 * (especially noticeable on short/percussive input and short decays). */
class AllpassDiffuser {
 public:
  void prepare(double sampleRate);
  void reset();
  // In-place; safe to call with out == in.
  void process(const float* in, float* out, int numSamples);

 private:
  static constexpr int kNumStages = 4;
  struct Stage {
    std::vector<float> buffer;
    int length = 1;
    int writePos = 0;
  };
  std::array<Stage, kNumStages> stages_;
  static constexpr float kCoefficient = 0.6f;
};

/** @brief Shared diffuse reverb tail: one feedback delay network (8 lines,
 * an energy-preserving Householder feedback matrix, each line's delay
 * subtly modulated by its own slow LFO) feeding all 9 points, rather than
 * 9 independent reverbs - physically there is one room, and the 9 points
 * are 9 decorrelated taps into the same reverberant field. The modulation
 * is what keeps a feedback network like this from ringing metallically:
 * without it, every round trip hits the exact same sample positions and
 * reinforces a handful of resonant frequencies; with it, the delay times
 * drift by a couple of samples so no single frequency is favoured for
 * long. RT60 and HF damping are the technical controls; setRoomSize()/
 * setDensity() are the qualitative ones layered on top - finer per-band
 * control is a later phase (IRCAM-Verb-style parameters). */
class FeedbackDelayNetwork {
 public:
  static constexpr int kNumLines = 8;
  // How far setRoomSize() can stretch the base delay times; buffers are
  // allocated at this size up front so size changes never reallocate.
  static constexpr float kMaxRoomSize = 2.0f;

  void prepare(double sampleRate);
  void reset();
  void setDecaySeconds(float rt60);
  void setDamping(float damping01);
  // Scales the lines' delay times (0.5..kMaxRoomSize, 1 = design size):
  // a bigger room spaces out the tail's internal echoes and lengthens the
  // time it takes to build up, a smaller one tightens/speeds it up.
  void setRoomSize(float scale);
  // 1 = full Householder mixing (smooth, dense tail); lower values fade
  // toward each line feeding back only into itself (a sparser, more
  // comb-like character) - the other qualitative knob.
  void setDensity(float density01);

  // Runs the network for this block; results are read back per-sample via
  // getTapSample() for each of the 9 points immediately afterwards.
  void process(const float* input, int numSamples);
  float getTapSample(int line, int n) const;

 private:
  std::array<std::vector<float>, kNumLines> delayBuffer_;
  std::array<int, kNumLines> baseDelayLength_{};
  std::array<int, kNumLines> delayLength_{};
  std::array<int, kNumLines> writePos_{};
  std::array<float, kNumLines> feedbackGain_{};
  std::array<float, kNumLines> dampingState_{};
  std::array<std::vector<float>, kNumLines> blockOutput_;
  // Per-line delay-time modulation (depth in samples, phase, rate), so
  // the feedback loop never settles into a fixed, ringing comb pattern.
  // Two summed LFOs per line (different, non-commensurate rates) rather
  // than one: a single sine still revisits the same delay offset twice
  // per cycle, which on a short loop is regular enough to be audible as
  // a periodic colouration of its own: summing two breaks that up.
  std::array<float, kNumLines> modPhase_{};
  std::array<float, kNumLines> modRateHz_{};
  std::array<float, kNumLines> modDepthSamples_{};
  std::array<float, kNumLines> modPhase2_{};
  std::array<float, kNumLines> modRateHz2_{};
  std::array<float, kNumLines> modDepthSamples2_{};
  // Energy-preserving Householder reflection matrix (computed once in
  // prepare(), generic for any kNumLines - replaces a hand-written
  // Hadamard table, which only exists for powers of two).
  std::array<std::array<float, kNumLines>, kNumLines> householder_{};
  float damping_ = 0.25f;
  float rt60_ = 1.8f;
  float roomSize_ = 1.0f;
  float density_ = 1.0f;
  double sampleRate_ = 48000.0;
};

/** @brief A plain single-tap delay line (up to maxDelayMs), used for the
 * reverb's pre-delay - the gap between the dry sound and when the
 * reflections/tail start, a standard qualitative "distance from the
 * walls" control. */
class SimpleDelayLine {
 public:
  void prepare(double sampleRate, float maxDelayMs);
  void reset();
  void setDelayMs(float ms);
  // Safe to call with out == in (reads before it writes each sample).
  void process(const float* in, float* out, int numSamples);

 private:
  std::vector<float> buffer_;
  int writePos_ = 0;
  int delaySamples_ = 0;
  double sampleRate_ = 48000.0;
};

}  // namespace reverbdsp
}  // namespace plugin
}  // namespace ear
