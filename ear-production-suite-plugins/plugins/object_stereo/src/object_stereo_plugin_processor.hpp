#pragma once

#include "JuceHeader.h"

#include <memory>
#include "components/level_meter_calculator.hpp"
#include "components/read_only_audio_parameter_int.hpp"
#include "communication/common_types.hpp"
#include "osc_port_registry.hpp"
#include "reaper_vst3_interfaces.h"
#include <daw_channel_count.h>
#include "derived_object_connector.hpp"

namespace ear {
namespace plugin {
namespace ui {
class ObjectStereoJuceFrontendConnector;
}
class ObjectBackend;
}  // namespace plugin
}  // namespace ear

/** @brief One plugin instance driving a linked PAIR of EAR Objects (two
 * independent Scene connections) from a single set of controls: a shared
 * centre azimuth/elevation/distance, a Pair Distance (angular separation
 * between the two objects, split evenly around the centre) and a Pair
 * Rotation (the separation axis's roll around the centre - 0 degrees spreads
 * the pair horizontally, i.e. classic L/R stereo; 90 degrees spreads it
 * vertically, one object up and one down; other angles are diagonal).
 * Gain/Width/Height/Depth/Diffuse apply identically to both objects.
 *
 * Internally this is just two ordinary Object connections
 * (ObjectBackend A/B, each fed by its own DerivedObjectConnector) kept in
 * lock-step by recomputeAndPush(), called whenever any control changes.
 *
 * reaper-adm-extension recognises this as a 2-point multi-object input
 * (same family as "LF Reverb"/"LF IR Reverb"/"LF Multi-Encoder" - see
 * exportaction_admsource-earvst.cpp's isReverbPlugin()/
 * numPointsForMultiObjectPlugin()), which requires this processor's ONLY
 * addParameter() calls to be, in order: 2 read-only instance-id parameters
 * (one per object), then 2*8 = 16 position/gain export-mirror parameters
 * (azimuth, elevation, distance, gain, width, height, depth, diffuse for L,
 * then the same 8 for R) - see the constructor. Every other control (centre
 * position, pair distance/rotation, routing, extent, name, colour...) is
 * therefore kept as plain internal state here, not as a host-automatable
 * AudioProcessorParameter - same trade-off "LF Multi-Encoder" already
 * makes. The UI (ObjectStereoJuceFrontendConnector) talks to this state
 * through plain setter methods, not AudioProcessorParameter::Listener.
 */
class ObjectStereoAudioProcessor : public AudioProcessor,
                                   public VST3ClientExtensions,
                                   private ear::plugin::OscPortRegistry::Subscriber {
 public:
  ObjectStereoAudioProcessor();
  ~ObjectStereoAudioProcessor();

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

  // --- Plain state getters/setters (not AudioProcessorParameters - see
  // the class comment) -------------------------------------------------
  int getRoutingA() const { return routingA_; }
  void setRoutingA(int routing);
  int getRoutingB() const { return routingB_; }
  void setRoutingB(int routing);

  float getGainDb() const { return gainDb_; }
  void setGainDb(float gainDb);
  float getAzimuth() const { return azimuthDeg_; }
  void setAzimuth(float deg);
  float getElevation() const { return elevationDeg_; }
  void setElevation(float deg);
  float getDistance() const { return distance_; }
  void setDistance(float dist);
  float getPairDistance() const { return pairDistanceDeg_; }
  void setPairDistance(float deg);
  float getPairRotation() const { return pairRotationDeg_; }
  void setPairRotation(float deg);

  bool getLinkSize() const { return linkSize_; }
  void setLinkSize(bool link);
  float getSize() const { return size_; }
  void setSize(float size);
  float getWidth() const { return width_; }
  void setWidth(float width);
  float getHeight() const { return height_; }
  void setHeight(float height);
  float getDepth() const { return depth_; }
  void setDepth(float depth);
  float getDiffuse() const { return diffuse_; }
  void setDiffuse(float diffuse);

  bool getUseTrackName() const { return useTrackName_; }
  void setUseTrackName(bool useTrackName);
  void setName(const std::string& name);
  std::string getActiveName() const { return activeName_; }
  void setColour(Colour colour);
  Colour getActiveColour() const { return activeColour_; }

  std::weak_ptr<ear::plugin::LevelMeterCalculator> getLevelMeter() {
    return levelMeter_;
  };

  ear::plugin::ui::ObjectStereoJuceFrontendConnector* getFrontendConnector() {
    return connector_.get();
  }

  void setIHostApplication(Steinberg::FUnknown* unknown) override;

  int getNumDawChannels() { return numDawChannels_; }

  /** @brief Recomputes object A/B's azimuth/elevation from the shared centre
   * + Pair Distance/Rotation, pushes the full derived state (position,
   * gain, extent, diffuse, routing, name, colour) into both ObjectBackends
   * for live Scene rendering, and mirrors each object's position/gain/
   * extent/diffuse into this processor's export parameters (see the class
   * comment) for reaper-adm-extension to read at export time. Called
   * whenever any relevant control changes - never from audio-thread code. */
  void recomputeAndPush();

  /** @brief Receives this plugin's saved state back from reaper-adm-
   * extension (via the registerPluginLoadWithExtension() callback set up in
   * setIHostApplication()), as a partial update - see setStateInformation's
   * useDefaultsIfUnspecified. */
  void extensionSetState(std::string const& xmlStateStr);

  struct DerivedPositions {
    float azimuthA, elevationA, azimuthB, elevationB, distance;
  };
  /** @brief Fired on the message thread after recomputeAndPush(), so the
   * editor's panning view can redraw the two satellite markers. */
  std::function<void(const DerivedPositions&)> onDerivedPositionsChanged;

  // --- OSC remote control - identical mechanism to "LF Object OSC" (see
  // its own long header comment for the full address list/history), just
  // driving this plugin's shared centre azimuth/elevation/distance/width/
  // height instead of a single object's. Pair Distance/Rotation are never
  // driven by OSC - they stay local controls for shaping the pair itself.
  // Plain state, not AudioProcessorParameters (see the class comment).
  bool getOscEnabled() const { return oscEnabled_; }
  int getOscPort() const { return oscPort_; }
  int getOscSourceIndex() const { return oscSourceIndex_; }
  enum class OscProtocol { Custom = 0, ControlGris2 = 1 };
  OscProtocol getOscProtocol() const { return oscProtocol_; }
  void setOscEnabled(bool enabled);
  void setOscPort(int port);
  void setOscSourceIndex(int index);
  void setOscProtocol(OscProtocol protocol);

  enum class ControlGrisPadMode { Dome = 0, Cube = 1 };
  ControlGrisPadMode getControlGrisPadMode() const { return controlGrisPadMode_; }
  void setControlGrisPadMode(ControlGrisPadMode mode);

  String getOscDebugText() const { return oscDebugText_; }
  std::function<void()> onOscDebugChanged;

 private:
  void setOscDebugText(const String& text);
  void handleOscMessage(const OSCMessage& message) override;
  void applyOscConnection();
  void handleObjectAddressMessage(const String& address, const OSCMessage& message);
  void handleControlGris2Message(const String& address, const OSCMessage& message);

  bool oscEnabled_{false};
  int oscPort_{9000};
  int oscSourceIndex_{1};
  OscProtocol oscProtocol_{OscProtocol::ControlGris2};
  ControlGrisPadMode controlGrisPadMode_{ControlGrisPadMode::Dome};
  int oscSubscribedPort_{-1};
  String oscDebugText_{"(no OSC received yet)"};
  std::shared_ptr<bool> aliveFlag_ = std::make_shared<bool>(true);

  IReaperHostApplication* reaperHost{ nullptr };
  ear::plugin::communication::ConnectionId connectionIdA_;
  ear::plugin::communication::ConnectionId connectionIdB_;

  // --- Plain internal state (persisted via get/setStateInformation's own
  // XML, not via AudioProcessorParameter - see the class comment) --------
  int routingA_{-1};
  int routingB_{-1};
  float gainDb_{0.f};
  float azimuthDeg_{0.f};
  float elevationDeg_{0.f};
  float distance_{1.f};
  float pairDistanceDeg_{30.f};
  float pairRotationDeg_{0.f};
  bool linkSize_{false};
  float size_{0.f};
  float width_{0.f};
  float height_{0.f};
  float depth_{0.f};
  float diffuse_{0.f};
  bool useTrackName_{true};
  std::string activeName_{"ObjectPair_1"};
  std::string lastKnownTrackName_;
  std::string lastKnownCustomName_;
  Colour activeColour_{Colours::transparentBlack};

  std::unique_ptr<ear::plugin::ui::ObjectStereoJuceFrontendConnector> connector_;
  ear::plugin::ui::DerivedObjectConnector derivedConnectorA_;
  ear::plugin::ui::DerivedObjectConnector derivedConnectorB_;
  std::unique_ptr<ear::plugin::ObjectBackend> backendA_;
  std::unique_ptr<ear::plugin::ObjectBackend> backendB_;

  // --- reaper-adm-extension export contract: the ONLY addParameter()
  // calls in this processor (see the class comment) ----------------------
  ReadOnlyAudioParameterInt* inputInstanceIdA_;
  ReadOnlyAudioParameterInt* inputInstanceIdB_;
  struct ExportFields {
    AudioParameterFloat* azimuth{nullptr};
    AudioParameterFloat* elevation{nullptr};
    AudioParameterFloat* distance{nullptr};
    AudioParameterFloat* gain{nullptr};
    AudioParameterFloat* width{nullptr};
    AudioParameterFloat* height{nullptr};
    AudioParameterFloat* depth{nullptr};
    AudioParameterFloat* diffuse{nullptr};
  };
  ExportFields exportA_;
  ExportFields exportB_;

  int samplerate_;
  int numPluginChannels_;
  int numDawChannels_{MAX_DAW_CHANNELS};
  std::shared_ptr<ear::plugin::LevelMeterCalculator> levelMeter_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ObjectStereoAudioProcessor)
};
