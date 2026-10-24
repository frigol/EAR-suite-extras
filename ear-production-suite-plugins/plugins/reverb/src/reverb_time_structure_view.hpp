#pragma once

#include "JuceHeader.h"
#include "reverb_plugin_processor.hpp"

/** @brief A read-only "Time Structure" graph in the spirit of IRCAM Tools
 * Verb's own panel: a horizontal timeline showing where, in time, the
 * pre-delay gap, the early-reflection cluster and the diffuse tail's
 * decay sit, driven directly by the processor's actual DspParams - so
 * dragging Decay/Pre-delay/Room Size/Density moves this graph too,
 * instead of it being a separate, disconnected illustration. */
class ReverbTimeStructureView : public juce::Component {
 public:
  explicit ReverbTimeStructureView(ReverbAudioProcessor* p);

  void paint(juce::Graphics&) override;

  // Call whenever a DSP slider changes, so the graph redraws from the
  // processor's current values.
  void refresh() { repaint(); }

 private:
  ReverbAudioProcessor* p_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbTimeStructureView)
};
