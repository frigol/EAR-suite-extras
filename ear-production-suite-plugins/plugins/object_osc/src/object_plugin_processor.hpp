#pragma once

#include "JuceHeader.h"

#include <memory>
#include "components/level_meter_calculator.hpp"
#include "communication/common_types.hpp"
#include "osc_port_registry.hpp"
#include "reaper_vst3_interfaces.h"
#include "components/read_only_audio_parameter_int.hpp"
#include <daw_channel_count.h>

namespace ear {
namespace plugin {
namespace ui {
class ObjectsJuceFrontendConnector;
}
class ObjectBackend;
}  // namespace plugin
}  // namespace ear

class ObjectsAudioProcessor
    : public AudioProcessor,
      public VST3ClientExtensions,
      private ear::plugin::OscPortRegistry::Subscriber {
 public:
  ObjectsAudioProcessor();
  ~ObjectsAudioProcessor();

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
  void setStateInformation(XmlElement* xmlState, bool useDefaultsIfUnspecified = true);

  void updateTrackProperties(const TrackProperties& properties) override;

  AudioParameterInt* getRouting() { return routing_; }
  AudioParameterFloat* getGain() { return gain_; }
  AudioParameterFloat* getAzimuth() { return azimuth_; }
  AudioParameterFloat* getElevation() { return elevation_; }
  AudioParameterFloat* getDistance() { return distance_; }
  AudioParameterBool* getLinkSize() { return linkSize_; }
  AudioParameterFloat* getSize() { return size_; }
  AudioParameterFloat* getWidth() { return width_; }
  AudioParameterFloat* getHeight() { return height_; }
  AudioParameterFloat* getDepth() { return depth_; }
  AudioParameterFloat* getDiffuse() { return diffuse_; }
  AudioParameterBool* getDivergence() { return divergence_; }
  AudioParameterFloat* getFactor() { return factor_; }
  AudioParameterFloat* getRange() { return range_; }
  AudioParameterBool* getUseTrackName() { return useTrackName_; }
  AudioParameterInt* getInputInstanceId() { return inputInstanceId_; }

  AudioProcessorParameter* getBypassParameter() {
    return bypass_;
  }

  std::weak_ptr<ear::plugin::LevelMeterCalculator> getLevelMeter() {
    return levelMeter_;
  };

  ear::plugin::ui::ObjectsJuceFrontendConnector* getFrontendConnector() {
    return connector_.get();
  }

  void setIHostApplication(Steinberg::FUnknown *unknown) override;
  void extensionSetState(std::string const& xmlState);

  int getNumDawChannels() { return numDawChannels_; }

  // --- OSC remote control (e.g. a show-control / spatialisation system
  // such as ControlGRIS driving this object's position live) ---
  //
  // Exactly one of these address schemes is active at a time, chosen via
  // oscProtocol_ (not both at once, to keep behaviour predictable even if
  // the schemes were ever to overlap):
  //
  // Protocol::Custom - this plugin's own, one message per parameter,
  // single float32 argument, in the same units/range as the matching
  // automation parameter (not normalised 0-1):
  //   /object/azimuth    -180..180
  //   /object/elevation  -90..90
  //   /object/distance   0..1
  //   /object/gain       -100..6 (dB)
  //   /object/size       0..1
  //   /object/width      0..1
  //   /object/height     0..1
  //   /object/depth      0..1
  //   /object/diffuse    0..1
  //
  // Protocol::ControlGris2 - ControlGRIS2's own address scheme, reverse
  // engineered from a live capture (NOT the "/spat/serv" scheme documented
  // in SpatGRIS's own README, which ControlGRIS2's "Controllers" tab does
  // not actually send despite that README section's framing):
  //   /controlgris/<pluginId>/traj/<trajId>/xyz <x> <y> <z>
  // (ControlGRIS2 also sends /x, /y, /z, /xy and /xyz/1, /xyz/2, /xyz/3
  // redundantly carrying the same update; only the combined /xyz triplet
  // is used here, both to avoid applying the same move multiple times and
  // because it alone gives a consistent x/y/z triplet.) <pluginId> is
  // ControlGRIS's own per-instance "OSC plugin ID" - matched against
  // oscSourceIndex_ so several Object instances can share one port, each
  // driven by a different ControlGRIS2 instance; <trajId> (which source
  // within that instance) is accepted as-is. x,y,z are NOT a cartesian
  // vector - they are normalised DOME pad coordinates (see the comment on
  // applyCartesian() in the .cpp), calibrated live against 7 paired
  // ControlGRIS2/EAR Object screenshots. Distance is fixed at 1.0:
  // ControlGRIS2's DOME mode has no distance control, confirmed by an OSC
  // capture that only ever showed x/y/z/xy/xyz addresses.
  //
  // A second pair of messages covers ControlGRIS2's "Azimuth Span" /
  // "Elevation Span" controls, mapped onto EAR Object's Width/Height with
  // Size kept unlinked so the two stay independent rather than collapsing
  // to one knob:
  //   /controlgris/<pluginId>/azispan <v>  0..1
  //   /controlgris/<pluginId>/elespan <v>  0..1
  // Unlike position, these have no /traj/<trajId>/ segment - they are
  // per-plugin-instance, not per-source - and use abbreviated names, not
  // "azimuthspan"/"elevationspan" as first guessed. Confirmed (address and
  // 0..1 range) via a live OSC capture on a redirected port.
  AudioParameterBool* getOscEnabled() { return oscEnabled_; }
  int getOscPort() const { return oscPort_; }
  int getOscSourceIndex() const { return oscSourceIndex_; }
  enum class OscProtocol { Custom = 0, ControlGris2 = 1 };
  OscProtocol getOscProtocol() const { return oscProtocol_; }
  void setOscEnabled(bool enabled);
  void setOscPort(int port);
  void setOscSourceIndex(int index);
  void setOscProtocol(OscProtocol protocol);

  // Live diagnostic for the editor: every OSC packet this plugin receives
  // updates this (even one that gets filtered out by protocol/address/
  // source-index mismatch) and fires onOscDebugChanged, so "is anything
  // arriving at all, and does it look like what I expect" is visible
  // directly in the plugin UI rather than needing an external OSC sniffer.
  String getOscDebugText() const { return oscDebugText_; }
  std::function<void()> onOscDebugChanged;

 private:
  void setOscDebugText(const String& text);
  // ear::plugin::OscPortRegistry::Subscriber - called for every message
  // received on oscSubscribedPort_, by every "EAR Object OSC" instance
  // subscribed to that port, not just this one. Bundle unwrapping already
  // happened in the registry (see osc_port_registry.hpp) before this is
  // called, so this always sees a plain message.
  void handleOscMessage(const OSCMessage& message) override;
  void applyOscConnection();
  void handleObjectAddressMessage(const String& address, const OSCMessage& message);
  void handleControlGris2Message(const String& address, const OSCMessage& message);

  AudioParameterBool* oscEnabled_;
  int oscPort_{9000};
  int oscSourceIndex_{1};
  OscProtocol oscProtocol_{OscProtocol::ControlGris2};
  // The port this instance is actually subscribed to in OscPortRegistry
  // right now (-1 if none), tracked separately from oscPort_ so a port
  // change while enabled unsubscribes the OLD port rather than leaking a
  // stale subscription.
  int oscSubscribedPort_{-1};
  String oscDebugText_{"(no OSC received yet)"};
  // setOscDebugText() defers to the message thread via a captured
  // weak_ptr to this flag (handleOscMessage can be called off the
  // message thread by some senders despite MessageLoopCallback), so a
  // processor destroyed before that callback fires is never touched by it.
  std::shared_ptr<bool> aliveFlag_ = std::make_shared<bool>(true);

  IReaperHostApplication* reaperHost{ nullptr };
  ear::plugin::communication::ConnectionId connectionId_;

  AudioParameterInt* routing_;
  AudioParameterFloat* gain_;
  AudioParameterFloat* azimuth_;
  AudioParameterFloat* elevation_;
  AudioParameterFloat* distance_;
  AudioParameterBool* linkSize_;
  AudioParameterFloat* size_;
  AudioParameterFloat* width_;
  AudioParameterFloat* height_;
  AudioParameterFloat* depth_;
  AudioParameterFloat* diffuse_;
  AudioParameterBool* divergence_;
  AudioParameterFloat* factor_;
  AudioParameterFloat* range_;
  AudioParameterBool* bypass_;
  AudioParameterBool* useTrackName_;
  ReadOnlyAudioParameterInt* inputInstanceId_;

  std::unique_ptr<ear::plugin::ui::ObjectsJuceFrontendConnector> connector_;
  std::unique_ptr<ear::plugin::ObjectBackend> backend_;

  int samplerate_;
  int numPluginChannels_;
  int numDawChannels_{MAX_DAW_CHANNELS};
  std::shared_ptr<ear::plugin::LevelMeterCalculator> levelMeter_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ObjectsAudioProcessor)
};
