#pragma once

#include "JuceHeader.h"

#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "object_backend.hpp"
#include "point_encoder_point_connector.hpp"
#include "reaper_vst3_interfaces.h"
#include "components/read_only_audio_parameter_int.hpp"
#include "osc_port_registry.hpp"

/** @brief "LF Multi-Encoder" - a generic N-channel-in / N-Object-
 * out encoder: each of up to kMaxPoints input channels can be independently
 * activated as its own ADM Object sent to EAR Scene, with a full Object-
 * style parameter set (position, gain, width/height/depth, diffuse) and a
 * free choice of which of this plugin's own input channels feeds it.
 *
 * Why this plugin exists: "LF Reverb" and "IR Reverb for EAR
 * Suite" used to each register their own 10 points with Scene directly (one
 * ear::plugin::ObjectBackend per point per reverb instance) - with several
 * reverb instances active in a session (not unusual: one per
 * room/zone/source group), that's N*10 separate Scene-registered Objects,
 * each consuming one of REAPER's own routing channels, and REAPER has a
 * hard 128-channel cap on inter-track routing. Both reverb plugins are now
 * pure DSP engines with 10 plain audio outputs (no Scene registration of
 * their own) - route their outputs into a shared instance of this plugin
 * instead, and N reverb instances' corresponding points (e.g. every
 * instance's own "L" point) can be summed together in REAPER onto the same
 * one of this plugin's input channels and sent to Scene as a single Object,
 * using only as many Scene Objects/channels as there are actual points in
 * play, independent of how many reverb instances feed them.
 *
 * This works because rendering is linear: two sources panned to the same
 * fixed direction (e.g. two reverbs' own "L" point, both nominally at the
 * same azimuth/elevation) and summed before one Object is mathematically
 * identical to encoding each to its own Object at that same direction and
 * letting the renderer sum them downstream - summing the audio upstream of
 * one shared encoder loses nothing.
 *
 * A point's audio is a plain pass-through of whichever of this plugin's own
 * kMaxPoints input channels its own "mapping" selects (see
 * setPointInputChannel()) - independent of the point's own slot index, so
 * e.g. point 3 can read input channel 7 if that's where the audio actually
 * lands after REAPER's own routing/summing. Only activated points (see
 * setPointActive()) register an ObjectBackend with Scene at all - an
 * inactive point costs nothing beyond a reserved-but-dormant VST3
 * parameter slot (see the instance-id/export-parameter block below).
 *
 * Point positions/names can be set one at a time from the editor, or in
 * bulk by loading a plain-text loudspeaker-layout file (same format/parser
 * as "LF Custom Monitoring" - see loadLayoutFile()): each line
 * activates and names one point at that azimuth/elevation/radius.
 */
class PointEncoderAudioProcessor : public juce::AudioProcessor,
                                   public juce::VST3ClientExtensions {
 public:
  // Fixed VST3 parameter counts (instance-id + export mirrors, see below)
  // force a compile-time cap, even though only some points are normally
  // active at once - see the class comment. 24 is ample headroom over the
  // 10-point layout either reverb plugin uses, while staying well clear of
  // REAPER's 128-channel routing cap even with several Point Encoder
  // instances in a large session.
  static constexpr int kMaxPoints = 24;

  /** @brief One point's full, independent state: whether it's active (and
   * therefore registered with Scene at all), which of this plugin's own
   * input channels feeds its audio, which EAR/Scene channel it reports
   * itself on, and its full Object-style parameter set.
   *
   * inputChannel and earChannel are deliberately separate, same as the
   * algorithmic Reverb's points used to distinguish before the split: this
   * plugin always writes a point's own audio to ITS OWN output-bus channel
   * (point p always comes out on this plugin's output channel p, fixed -
   * see processBlock()), but the channel Scene should read is whatever
   * REAPER's own track/routing setup places that audio on once it reaches
   * the track feeding Scene - which depends on the user's own routing, not
   * on this plugin's internal channel numbering, so it needs its own
   * explicit, user-set value (same "Select Scene channel" semantics every
   * other EAR Suite input plugin already has). */
  struct PointState {
    bool active{false};
    juce::String name;
    int inputChannel{-1};  // -1 = none (silence); else 0..kMaxPoints-1
    int earChannel{-1};    // -1 = unset; the channel reported to Scene
    float azimuthDeg{0.0f};
    float elevationDeg{0.0f};
    float distance{1.0f};
    float gain{1.0f};
    float width{0.0f};
    float height{0.0f};
    float depth{0.0f};
    float diffuse{0.0f};
  };

  PointEncoderAudioProcessor();
  ~PointEncoderAudioProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;
  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
  void processBlock(AudioBuffer<float>&, MidiBuffer&) override;

  void updateTrackProperties(const TrackProperties& properties) override;

  AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override { return true; }
  const String getName() const override;
  bool acceptsMidi() const override { return false; }
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  double getTailLengthSeconds() const override { return 0.0; }
  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const String getProgramName(int) override { return {}; }
  void changeProgramName(int, const String&) override {}

  void getStateInformation(MemoryBlock& destData) override;
  void setStateInformation(const void* data, int sizeInBytes) override;

  // --- Per-point API -------------------------------------------------
  PointState getPointState(int pointIndex) const;
  void setPointActive(int pointIndex, bool active);
  void setPointName(int pointIndex, const juce::String& name);
  void setPointInputChannel(int pointIndex, int inputChannel);
  void setPointEarChannel(int pointIndex, int earChannel);
  void setPointPosition(int pointIndex, float azimuthDeg, float elevationDeg,
                        float distance);
  void setPointGain(int pointIndex, float linearGain);
  void setPointWidth(int pointIndex, float width);
  void setPointHeight(int pointIndex, float height);
  void setPointDepth(int pointIndex, float depth);
  void setPointDiffuse(int pointIndex, float diffuse);
  void resetPointToDefault(int pointIndex);

  // Loads a plain-text loudspeaker-layout file (same format as "LF
  // Custom Monitoring" - name azimuth elevation [radius] [gain]
  // [lfe], see ear::plugin::loadCustomLayoutFromFile()'s own doc comment)
  // and activates/names/positions one point per line, in file order,
  // starting at point 0, up to min(file channel count, kMaxPoints). lfe is
  // ignored (an LFE channel has no single direction an Object encoder can
  // use) and radius becomes the point's own distance. Points beyond the
  // file's channel count are left untouched. Throws CustomLayoutFileError
  // (ear-plugin-base/include/helper/custom_layout_file.hpp) on a parse
  // error - the editor catches this and shows it rather than crashing.
  void loadLayoutFile(const juce::File& file);

  // Standard BS.2051-derived layout presets - same list/display names as
  // "LF Decoder" (see that plugin's own
  // getStandardLayoutChoices() for the full reasoning/source), offered
  // here too so common speaker arrangements (5.1, 7.1.4, 22.2...) don't
  // need an external layout file just to activate/position/name this
  // many points at once. Index into this list is what loadStandardLayout()
  // takes. Same activation rules as loadLayoutFile(): one point per
  // channel, in layout order, starting at point 0, up to kMaxPoints; LFE
  // channels are skipped (no single direction to encode).
  struct LayoutChoice {
    juce::String displayName;
    std::string earLayoutName;
  };
  static const std::vector<LayoutChoice>& getStandardLayoutChoices();
  void loadStandardLayout(int choiceIndex);

  juce::Colour getTrackColour() const { return trackColour_; }

  // Whether the editor's points list is currently expanded - see the
  // algorithmic Reverb's own getPointsExpanded() for why this lives here
  // and not in the editor.
  bool getPointsExpanded() const { return pointsExpanded_; }
  void setPointsExpanded(bool expanded) { pointsExpanded_ = expanded; }

  // --- OSC output (reports each active point's position/state) -------
  bool getOscOutputEnabled() const { return oscOutputEnabled_; }
  void setOscOutputEnabled(bool enabled);
  juce::String getOscOutputIp() const { return oscOutputIp_; }
  void setOscOutputIp(const juce::String& ip);
  int getOscOutputPort() const { return oscOutputPort_; }
  void setOscOutputPort(int port);

  // --- OSC input (controls points remotely) ---------------------------
  bool getOscInputEnabled() const { return oscInputEnabled_; }
  void setOscInputEnabled(bool enabled);
  int getOscInputPort() const { return oscInputPort_; }
  void setOscInputPort(int port);

  std::function<void()> onPointsChanged;

 private:
  void buildPointConnector(int pointIndex);
  void teardownPointConnector(int pointIndex);
  // Builds or tears down pointIndex's connector so it matches whether the
  // point should currently exist to Scene at all (active AND a real EAR
  // channel assigned) - see its own comment in the .cpp.
  void updateConnectorLifecycle(int pointIndex);
  void pushPointPropertiesToScene(int pointIndex);
  void notifyPointsChangedOnMessageThread();

  // OSC output: sends /point/<index>/active, /az, /el, /dist whenever a
  // point's own state/position changes - sending needs no fixed local
  // port (OSCSender::connect(ip,port) just targets a remote endpoint), so
  // unlike input there is no port-sharing concern here.
  void sendPointOsc(int pointIndex);
  void reconnectOscOutputLocked();

  bool oscOutputEnabled_{false};
  juce::String oscOutputIp_{"127.0.0.1"};
  int oscOutputPort_{9000};
  juce::OSCSender oscSender_;
  std::mutex oscSenderMutex_;

  // OSC input: receives the same /point/<index>/{az,el,dist,active}
  // addresses to drive points remotely. Several instances of this same
  // plugin might want to listen on the same port, hence going through
  // the shared OscPortRegistry rather than owning an OSCReceiver
  // directly - see osc_port_registry.hpp's own comment.
  class OscInputSubscriber : public ear::plugin::OscPortRegistry::Subscriber {
   public:
    explicit OscInputSubscriber(PointEncoderAudioProcessor& owner) : owner_(owner) {}
    void handleOscMessage(const juce::OSCMessage& message) override {
      owner_.handleOscMessage(message);
    }
   private:
    PointEncoderAudioProcessor& owner_;
  };
  void handleOscMessage(const juce::OSCMessage& message);

  bool oscInputEnabled_{false};
  int oscInputPort_{9001};
  OscInputSubscriber oscInputSubscriber_{*this};

  std::array<PointState, kMaxPoints> pointState_;
  mutable std::mutex pointStateMutex_;

  std::array<std::unique_ptr<ear::plugin::PointEncoderPointConnector>, kMaxPoints>
      pointConnectors_;
  std::array<std::unique_ptr<ear::plugin::ObjectBackend>, kMaxPoints>
      pointBackends_;

  juce::Colour trackColour_{juce::Colours::orange};
  bool pointsExpanded_{false};

  // Same mechanism as the algorithmic Reverb's own instance-id + export
  // parameters used to have (see reaper-adm-extension-patch/ for the
  // extension-side recognition this plugin now carries alone): one
  // read-only instance-id parameter per point, followed immediately by 8
  // position/gain export-mirror parameters per point, all declared in
  // point order 0..kMaxPoints-1 - reaper-adm-extension reads these by raw
  // VST3 parameter INDEX, not by name, so these must stay the ONLY
  // addParameter() calls in this processor and their relative order must
  // never change.
  void setIHostApplication(Steinberg::FUnknown* unknown) override;
  IReaperHostApplication* reaperHost_{nullptr};
  std::array<ReadOnlyAudioParameterInt*, kMaxPoints> inputInstanceIdParams_{};

  struct PointExportParams {
    juce::AudioParameterFloat* azimuth{nullptr};
    juce::AudioParameterFloat* elevation{nullptr};
    juce::AudioParameterFloat* distance{nullptr};
    juce::AudioParameterFloat* gain{nullptr};
    juce::AudioParameterFloat* width{nullptr};
    juce::AudioParameterFloat* height{nullptr};
    juce::AudioParameterFloat* depth{nullptr};
    juce::AudioParameterFloat* diffuse{nullptr};
  };
  std::array<PointExportParams, kMaxPoints> pointExportParams_{};
  void pushExportParams(int pointIndex);

  std::shared_ptr<bool> aliveFlag_ = std::make_shared<bool>(true);

  // Reused scratch copy of the input, sized once in prepareToPlay() -
  // processBlock() needs every input channel's original content available
  // while it writes output channels in place (a point's input mapping can
  // read any channel, including one this same block is about to
  // overwrite), without allocating on the audio thread.
  juce::AudioBuffer<float> inputCopy_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PointEncoderAudioProcessor)
};
