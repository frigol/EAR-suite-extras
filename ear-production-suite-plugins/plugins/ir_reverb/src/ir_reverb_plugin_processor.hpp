#pragma once

#include "JuceHeader.h"

#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "object_backend.hpp"
#include "ir_reverb_dsp.hpp"
#include "ir_reverb_point_connector.hpp"
#include "communication/monitoring_control_connection.hpp"
#include "scene_store.pb.h"
#include <daw_channel_count.h>
#include "reaper_vst3_interfaces.h"
#include "osc_port_registry.hpp"
#include "components/read_only_audio_parameter_int.hpp"

namespace ear {
namespace plugin {
namespace communication {
class MonitoringMetadataReceiver;
}
}  // namespace plugin
}  // namespace ear

/** @brief "LF IR Reverb".
 *
 * Same overall structure as the algorithmic "LF Reverb": takes
 * up to kMaxInputs mono sends, discovers them via Scene's own PUB
 * broadcast exactly like that plugin does (see communication::
 * MonitoringControlConnection/MonitoringMetadataReceiver, unchanged), and
 * outputs kNumPoints channels in the same "4+6+0" layout, each
 * independently registered with Scene as its own ADM Object.
 *
 * Each point has its own "Active" toggle, ON by default (see the
 * algorithmic Reverb's own class comment for the full reasoning) - turning
 * it off unregisters that point from Scene (frees a routed channel/Scene
 * Object slot) without stopping its audio output, so it can be routed
 * into a "LF Multi-Encoder" instance instead when several
 * reverb instances in one session would otherwise need more Scene Objects
 * than are available.
 *
 * The difference from the algorithmic engine is the tail: instead of an
 * algorithmic FDN/MVerb, each output point convolves its own direction-
 * weighted sum of the active sources against a single loaded impulse
 * response (see processBlock()) - "encode (direction-weight) then sum
 * then convolve", so a source's direction is still audible in the result
 * (louder at the points nearest its own direction) while the convolution
 * cost stays at one pass per point, independent of how many sources are
 * active.
 *
 * Supported IR types: mono, stereo and full-order AmbiX (ACN/SN3D), plus
 * a recognized discrete-array layout (PCMA-3D etc) - see IrMode below. A
 * mono IR is convolved independently per output point, each fed that
 * point's own direction-weighted sum of the active sources; a stereo IR
 * reuses that exact same per-point pipeline, just loading each point's
 * own convolution from whichever of the 2 channels matches its side (see
 * loadStereoIr()) instead of from channel 0 of everything. An AmbiX IR
 * instead convolves once per ACN channel against ONE shared ambisonic
 * bus (every active source encoded into it by its own direction's
 * spherical-harmonic coefficients and summed in), then decodes that
 * convolved bus back out to the 10 points using the same coefficients
 * evaluated at each point's own direction - "encode then sum then
 * convolve then decode" - so the convolution cost is (order+1)^2 passes
 * total, independent of how many sources or how many output points there
 * are. See ir_reverb_dsp.hpp and processBlock().
 *
 * A discrete-array IR (PCMA-3D etc) reuses the same encode-sum-convolve-
 * decode shape as AmbiX, but with nearest-point angular blending (see
 * irreverbdsp::directionalBlendWeights()) instead of spherical harmonics,
 * since an array channel represents one physical capture point, not a
 * sound-field component. Channel count alone can't always tell an array
 * apart from AmbiX (9ch is both PCMA-3D and AmbiX order 2) - see
 * setPreferArrayInterpretation().
 *
 * Each of the 10 points defaults to its nominal position but, like a real
 * EAR Object (and like the algorithmic Reverb's own points), every
 * parameter can be overridden afterwards from the editor (see
 * setPointPosition() etc.) - the override is both what gets sent to Scene
 * and what the DSP's own direction-weighted encode/decode above uses.
 *
 * Deferred, not forgotten: a proper AllRAD/max-rE-weighted ambisonic decode
 * (the current one is a simple, correct-but-unoptimised "basic" decoder -
 * see ambisonicEncodingSN3D()'s own comment) and a saved-IR library combo
 * beyond the current flat list.
 */
class IrReverbAudioProcessor : public juce::AudioProcessor,
                               public juce::VST3ClientExtensions {
 public:
  static constexpr int kNumPoints = 10;
  static constexpr int kMaxInputs = MAX_DAW_CHANNELS;

  struct SourceObjectInfo {
    std::string connectionId;
    juce::String name;
    uint32_t colour{0};
    float azimuthDeg{0.0f};
    float elevationDeg{0.0f};
    float distance{1.0f};
    float width{0.0f};
    float height{0.0f};
  };

  /** @brief A DirectSpeakers or HOA item visible on Scene - listed (for
   * display/parity with EAR Scene's own "+ Item" dialog) but not
   * selectable as a reverb input, same reasoning as the algorithmic
   * Reverb's own UnsupportedInputInfo: this plugin's DSP needs one point
   * position per source to weight its direction, which only Objects
   * have. */
  struct UnsupportedInputInfo {
    std::string connectionId;
    juce::String name;
    uint32_t colour{0};
  };

  struct PointInfo {
    juce::String name;
    float nominalAzimuthDeg{0.0f};
    float nominalElevationDeg{0.0f};
  };

  /** @brief A point's current, user-overridable Object-style parameters -
   * same shape as the algorithmic Reverb's own PointParams, initialised
   * from PointInfo's nominal "4+6+0" direction but editable afterwards
   * like any real EAR Object. width/height/depth/diffuse don't feed the
   * DSP here (unlike the algorithmic engine's early-reflection sharpness)
   * - they're still sent to Scene as real Object metadata. */
  struct PointParams {
    float azimuthDeg{0.0f};
    float elevationDeg{0.0f};
    float distance{1.0f};
    float gain{1.0f};
    float width{0.0f};
    float height{0.0f};
    float depth{0.0f};
    float diffuse{0.0f};
  };

  struct ActiveInputSlot {
    int slotIndex;
    std::string connectionId;
    juce::String name;
    uint32_t colour{0};
    // False when this slot's connectionId no longer matches any object
    // Scene is currently broadcasting (see the algorithmic Reverb's own
    // ActiveInputSlot for the reasoning) - the slot's audio/routing may
    // still be working, but its name/colour can no longer be refreshed.
    bool isLive{true};
  };

  struct InputSlotInfo {
    std::string connectionId;
    juce::String name;
    uint32_t colour{0};
    float azimuthDeg{0.0f};
    float elevationDeg{0.0f};
    float distance{1.0f};
    float width{0.0f};
    float height{0.0f};
  };

  IrReverbAudioProcessor();
  ~IrReverbAudioProcessor() override;

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

  // --- Object discovery / input slots (same pattern as the algorithmic
  // Reverb's own) -------------------------------------------------------
  std::vector<SourceObjectInfo> getAvailableObjects() const;
  std::vector<UnsupportedInputInfo> getAvailableDirectSpeakers() const;
  std::vector<UnsupportedInputInfo> getAvailableHoa() const;
  std::vector<ActiveInputSlot> getActiveInputSlots() const;
  int addInputSlot(const std::string& connectionId);
  void setInputSlotSource(int slotIndex, const std::string& connectionId);
  std::string getInputSlotSource(int slotIndex) const;
  InputSlotInfo getInputSlotInfo(int slotIndex) const;

  void setPointRouting(int pointIndex, int sceneChannel);
  int getPointRouting(int pointIndex) const;

  // Whether this point is currently registered with Scene at all - ON by
  // default (see the class comment). The point's audio output keeps
  // running either way.
  void setPointActive(int pointIndex, bool active);
  bool getPointActive(int pointIndex) const;

  // Object-style per-point parameters: default to the point's nominal
  // "4+6+0" direction, overridable afterwards exactly like a real EAR
  // Object - mirrors the algorithmic Reverb's own API 1:1.
  PointParams getPointParams(int pointIndex) const;
  void setPointPosition(int pointIndex, float azimuthDeg, float elevationDeg,
                        float distance);
  void setPointGain(int pointIndex, float linearGain);
  void setPointWidth(int pointIndex, float width);
  void setPointHeight(int pointIndex, float height);
  void setPointDepth(int pointIndex, float depth);
  void setPointDiffuse(int pointIndex, float diffuse);
  void resetPointToNominal(int pointIndex);

  // --- Impulse response management --------------------------------------
  static juce::File getIrDirectory();
  juce::StringArray getAvailableIrNames() const;
  // Copies sourceFile into getIrDirectory() (if not already there) and
  // loads it. Returns the imported file, or an invalid File on failure.
  juce::File importAndLoadIr(const juce::File& sourceFile);
  bool loadIrByName(const juce::String& name);
  juce::String getLoadedIrName() const { return loadedIrName_; }
  // A loaded file's channel count decides its mode automatically: 4, 9,
  // 16, 25... (any (order+1)^2) is treated as full-order AmbiX; anything
  // else (including plain stereo) falls back to mono, using channel 1
  // only - non-fatal, the UI should warn rather than refuse, since
  // discrete-array support is still pending.
  int getLoadedIrNumChannels() const { return loadedIrNumChannels_; }
  bool isAmbisonicMode() const { return irMode_ == IrMode::Ambisonic; }
  bool isStereoMode() const { return irMode_ == IrMode::Stereo; }
  bool isArrayMode() const { return irMode_ == IrMode::Array; }
  int getAmbisonicOrder() const { return ambisonicOrder_; }
  juce::String getArrayPresetName() const { return arrayPresetName_; }

  // 9- and 10-channel files are ambiguous (9 = AmbiX order 2 OR PCMA-3D's
  // "4+5+0" minus its LFE; 10 = AmbiX has no valid order at 10ch, but a
  // file could still be a discrete array in this plugin's own point
  // order) - this defaults to false (AmbiX/mono-fallback, as before) and
  // only changes how the *next* loadIrByName()/prepareToPlay() reload
  // interprets such a file, so existing sessions aren't affected by this
  // being added. Re-applies immediately to whatever IR is already loaded.
  void setPreferArrayInterpretation(bool prefer);
  bool getPreferArrayInterpretation() const { return preferArrayInterpretation_; }

  // --- Mix controls ------------------------------------------------------
  float getWetLevel() const;
  void setWetLevel(float level);
  float getPreDelayMs() const;
  void setPreDelayMs(float ms);

  /** @brief How the loaded IR itself is reshaped before convolution -
   * applied to the raw file once per change (see applyIrShaping()), not
   * per processBlock(), so these are "load-time" controls, not automatable
   * DSP parameters. */
  struct IrShapeParams {
    // Resamples the IR to stretch/compress its own duration before
    // anything else - >1 genuinely lengthens it past the loaded file's
    // own length (reads the source at a slower rate, same basic
    // technique as a sampler's "stretch" - darkens/slows the diffuse
    // tail's own spectral content as a side effect, most audible on a
    // short/characterful IR, least audible on an already-dense,
    // noise-like tail). 1.0 = unmodified length.
    float stretch{1.0f};
    // Exponential-decay trim applied after stretch - 1.0 = unmodified;
    // lower values accelerate the fade-out, shortening the *perceived*
    // tail within the (possibly already stretched) buffer. This can only
    // shorten/soften, never lengthen - use Stretch for that.
    float decay{1.0f};
    // One-pole lowpass applied to the whole (stretched+decayed) IR - 0 =
    // no damping/full brightness, 1 = heavily darkened. Same qualitative
    // role as the algorithmic Reverb's own Damping control.
    float damping{0.0f};
  };
  IrShapeParams getIrShapeParams() const;
  void setIrStretch(float stretch);
  void setIrDecay(float decay);
  void setIrDamping(float damping);

  // DSP parameter presets - same scope/convention as the algorithmic
  // Reverb's own (Wet level/Pre-delay only, not per-point routing/
  // position - a preset is a reusable "sound").
  static juce::File getPresetsDirectory();
  juce::StringArray getAvailablePresetNames() const;
  void savePresetAs(const juce::String& name);
  bool loadPreset(const juce::String& name);
  void deletePreset(const juce::String& name);

  juce::String getPointDisplayName(int pointIndex) const;
  std::function<void()> onTrackNameChanged;
  juce::Colour getTrackColour() const { return trackColour_; }

  // Whether the editor's "Output points" section is currently expanded -
  // see the algorithmic Reverb's own getPointsExpanded() for why this
  // lives here and not in the editor.
  bool getPointsExpanded() const { return pointsExpanded_; }
  void setPointsExpanded(bool expanded) { pointsExpanded_ = expanded; }

  const std::array<PointInfo, kNumPoints>& getPointLayout() const {
    return pointInfo_;
  }

  std::function<void()> onAvailableObjectsChanged;

  enum class IrMode { Mono, Stereo, Ambisonic, Array };

  // --- OSC output (reports each point's own position/active state) ---
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

 private:
  void onSceneReceived(const ear::plugin::proto::SceneStore& store);
  void onSceneControlConnection(ear::plugin::communication::ConnectionId id,
                                const std::string& streamEndpoint);
  void onSceneControlConnectionLost();
  void buildPointLayout();
  void notifyAvailableObjectsChangedOnMessageThread();
  void notifyTrackNameChangedOnMessageThread();
  void pushAllPointPropertiesToScene();
  void buildPointConnector(int pointIndex);
  void teardownPointConnector(int pointIndex);
  // Mono path: (re)loads the same mono IR file into all kNumPoints fixed
  // per-point convolutions_ below, from shapedIr_ - called with dspMutex_
  // already held.
  void reloadMonoConvolutions();
  // Stereo path: reuses the exact same per-point convolutions_/preDelay_/
  // pointFeedBuffer_ pipeline as Mono (each point still gets its own
  // direction-weighted dry sum, same processBlock() code, no new branch
  // needed there) - the only difference is which of the IR's 2 channels
  // each point's convolution is loaded from: points on the left side
  // (positive azimuth, matching this plugin's own L/LS/LTF/LTR naming)
  // get channel 0, points on the right side get channel 1, and the two
  // centre points (C/CB) are split one to each channel for a little
  // decorrelation. Reads from shapedIr_. Called with dspMutex_ already held.
  void loadStereoIr();
  // Ambisonic path: (re)builds ambiConvolutions_/ambiPreDelay_/the feed
  // buffers for a numChannels-channel HOA file, reading each channel out
  // of shapedIr_ into its own convolution - called with dspMutex_ already
  // held.
  void loadAmbisonicIr(int numChannels);
  // Array path: like loadAmbisonicIr() (one convolution per raw channel,
  // from shapedIr_), but channel directions come from a recognized array
  // preset (arrayChannelDirections_) instead of being implied by ACN/SN3D
  // order - called with dspMutex_ held.
  void loadArrayIr(int numChannels);
  // Decides Mono/Stereo/Ambisonic/Array for a just-opened reader, reads it
  // into rawIr_, and dispatches to rebuildConvolutionsFromShapedIr() -
  // shared by loadIrByName() and prepareToPlay()'s reload-on-reprepare
  // path. Called with dspMutex_ held; `reader` must already be open for
  // loadedIrFile_.
  void decideModeAndLoad(juce::AudioFormatReader& reader);
  // Re-applies IR shaping (see IrShapeParams/applyIrShaping()) to rawIr_
  // and rebuilds whichever mode's convolutions are current (irMode_) from
  // the result - called whenever a shaping parameter changes, and once at
  // the end of decideModeAndLoad() for the initial load. Called with
  // dspMutex_ already held.
  void rebuildConvolutionsFromShapedIr();
  // Resamples rawIr_ by irShape_.stretch, then applies the decay envelope
  // and damping filter, writing the result to shapedIr_/
  // shapedIrSampleRate_ - see IrShapeParams's own field comments for what
  // each stage does. Called with dspMutex_ already held.
  void applyIrShaping();
  // Shared by getStateInformation/setStateInformation's own "Dsp" XML
  // node and by the preset save/load methods - a preset file is just
  // this same node written to its own standalone file.
  void writeDspXml(juce::XmlElement& dspXml) const;
  void applyDspXml(const juce::XmlElement& dspXml);

  void sendPointOsc(int pointIndex);
  void reconnectOscOutputLocked();

  bool oscOutputEnabled_{false};
  juce::String oscOutputIp_{"127.0.0.1"};
  int oscOutputPort_{9000};
  juce::OSCSender oscSender_;
  std::mutex oscSenderMutex_;

  class OscInputSubscriber : public ear::plugin::OscPortRegistry::Subscriber {
   public:
    explicit OscInputSubscriber(IrReverbAudioProcessor& owner) : owner_(owner) {}
    void handleOscMessage(const juce::OSCMessage& message) override {
      owner_.handleOscMessage(message);
    }
   private:
    IrReverbAudioProcessor& owner_;
  };
  void handleOscMessage(const juce::OSCMessage& message);

  bool oscInputEnabled_{false};
  int oscInputPort_{9001};
  OscInputSubscriber oscInputSubscriber_{*this};

  std::array<PointInfo, kNumPoints> pointInfo_;
  juce::String trackName_;
  juce::Colour trackColour_{juce::Colours::orange};
  bool pointsExpanded_{false};
  std::array<std::unique_ptr<ear::plugin::IrReverbPointConnector>, kNumPoints>
      pointConnectors_;
  std::array<std::unique_ptr<ear::plugin::ObjectBackend>, kNumPoints>
      pointBackends_;
  std::array<bool, kNumPoints> pointActive_;
  std::array<int, kNumPoints> pointRouting_;
  std::array<PointParams, kNumPoints> pointParams_;
  mutable std::mutex pointParamsMutex_;

  std::array<InputSlotInfo, kMaxInputs> inputSlots_;
  mutable std::mutex inputSlotsMutex_;

  // --- Mono path: one convolution per output point ----------------------
  std::array<juce::dsp::Convolution, kNumPoints> convolutions_;
  std::array<ear::plugin::irreverbdsp::SimpleDelayLine, kNumPoints> preDelay_;
  std::array<std::vector<float>, kNumPoints> pointFeedBuffer_;

  // --- Ambisonic path: one convolution per ACN channel (see class
  // comment and ir_reverb_dsp.hpp) - only populated/used when
  // irMode_ == Ambisonic; sized to (order+1)^2 in loadAmbisonicIr().
  std::vector<std::unique_ptr<juce::dsp::Convolution>> ambiConvolutions_;
  std::vector<ear::plugin::irreverbdsp::SimpleDelayLine> ambiPreDelay_;
  std::vector<std::vector<float>> ambiFeedBuffer_;       // pre-convolution
  std::vector<std::vector<float>> ambiConvolvedBuffer_;  // post-convolution

  // --- Discrete-array path: one convolution per physical capture point
  // (see class comment and ir_reverb_dsp.hpp's directionalBlendWeights())
  // - only populated/used when irMode_ == Array.
  std::vector<std::unique_ptr<juce::dsp::Convolution>> arrayConvolutions_;
  std::vector<ear::plugin::irreverbdsp::SimpleDelayLine> arrayPreDelay_;
  std::vector<std::vector<float>> arrayFeedBuffer_;
  std::vector<std::vector<float>> arrayConvolvedBuffer_;
  std::vector<ear::plugin::irreverbdsp::Direction> arrayChannelDirections_;
  juce::String arrayPresetName_;
  bool preferArrayInterpretation_{false};

  IrMode irMode_{IrMode::Mono};
  int ambisonicOrder_{0};
  juce::File loadedIrFile_;
  juce::String loadedIrName_;
  int loadedIrNumChannels_{0};
  double preparedSampleRate_{48000.0};
  int preparedBlockSize_{512};
  mutable std::mutex dspMutex_;
  float wetLevel_{1.0f};
  float preDelayMs_{0.0f};

  // The IR exactly as read from the loaded file (all channels, full
  // length) - kept so Stretch/Decay/Damping can be re-applied from
  // scratch each time one changes, rather than compounding onto an
  // already-shaped buffer. shapedIr_/shapedIrSampleRate_ are what the
  // mode-specific load*() functions actually build convolutions from -
  // see applyIrShaping().
  juce::AudioBuffer<float> rawIr_;
  double rawIrSampleRate_{48000.0};
  juce::AudioBuffer<float> shapedIr_;
  double shapedIrSampleRate_{48000.0};
  IrShapeParams irShape_;

  ear::plugin::communication::MonitoringControlConnection
      sceneControlConnection_;
  std::unique_ptr<ear::plugin::communication::MonitoringMetadataReceiver>
      sceneMetadataReceiver_;

  mutable std::mutex availableObjectsMutex_;
  std::vector<SourceObjectInfo> availableObjects_;
  std::vector<UnsupportedInputInfo> availableDirectSpeakers_;
  std::vector<UnsupportedInputInfo> availableHoa_;

  void setIHostApplication(Steinberg::FUnknown* unknown) override;
  IReaperHostApplication* reaperHost_{nullptr};
  std::array<ReadOnlyAudioParameterInt*, kNumPoints> inputInstanceIdParams_{};

  // Write-only mirrors of pointParams_ that reaper-adm-extension reads by
  // raw VST3 parameter index to author each exported ADM Object's
  // metadata - same mechanism and index-contiguity requirement as the
  // algorithmic Reverb's own PointExportParams (see its header for the
  // full reasoning). The UI/DSP never reads these back.
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
  std::array<PointExportParams, kNumPoints> pointExportParams_{};
  void pushExportParams(int pointIndex);

  std::shared_ptr<bool> aliveFlag_ = std::make_shared<bool>(true);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IrReverbAudioProcessor)
};
