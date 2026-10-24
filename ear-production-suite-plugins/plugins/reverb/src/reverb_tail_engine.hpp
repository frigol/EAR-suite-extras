#pragma once

#include "reverb_dsp.hpp"
#include <memory>
#include <vector>

namespace ear {
namespace plugin {
namespace reverbdsp {

enum class TailEngineType {
  CustomFdn = 0,   // this project's own hand-written FeedbackDelayNetwork
  MVerbPlate = 1,  // vendored MVerb (Dattorro-style plate) - see third_party/
};

/** @brief Common interface for the diffuse-tail generator, so the 10
 * output points can be driven by whichever engine the user picks from
 * the Mix panel's "Tail engine" menu without the rest of the reverb
 * (input handling, direction-weighted early reflections, Scene
 * integration) knowing or caring which one is active. Both engines are
 * kept prepared at all times (cheap - diffuse tail state is small) so
 * switching is instant and glitch-free, not a rebuild. */
class TailEngine {
 public:
  virtual ~TailEngine() = default;

  virtual void prepare(double sampleRate) = 0;
  virtual void reset() = 0;

  // All setters take the same normalized ranges DspParams already uses
  // (decaySeconds in seconds, damping/density 0..1, roomSize 0.5..2.0),
  // so the UI doesn't need to know which engine is listening.
  virtual void setDecaySeconds(float rt60) = 0;
  virtual void setDamping(float damping01) = 0;
  virtual void setRoomSize(float scale) = 0;
  virtual void setDensity(float density01) = 0;
  // Input brightness into the tank/network, 0 = dark, 1 = full bandwidth.
  // MVerb has its own dedicated control for this (BANDWIDTHFREQ); the
  // custom FDN has no equivalent stage, so it ignores this (a no-op) -
  // the UI control stays visible either way rather than appearing/
  // disappearing when the engine is switched.
  virtual void setBandwidth(float bandwidth01) = 0;

  // Feeds this block's mono diffuse-send mix into the engine.
  virtual void process(const float* monoInput, int numSamples) = 0;

  // Reads back sample n of this block for output point `pointIndex` -
  // each engine decorrelates its own, generally smaller, set of internal
  // taps across however many points are asked for.
  virtual float getTap(int pointIndex, int n) const = 0;
};

/** @brief Wraps this project's own FeedbackDelayNetwork (8 lines,
 * Householder matrix, modulated delays) - see reverb_dsp.hpp for the
 * actual algorithm. The default engine, and the one all of Phase 2/3's
 * tuning work (metallic-ringing fixes, pre-diffusion, etc.) was done on. */
class FdnTailEngine : public TailEngine {
 public:
  void prepare(double sampleRate) override;
  void reset() override;
  void setDecaySeconds(float rt60) override;
  void setDamping(float damping01) override;
  void setRoomSize(float scale) override;
  void setDensity(float density01) override;
  // A one-pole lowpass on the input, same role as MVerb's own
  // BANDWIDTHFREQ stage - this engine had no equivalent control before;
  // it's a perfectly ordinary thing to want on any reverb's input, not
  // something MVerb-specific, so it's real here too rather than a no-op.
  void setBandwidth(float bandwidth01) override;
  void process(const float* monoInput, int numSamples) override;
  float getTap(int pointIndex, int n) const override;

 private:
  FeedbackDelayNetwork fdn_;
  AllpassDiffuser inputDiffuser_;
  std::vector<float> diffusedInput_;
  double sampleRate_{48000.0};
  float bandwidthCoeff_{0.0f};  // one-pole lowpass coefficient, see setBandwidth()
  float lowpassState_{0.0f};
};

/** @brief Wraps the vendored MVerb (see third_party/MVerb.h, Martin
 * Eastwood, GPLv3) - a more conventional, widely-used Dattorro-style
 * plate reverb, offered as an alternative "character" to A/B against the
 * custom FDN above. MVerb is inherently stereo (2 internal tank/output
 * channels); its 2 taps are shared out across however many output points
 * are asked for (alternating L/R), the same way the FDN shares its own 8
 * lines across more points than it has. */
class MVerbTailEngine : public TailEngine {
 public:
  MVerbTailEngine();
  ~MVerbTailEngine() override;

  void prepare(double sampleRate) override;
  void reset() override;
  void setDecaySeconds(float rt60) override;
  void setDamping(float damping01) override;
  void setRoomSize(float scale) override;
  void setDensity(float density01) override;
  void setBandwidth(float bandwidth01) override;
  void process(const float* monoInput, int numSamples) override;
  float getTap(int pointIndex, int n) const override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::vector<float> inLeft_, inRight_, outLeft_, outRight_;
};

}  // namespace reverbdsp
}  // namespace plugin
}  // namespace ear
