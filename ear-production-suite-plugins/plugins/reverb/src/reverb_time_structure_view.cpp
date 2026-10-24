#include "reverb_time_structure_view.hpp"
#include "components/look_and_feel/colours.hpp"
#include "components/look_and_feel/fonts.hpp"

#include <cmath>

using namespace ear::plugin::ui;

namespace {
constexpr float kDbRange = 60.0f;  // 0dB at the top down to -60dB at the bottom

float linearToDb(float amp) {
  return 20.0f * std::log10(juce::jmax(1.0e-5f, amp));
}

// Deterministic (same input -> same output, so it doesn't flicker on
// every repaint) pseudo-random jitter in [-1, 1], standing in for the
// diffuse tail's actual stochastic texture - real tail energy isn't a
// smooth curve, it's noise-like once past the first few reflections.
float noise(float x) {
  float s = std::sin(x * 127.1f) * 43758.5453f;
  return 2.0f * (s - std::floor(s)) - 1.0f;
}

// Picks a "nice" tick spacing (1/2/5 x a power of ten) giving roughly
// kTargetTicks divisions over range.
float niceTickStep(float range, int targetTicks) {
  if (range <= 0.0f) return 1.0f;
  float rough = range / static_cast<float>(targetTicks);
  float mag = std::pow(10.0f, std::floor(std::log10(rough)));
  float residual = rough / mag;
  float step = (residual < 1.5f) ? 1.0f : (residual < 3.5f) ? 2.0f : (residual < 7.5f) ? 5.0f : 10.0f;
  return step * mag;
}
}  // namespace

ReverbTimeStructureView::ReverbTimeStructureView(ReverbAudioProcessor* p)
    : p_(p) {}

void ReverbTimeStructureView::paint(juce::Graphics& g) {
  g.fillAll(EarColours::ObjectItemBackground);

  auto dsp = p_->getDspParams();
  const float preDelaySec = dsp.preDelayMs / 1000.0f;
  // Room size also stretches how spread-out the early-reflection cluster
  // is, same as it stretches the diffuse tail's internal echo spacing.
  const float earlyDurationSec = 0.04f * dsp.roomSize;
  const float tailStart = preDelaySec + earlyDurationSec;
  const float tailEndSec = tailStart + dsp.decaySeconds;
  const float totalSec = juce::jmax(0.5f, tailEndSec);

  constexpr float kYAxisWidth = 32.0f;
  constexpr float kXAxisHeight = 16.0f;
  auto full = getLocalBounds().toFloat().reduced(6.0f, 6.0f);
  auto yAxisArea = full.removeFromLeft(kYAxisWidth);
  auto xAxisArea = full.removeFromBottom(kXAxisHeight);
  auto area = full;  // the plot itself

  const float pps = area.getWidth() / totalSec;  // pixels per second
  const float topY = area.getY();                // 0dB
  const float plotHeight = area.getHeight();
  auto timeToX = [&](float t) { return area.getX() + t * pps; };
  auto dbToY = [&](float db) {
    return topY + juce::jlimit(0.0f, 1.0f, -db / kDbRange) * plotHeight;
  };

  // --- Y axis: level in dB -------------------------------------------
  g.setFont(EarFontsSingleton::instance().Measures);
  for (float db = 0.0f; db >= -kDbRange; db -= 20.0f) {
    float y = dbToY(db);
    g.setColour(EarColours::Sphere.withAlpha(db == 0.0f ? 0.8f : 0.35f));
    g.drawLine(area.getX(), y, area.getRight(), y, 1.0f);
    g.setColour(EarColours::Label);
    g.drawText(juce::String(static_cast<int>(db)),
              juce::Rectangle<float>(yAxisArea.getX(), y - 6.0f,
                                     yAxisArea.getWidth() - 4.0f, 12.0f),
              juce::Justification::right);
  }
  g.setColour(EarColours::Label);
  {
    juce::Graphics::ScopedSaveState save(g);
    g.addTransform(juce::AffineTransform::rotation(
        -juce::MathConstants<float>::halfPi, yAxisArea.getX() + 9.0f,
        area.getCentreY()));
    g.drawText("dB", juce::Rectangle<float>(yAxisArea.getX() - 30.0f,
                                            area.getCentreY() - 9.0f, 60.0f, 18.0f),
              juce::Justification::centred);
  }

  // --- X axis: time in seconds -----------------------------------------
  float xStep = niceTickStep(totalSec, 6);
  g.setFont(EarFontsSingleton::instance().Measures);
  for (float t = 0.0f; t <= totalSec + 0.001f; t += xStep) {
    float x = timeToX(t);
    g.setColour(EarColours::Sphere.withAlpha(0.35f));
    g.drawLine(x, area.getY(), x, area.getBottom(), 1.0f);
    g.setColour(EarColours::Label);
    g.drawText(juce::String(t, t < 1.0f ? 2 : (xStep < 1.0f ? 1 : 0)) + "s",
              juce::Rectangle<float>(x - 20.0f, xAxisArea.getY(), 40.0f, kXAxisHeight),
              juce::Justification::centred);
  }

  // Pre-delay: a flat, empty gap before anything happens.
  if (preDelaySec > 0.0f) {
    juce::Rectangle<float> zone(area.getX(), area.getY(),
                                timeToX(preDelaySec) - area.getX(),
                                area.getHeight());
    g.setColour(EarColours::Area01dp);
    g.fillRect(zone);
  }
  g.setColour(EarColours::Sphere);
  g.drawLine(area.getX(), dbToY(0.0f), area.getRight(), dbToY(0.0f), 1.5f);

  // Direct: the unprocessed dry signal is instantaneous and constant-
  // level, so it's a single flat line across the whole plot at its own
  // level - off (and absent from the graph) by default, same as in the
  // actual signal path.
  if (dsp.directEnabled) {
    float directDb = linearToDb(dsp.directLevel);
    g.setColour(juce::Colours::white.withAlpha(0.8f));
    juce::Path directDashed;
    directDashed.startNewSubPath(area.getX(), dbToY(directDb));
    directDashed.lineTo(area.getRight(), dbToY(directDb));
    float directDash[] = {6.0f, 3.0f};
    juce::PathStrokeType(1.5f).createDashedStroke(directDashed, directDashed,
                                                  directDash, 2);
    g.strokePath(directDashed, juce::PathStrokeType(1.5f));
  }

  // Early reflections: discrete stems at their real level in dB - the
  // same decay pattern reverbdsp::EarlyReflections actually generates
  // (gain = 0.72^i, randomised further per instance), scaled by the Mix
  // section's Early level and omitted entirely when its toggle is off,
  // so this picture actually reflects those controls instead of ignoring
  // them.
  if (dsp.earlyEnabled && dsp.earlyLevel > 0.0f) {
    constexpr int kNumEarlyTicks = 12;
    float earlyLevelDb = linearToDb(dsp.earlyLevel);
    g.setColour(juce::Colours::orange);
    for (int i = 0; i < kNumEarlyTicks; ++i) {
      float frac = (i + 0.3f) / static_cast<float>(kNumEarlyTicks);
      float t = preDelaySec + frac * earlyDurationSec;
      float gain = std::pow(0.72f, static_cast<float>(i));
      float x = timeToX(t);
      g.drawLine(x, dbToY(0.0f), x, dbToY(linearToDb(gain) + earlyLevelDb), 2.0f);
    }
  }

  // Diffuse tail: the RT60 decay is, by definition, a straight line when
  // plotted in dB over time (that's what "decays by 60dB over RT60
  // seconds" means) - so a straight line here is the physically correct
  // picture, not a simplification. Textured with noise (not a smooth
  // ripple) whose amount follows the Density knob, since a real diffuse
  // tail is stochastic, not periodic - low density also widens the band
  // to suggest a sparser, less consistent/more erratic tail. Shifted by
  // the Mix section's Diffuse level (a linear output gain shifts the
  // whole curve by its own dB value) and omitted when its toggle is off.
  if (dsp.diffuseEnabled && dsp.diffuseLevel > 0.0f) {
    float diffuseLevelDb = linearToDb(dsp.diffuseLevel);
    juce::Path tailTop, tailBottom;
    const int kSteps = 200;
    for (int i = 0; i <= kSteps; ++i) {
      float t = tailStart + dsp.decaySeconds * i / float(kSteps);
      float elapsed = t - tailStart;
      float meanDb = diffuseLevelDb -
                     60.0f * elapsed / juce::jmax(0.1f, dsp.decaySeconds);
      float jitterDb = (3.0f + 5.0f * (1.0f - dsp.density)) * noise(elapsed * 37.0f);
      float x = timeToX(t);
      float yTop = dbToY(meanDb + jitterDb * 0.5f);
      float yBot = dbToY(meanDb - jitterDb * 0.5f);
      if (i == 0) {
        tailTop.startNewSubPath(x, yTop);
        tailBottom.startNewSubPath(x, yBot);
      } else {
        tailTop.lineTo(x, yTop);
        tailBottom.lineTo(x, yBot);
      }
    }
    g.setColour(juce::Colours::lightblue.withAlpha(0.85f));
    g.strokePath(tailTop, juce::PathStrokeType(1.2f));
    g.strokePath(tailBottom, juce::PathStrokeType(1.2f));
  }

  // RT60 marker (-60dB point).
  float rt60X = timeToX(tailEndSec);
  g.setColour(EarColours::Label);
  juce::Path dashed;
  dashed.startNewSubPath(rt60X, area.getY());
  dashed.lineTo(rt60X, area.getBottom());
  float dashLengths[] = {4.0f, 3.0f};
  juce::PathStrokeType(1.0f).createDashedStroke(dashed, dashed, dashLengths, 2);
  g.strokePath(dashed, juce::PathStrokeType(1.0f));

  g.setFont(EarFontsSingleton::instance().Description);
  g.setColour(EarColours::Label);
  g.drawText("RT60 = " + juce::String(dsp.decaySeconds, 1) + "s",
            juce::Rectangle<float>(rt60X - 90.0f, area.getY() + 2.0f, 86.0f, 14.0f),
            juce::Justification::right);

  // Small zone labels at the top of the plot, inside their own zone's
  // time span (not a second axis-label row) - only drawn where there is
  // genuinely room for them, so they don't overlap at small widths. Each
  // gets an opaque chip behind it so the decay line/stems passing
  // underneath never collide with the text.
  g.setFont(EarFontsSingleton::instance().Description);
  auto drawZoneLabel = [&](juce::String const& text, float x, float w) {
    if (w <= 0.0f) return;
    auto bounds = juce::Rectangle<float>(x, area.getY() + 2.0f, w, 13.0f);
    g.setColour(EarColours::ObjectItemBackground.withAlpha(0.85f));
    g.fillRoundedRectangle(bounds, 2.0f);
    g.setColour(EarColours::Label);
    g.drawText(text, bounds, juce::Justification::centredLeft);
  };
  if (preDelaySec > 0.0f && timeToX(preDelaySec) - area.getX() > 50.0f)
    drawZoneLabel("Pre-delay", area.getX() + 2.0f,
                  juce::jmin(60.0f, timeToX(preDelaySec) - area.getX() - 4.0f));
  if (timeToX(tailStart) - timeToX(preDelaySec) > 40.0f)
    drawZoneLabel("Early", timeToX(preDelaySec) + 2.0f,
                  juce::jmin(44.0f, timeToX(tailStart) - timeToX(preDelaySec) - 4.0f));
  drawZoneLabel("Diffuse tail", timeToX(tailStart) + 2.0f, 70.0f);
}
