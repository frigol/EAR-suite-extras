#include "ir_reverb_dsp.hpp"

#include "JuceHeader.h"

#include <boost/math/special_functions/factorials.hpp>
#include <boost/math/special_functions/legendre.hpp>

#include <algorithm>
#include <cmath>

namespace ear {
namespace plugin {
namespace irreverbdsp {

namespace {
constexpr double kPiD = 3.14159265358979323846;
constexpr float kPi = static_cast<float>(kPiD);

// Associated Legendre function P_n^m(x), omitting the (-1)^m
// Condon-Shortley phase term folded back in separately below - matches
// libear's own hoa.hpp Alegendre()/sph_harm(), reimplemented locally (see
// the header comment for why).
double legendreConSho(int n, int m, double x) {
  return std::pow(-1.0, m) * boost::math::legendre_p(n, m, x);
}

double normSN3D(int n, int absM) {
  return std::sqrt(boost::math::factorial<double>(n - absM) /
                   boost::math::factorial<double>(n + absM));
}

double sphericalHarmonicSN3D(int n, int m, double azRad, double elRad) {
  double scale;
  if (m > 0)
    scale = std::sqrt(2.0) * std::cos(m * azRad);
  else if (m < 0)
    scale = -std::sqrt(2.0) * std::sin(m * azRad);
  else
    scale = 1.0;
  return normSN3D(n, std::abs(m)) * legendreConSho(n, std::abs(m), std::sin(elRad)) *
        scale;
}

int toAcn(int n, int m) { return n * n + n + m; }
}  // namespace

int ambisonicChannelCount(int order) { return (order + 1) * (order + 1); }

int ambisonicOrderForChannelCount(int channels) {
  if (channels < 4) return -1;  // order 0 (1ch) is handled as plain mono
  int order = static_cast<int>(std::lround(std::sqrt(static_cast<double>(channels)))) - 1;
  if (order < 1) return -1;
  return ambisonicChannelCount(order) == channels ? order : -1;
}

std::vector<float> ambisonicEncodingSN3D(int order, float azimuthDeg,
                                         float elevationDeg) {
  std::vector<float> coeffs(static_cast<size_t>(ambisonicChannelCount(order)),
                            0.0f);
  double az = static_cast<double>(azimuthDeg) * kPiD / 180.0;
  double el = static_cast<double>(elevationDeg) * kPiD / 180.0;
  for (int n = 0; n <= order; ++n)
    for (int m = -n; m <= n; ++m)
      coeffs[static_cast<size_t>(toAcn(n, m))] =
          static_cast<float>(sphericalHarmonicSN3D(n, m, az, el));
  return coeffs;
}

std::vector<float> ambisonicDecodingTapered(int order, float azimuthDeg,
                                            float elevationDeg) {
  auto coeffs = ambisonicEncodingSN3D(order, azimuthDeg, elevationDeg);
  for (int n = 0; n <= order; ++n) {
    double g = std::cos(static_cast<double>(n) * kPiD /
                        (2.0 * static_cast<double>(order + 1)));
    for (int m = -n; m <= n; ++m)
      coeffs[static_cast<size_t>(toAcn(n, m))] *= static_cast<float>(g);
  }
  return coeffs;
}

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

std::vector<float> directionalBlendWeights(Direction const& fromDir,
                                           std::vector<Direction> const& toDirs,
                                           float sharpness, float floorAmount) {
  std::vector<float> weights(toDirs.size(), 0.0f);
  float sum = 0.0f;
  for (size_t i = 0; i < toDirs.size(); ++i) {
    float w = angularWeight(fromDir, toDirs[i], sharpness);
    w = floorAmount + (1.0f - floorAmount) * w;
    weights[i] = w;
    sum += w;
  }
  if (sum > 1.0e-6f)
    for (auto& w : weights) w /= sum;
  return weights;
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
    buffer_[static_cast<size_t>(writePos_)] = in[n];
    int readPos = writePos_ - delaySamples_;
    while (readPos < 0) readPos += bufSize;
    out[n] = buffer_[static_cast<size_t>(readPos)];
    writePos_ = (writePos_ + 1) % bufSize;
  }
}

}  // namespace irreverbdsp
}  // namespace plugin
}  // namespace ear
