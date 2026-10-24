#pragma once

#include <vector>

namespace ear {
namespace plugin {
namespace irreverbdsp {

/** @brief Unit direction vector for a source or output point - copied from
 * the algorithmic Reverb's own reverb_dsp.hpp (Direction/angularWeight):
 * small, self-contained, DSP-agnostic math, not worth sharing a header
 * over between two otherwise-independent plugins. */
struct Direction {
  float x, y, z;
  static Direction fromAzEl(float azimuthDeg, float elevationDeg);
};

/** @brief Direction concentration weight in [0,1] - see the algorithmic
 * Reverb's own copy of this function for the full explanation. Used here
 * to build each output point's own direction-weighted sum of the active
 * sources BEFORE convolution ("encode-then-sum-then-convolve"), so a
 * loaded impulse response still responds differently per point depending
 * on where each source sits, while the actual convolution cost stays at
 * one pass per point regardless of how many sources are active. */
float angularWeight(Direction const& a, Direction const& b, float sharpness);

// Normalized (sums to 1, floored so no entry reaches an exact 0 - see
// ambisonicEncodingSN3D()'s comment for why an exact null is a real bug
// here, not a nitpick) angularWeight() against each of `toDirs`. Used
// symmetrically for discrete-array IRs: to ENCODE a source into the
// array's own channels (toDirs = the array's known channel directions)
// and, the same function, to DECODE each output point's own share back
// out of the convolved array channels (toDirs unchanged, `fromDir` is
// the point's direction instead of the source's).
std::vector<float> directionalBlendWeights(Direction const& fromDir,
                                           std::vector<Direction> const& toDirs,
                                           float sharpness, float floorAmount);

// --- Ambisonics (AmbiX: ACN channel order, SN3D normalization) ---------
// Self-contained (uses Boost.Math's legendre_p/factorial, already a
// project-wide dependency via ear-plugin-base) rather than reaching into
// libear's own HOA implementation (submodules/.../libear/src/hoa/hoa.hpp),
// which exists and does the same maths (plus a full AllRAD decoder) but
// lives under libear's private src/ tree, not its public include/ API.

// (order+1)^2 - the number of ACN channels for a full HOA set of this order.
int ambisonicChannelCount(int order);

// The HOA order whose channel count is exactly `channels`, or -1 if
// `channels` isn't a valid (order+1)^2 for any order >= 1 (mono/stereo/
// non-HOA multichannel files all return -1 here).
int ambisonicOrderForChannelCount(int channels);

// All (order+1)^2 real spherical harmonic (ACN/SN3D) coefficients for a
// unit-gain point source at the given direction. Used to ENCODE a source
// into the shared ambisonic bus - do not use this for decoding (see
// ambisonicDecodingTapered() below): a flat/untapered "basic" decode
// using these same coefficients has an exact null at the point
// diametrically opposite the source (provably zero for order 1 - the
// sum of the squared direction components is exactly 1, so the decode
// at the antipodal point is (1 - 1)/N - confirmed as a real audible bug,
// not just a quality nitpick: a point directly opposite the source
// produced literal silence instead of a quieter tail).
std::vector<float> ambisonicEncodingSN3D(int order, float azimuthDeg,
                                         float elevationDeg);

// Decode weights for one output point: the same spherical harmonics as
// ambisonicEncodingSN3D(), but with a per-order taper (g_n = cos(n*pi /
// (2*(order+1))), order 0 always at g=1) so decode weights never reach
// an exact null anywhere on the sphere, including the antipodal point -
// see ambisonicEncodingSN3D()'s comment for why that matters. This is a
// simple, order-general taper chosen for being closed-form and provably
// null-free, not a literature "max-rE" decoder (which needs the largest
// root of a Legendre polynomial - deferred, this is a pragmatic fix for
// the null, not a claim of optimal speaker-level evenness).
std::vector<float> ambisonicDecodingTapered(int order, float azimuthDeg,
                                            float elevationDeg);

/** @brief A plain delay line for pre-delay, applied to each point's
 * direction-weighted dry feed before it reaches that point's convolution
 * - copied from the algorithmic Reverb's own SimpleDelayLine. */
class SimpleDelayLine {
 public:
  void prepare(double sampleRate, float maxDelayMs);
  void reset();
  void setDelayMs(float ms);
  void process(const float* in, float* out, int numSamples);

 private:
  std::vector<float> buffer_;
  int writePos_ = 0;
  int delaySamples_ = 0;
  double sampleRate_ = 48000.0;
};

}  // namespace irreverbdsp
}  // namespace plugin
}  // namespace ear
