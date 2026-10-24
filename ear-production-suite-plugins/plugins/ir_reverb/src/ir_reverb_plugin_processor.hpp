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
#include "components/read_only_audio_parameter_int.hpp"

namespace ear {
namespace plugin {
namespace communication {
class MonitoringMetadataReceiver;
}
}  // namespace plugin
}  // namespace ear

/** @brief "IR Reverb for EAR Suite" - skeleton (v1).
 *
 * Same overall structure as the algorithmic "Reverb for EAR Suite": takes
 * up to kMaxInputs mono sends, discovers them via Scene's own PUB
 * broadcast exactly like that plugin does (see communication::
 * MonitoringControlConnection/MonitoringMetadataReceiver, unchanged), and
 * outputs kNumPoints channels in the same "4+6+0" layout, each
 * independently registered with Scene as its own ADM Object.
 *
 * The difference is the engine: instead of an algorithmic FDN/MVerb tail,
 * each output point convolves its own direction-weighted sum of the
 * active sources against a single loaded impulse response (see
 * processBlock()) - "encode (direction-weight) then sum then convolve",
 * so a source's direction is still audible in the result (louder at the
 * points nearest its own direction) while the convolution cost stays at
 * one pass per point, independent of how many sources are active.
 *
 * v1 scope: mono, stereo and full-order AmbiX (ACN/SN3D) impulse
 * responses - see IrMode below. A mono IR is convolved independently per
 * output point, each fed that point's own direction-weighted sum of the
 * active sources; a stereo IR reuses that exact same per-point pipeline,
 * just loading each point's own convolution from whichever of the 2
 * channels matches its side (see loadStereoIr()) instead of from channel
 * 0 of everything. An AmbiX IR instead convolves once per ACN channel
 * against ONE shared
 * ambisonic bus (every active source encoded into it by its own
 * direction's spherical-harmonic coefficients and summed in), then
 * decodes that convolved bus back out to the 10 points using the same
 * coefficients evaluated at each point's own direction - "encode then
 * sum then convolve then decode" - so the convolution cost is
 * (order+1)^2 passes total, independent of how many sources or how many
 * output points there are. See ir_reverb_dsp.hpp and processBlock().
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
 * see ambisonicEncodingSN3D()'s own comment), a saved-IR library combo
 * beyond the current flat list, and reaper-adm-extension/ADM export
 * recognition for this plugin specifically.
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

  const std::array<PointInfo, kNumPoints>& getPointLayout() const {
    return pointInfo_;
  }

  std::function<void()> onAvailableObjectsChanged;

  enum class IrMode { Mono, Stereo, Ambisonic, Array };

 private:
  void onSceneReceived(const ear::plugin::proto::SceneStore& store);
  void onSceneControlConnection(ear::plugin::communication::ConnectionId id,
                                const std::string& streamEndpoint);
  void onSceneControlConnectionLost();
  void buildPointLayout();
  void notifyAvailableObjectsChangedOnMessageThread();
  void notifyTrackNameChangedOnMessageThread();
  void pushAllPointPropertiesToScene();
  // Mono path: (re)loads the same mono IR file into all kNumPoints fixed
  // per-point convolutions_ below - called with dspMutex_ already held.
  void reloadMonoConvolutions();
  // Stereo path: reuses the exact same per-point convolutions_/preDelay_/
  // pointFeedBuffer_ pipeline as Mono (each point still gets its own
  // direction-weighted dry sum, same processBlock() code, no new branch
  // needed there) - the only difference is which of the IR's 2 channels
  // each point's convolution is loaded from: points on the left side
  // (positive azimuth, matching this plugin's own L/LS/LTF/LTR naming)
  // get channel 0, points on the right side get channel 1, and the two
  // centre points (C/CB) are split one to each channel for a little
  // decorrelation. Called with dspMutex_ already held.
  void loadStereoIr(juce::AudioFormatReader& reader);
  // Ambisonic path: (re)builds ambiConvolutions_/ambiPreDelay_/the feed
  // buffers for a numChannels-channel HOA file, reading all of it via
  // `reader` and loading each channel into its own convolution - called
  // with dspMutex_ already held.
  void loadAmbisonicIr(juce::AudioFormatReader& reader, int numChannels);
  // Array path: like loadAmbisonicIr() (one convolution per raw channel,
  // reading the whole file once and splitting it), but channel directions
  // come from a recognized array preset (arrayChannelDirections_) instead
  // of being implied by ACN/SN3D order - called with dspMutex_ held.
  void loadArrayIr(juce::AudioFormatReader& reader, int numChannels);
  // Decides Mono/Stereo/Ambisonic/Array for a just-opened reader and
  // dispatches to the matching load*() - shared by loadIrByName() and
  // prepareToPlay()'s reload-on-reprepare path. Called with dspMutex_
  // held; `reader` must already be open for loadedIrFile_.
  void decideModeAndLoad(juce::AudioFormatReader& reader);
  // Shared by getStateInformation/setStateInformation's own "Dsp" XML
  // node and by the preset save/load methods - a preset file is just
  // this same node written to its own standalone file.
  void writeDspXml(juce::XmlElement& dspXml) const;
  void applyDspXml(const juce::XmlElement& dspXml);

  std::array<PointInfo, kNumPoints> pointInfo_;
  juce::String trackName_;
  juce::Colour trackColour_{juce::Colours::orange};
  std::array<std::unique_ptr<ear::plugin::IrReverbPointConnector>, kNumPoints>
      pointConnectors_;
  std::array<std::unique_ptr<ear::plugin::ObjectBackend>, kNumPoints>
      pointBackends_;
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
