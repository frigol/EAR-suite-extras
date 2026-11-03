#pragma once

#include "JuceHeader.h"

#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "object_backend.hpp"
#include "reverb_dsp.hpp"
#include "reverb_point_connector.hpp"
#include "reverb_tail_engine.hpp"
#include "communication/monitoring_control_connection.hpp"
#include "scene_store.pb.h"
#include <daw_channel_count.h>
#include "reaper_vst3_interfaces.h"
#include "components/read_only_audio_parameter_int.hpp"
#include "osc_port_registry.hpp"

namespace ear {
namespace plugin {
namespace communication {
class MonitoringMetadataReceiver;
}
}  // namespace plugin
}  // namespace ear

/** @brief "LF Reverb".
 *
 * Takes up to kMaxInputs mono sends (one per source EAR Object/Object OSC
 * being reverberated), listens to Scene's own broadcast (the same PUB
 * stream every EAR Monitoring instance already subscribes to - see
 * communication::MonitoringControlConnection/MonitoringMetadataReceiver,
 * reused here unchanged) to discover which Objects currently exist in the
 * project and read their live position, and outputs kNumPoints audio
 * channels - one per "listening point" of this reverb's own "4+6+0"
 * layout (4 ITU-R BS.2051 top-layer corners, unchanged, plus a regular
 * hexagon of 6 points - vertex at front, every 60 degrees - in the mid
 * layer instead of BS.2051's own asymmetric 5-point mid ring; see
 * buildPointLayout()) - each independently registered with Scene as its
 * own ADM Object via its own ear::plugin::ObjectBackend connection, with
 * its own "Select Scene channel" routing.
 *
 * Each point has its own "Active" toggle, ON by default - exactly like
 * every point being a real Scene Object has always worked. Turning a
 * point's Active off unregisters it from Scene entirely (frees up one of
 * REAPER's 128 routable channels / one Scene Object slot) without
 * stopping its audio: the point's output channel keeps carrying the same
 * direction-weighted signal regardless, so it can instead be routed into
 * a "LF Multi-Encoder" instance - useful when several Reverb/
 * IR Reverb instances in one session would otherwise need more Scene
 * Objects/routed channels than are available; see that plugin's own class
 * comment. Default stays ON so a fresh instance behaves exactly as before
 * Point Encoder existed.
 *
 * DSP (see reverb_dsp.hpp): each point gets its own small early-reflection
 * generator (reverbdsp::EarlyReflections), fed not by the raw dry signal
 * but by a per-point mix of the active sources weighted by how closely
 * that source's live azimuth/elevation points toward that point - so
 * reflections concentrate around each source's own direction. Source
 * width/height flattens that weighting (a larger object spreads its
 * reflections wider), and distance scales the per-source levels (closer
 * = stronger early reflections, farther = more sent to the diffuse tail).
 * All sources also feed one shared reverbdsp::FeedbackDelayNetwork for the
 * late diffuse tail - physically one room, with each point reading a
 * different, nearly-direction-independent decorrelated tap of it, per the
 * "early concentrated at the source, tail spread evenly" brief.
 *
 * Each of the 10 points defaults to its nominal position but, like a
 * real EAR Object, every parameter (azimuth/elevation/distance, gain,
 * width/height/depth, diffuse) can be overridden afterwards from the
 * editor (see setPointPosition() etc.) - the override is both what gets
 * sent to Scene AND what the DSP's own direction weighting above uses, so
 * moving a point's Object in Scene also moves where its early reflections
 * concentrate.
 *
 * Input selection mirrors EAR Scene's own workflow: the editor shows a
 * checklist of every Object currently visible on Scene plus an "Add"
 * button (see getAvailableObjects()/addInputSlot()), rather than always
 * showing kMaxInputs empty dropdowns. kMaxInputs equals MAX_DAW_CHANNELS
 * (the same cap EAR Object's own routing already offers) - it is still a
 * hard cap on the underlying discrete-channel input bus (VST3 bus channel
 * counts are fixed at construction, unlike Scene's own unbounded item
 * list), but the UI never forces the user to look at unused slots.
 */
class ReverbAudioProcessor : public juce::AudioProcessor,
                             public juce::VST3ClientExtensions {
 public:
  // Not ITU-R BS.2051's own "4+5+0" (5 mid + 4 top = 9): the mid layer is
  // a regular hexagon instead (6 points, vertex at front) - this reverb's
  // own "4+6+0", 10 points total. See buildPointLayout() for why.
  static constexpr int kNumPoints = 10;
  static constexpr int kMaxInputs = MAX_DAW_CHANNELS;  // matches EAR Object's own cap

  /** @brief A snapshot of one Object currently visible to Scene (read from
   * its broadcast), as offered to the UI for "which object feeds this
   * input slot" / "which object am I reverberating" selection, and cached
   * per input slot to drive the direction-weighted DSP every block. */
  struct SourceObjectInfo {
    std::string connectionId;
    juce::String name;
    uint32_t colour{0};
    float azimuthDeg{0.0f};
    float elevationDeg{0.0f};
    float distance{1.0f};
    float width{0.0f};
    float height{0.0f};
    float depth{0.0f};
  };

  /** @brief A DirectSpeakers or HOA item visible on Scene - listed (for
   * display/parity with EAR Scene's own "+ Item" dialog) but not
   * selectable as a reverb input: this reverb's DSP needs one point
   * position per source to weight its direction (see SourceObjectInfo),
   * which only Objects have. DirectSpeakers is a set of discrete speaker
   * feeds with no single position, and HOA is a set of ambisonic
   * component channels with no single position either - reverberating
   * either would need a genuinely different input model (e.g. treating
   * each speaker/channel as its own point source), not just relaxing
   * this filter. */
  struct UnsupportedInputInfo {
    std::string connectionId;
    juce::String name;
    uint32_t colour{0};
  };

  /** @brief One of the 10 fixed "4+6+0" output positions. */
  struct PointInfo {
    juce::String name;  // conventional letter label, e.g. "L", "LTR"
    float nominalAzimuthDeg{0.0f};
    float nominalElevationDeg{0.0f};
  };

  /** @brief Global reverb DSP controls (reverb_dsp.hpp), shared by all 9
   * points' early reflections and by the one diffuse tail. decaySeconds/
   * damping/earlyLevel/diffuseLevel are the technical controls; roomSize/
   * density/preDelayMs are the qualitative ones layered on top of them. */
  struct DspParams {
    float decaySeconds{1.8f};   // diffuse tail RT60
    float damping{0.3f};        // diffuse tail HF absorption, 0..1
    float earlyLevel{1.0f};     // early-reflection output level
    float diffuseLevel{0.6f};   // diffuse-tail output level
    float roomSize{1.0f};       // 0.5..2: scales the diffuse tail's echo spacing
    float density{1.0f};        // 0..1: smooth/dense (1) vs sparse/metallic (0)
    float preDelayMs{0.0f};     // gap before reflections/tail start, 0..200ms
    // Input brightness into the tail, 0 = dark/muffled, 1 = full
    // bandwidth - real on both tail engines (see reverb_tail_engine.hpp).
    float bandwidth{1.0f};

    // Each of the 3 signal paths that can reach the 9 outputs - direct
    // (dry, unprocessed), early reflections and diffuse tail - has its
    // own enable switch and level. Direct defaults OFF: the dry object is
    // already sent to Scene on its own channel, so direct here is only
    // for auditioning/special routing, not the normal signal path.
    bool directEnabled{false};
    float directLevel{1.0f};
    bool earlyEnabled{true};
    bool diffuseEnabled{true};

    // Which diffuse-tail algorithm is currently driving the output - see
    // reverb_tail_engine.hpp. Both engines stay prepared at all times, so
    // switching is instant.
    ear::plugin::reverbdsp::TailEngineType tailEngine{
        ear::plugin::reverbdsp::TailEngineType::CustomFdn};
  };

  /** @brief A point's current, user-overridable Object-style parameters -
   * initialised from PointInfo's nominal ITU direction but editable like
   * any real EAR Object thereafter. */
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

  ReverbAudioProcessor();
  ~ReverbAudioProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;
  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
  void processBlock(AudioBuffer<float>&, MidiBuffer&) override;

  // Host-reported track name (standard JUCE AudioProcessor callback,
  // supported by REAPER and other DAW-agnostic hosts) - used to compose
  // each point's display/Scene name as "<track name> <letter>" (see
  // getPointDisplayName()), since the points can't be renamed manually
  // and a bare letter alone ("L", "R"...) is ambiguous with more than one
  // Reverb instance in the project.
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

  /** @brief One currently-occupied input slot, as offered to the UI for
   * rendering the "active inputs" list (only occupied slots are listed -
   * there is no fixed-size row of placeholders to render). */
  struct ActiveInputSlot {
    int slotIndex;
    std::string connectionId;
    juce::String name;
    uint32_t colour{0};
    // False when this slot's connectionId no longer matches any object
    // Scene is currently broadcasting (e.g. the feeding Object plugin got
    // recreated under the hood without the track itself being deleted -
    // seen in practice) - the slot's audio/routing may still be doing
    // something, but its name/position will never update again and it
    // should be removed and re-added once a current-looking choice
    // exists in "+Item" again. Previously such a slot silently showed
    // "(unnamed)", indistinguishable from "just hasn't synced its name
    // yet" and easy to mistake for cosmetic.
    bool isLive{true};
  };

  /** @brief Internal per-slot state: identity/UI fields plus the live
   * position/size last reported by Scene, kept in sync in onSceneReceived()
   * so processBlock() can read it directly (no Scene-side lookup on the
   * audio thread) for the direction-weighted DSP. */
  struct InputSlotInfo {
    std::string connectionId;  // empty = slot unused
    juce::String name;
    uint32_t colour{0};
    float azimuthDeg{0.0f};
    float elevationDeg{0.0f};
    float distance{1.0f};
    float width{0.0f};
    float height{0.0f};
  };

  // --- UI-facing API -------------------------------------------------
  // Thread-safe snapshot of every Object Scene currently knows about,
  // excluding this plugin's own 9 points and any object already assigned
  // to an input slot (so it won't be offered twice, matching Scene's own
  // "available items" checklist excluding items already on the current
  // programme).
  std::vector<SourceObjectInfo> getAvailableObjects() const;

  // DirectSpeakers/HOA items currently visible on Scene, for display only
  // (see UnsupportedInputInfo) - not addable as reverb inputs.
  std::vector<UnsupportedInputInfo> getAvailableDirectSpeakers() const;
  std::vector<UnsupportedInputInfo> getAvailableHoa() const;

  // Occupied slots only, in slot-index order, for the "active inputs" list.
  std::vector<ActiveInputSlot> getActiveInputSlots() const;

  // Assigns connectionId to the first free slot. Returns the assigned slot
  // index, or -1 if every slot is already occupied (kMaxInputs reached).
  int addInputSlot(const std::string& connectionId);

  // "" clears the slot (no source assigned).
  void setInputSlotSource(int slotIndex, const std::string& connectionId);
  std::string getInputSlotSource(int slotIndex) const;

  // Full cached state (identity + live position/size) for one slot, e.g.
  // for the position-overview display - getAvailableObjects() excludes
  // active sources, so this is how the UI reads an active one's position.
  InputSlotInfo getInputSlotInfo(int slotIndex) const;

  void setPointRouting(int pointIndex, int sceneChannel);
  int getPointRouting(int pointIndex) const;

  // Whether this point is currently registered with Scene at all - ON by
  // default for every point (see the class comment). Turning it off
  // unregisters the point's ObjectBackend; the point's audio output keeps
  // running either way.
  void setPointActive(int pointIndex, bool active);
  bool getPointActive(int pointIndex) const;

  // Object-style per-point parameters: default to the point's nominal ITU
  // direction, overridable afterwards exactly like a real EAR Object.
  PointParams getPointParams(int pointIndex) const;
  void setPointPosition(int pointIndex, float azimuthDeg, float elevationDeg,
                        float distance);
  void setPointGain(int pointIndex, float linearGain);
  void setPointWidth(int pointIndex, float width);
  void setPointHeight(int pointIndex, float height);
  void setPointDepth(int pointIndex, float depth);
  void setPointDiffuse(int pointIndex, float diffuse);
  void resetPointToNominal(int pointIndex);

  // Global DSP controls (reverb_dsp.hpp) - shared by all 9 points.
  DspParams getDspParams() const;
  void setDecaySeconds(float seconds);
  void setDamping(float damping01);
  void setEarlyLevel(float level);
  void setDiffuseLevel(float level);
  void setRoomSize(float scale);
  void setDensity(float density01);
  void setPreDelayMs(float ms);
  void setBandwidth(float bandwidth01);
  void setDirectEnabled(bool enabled);
  void setDirectLevel(float level);
  void setEarlyEnabled(bool enabled);
  void setDiffuseEnabled(bool enabled);
  void setTailEngine(ear::plugin::reverbdsp::TailEngineType type);

  // DSP parameter presets - deliberately just the Mix/Decay/Diffusion
  // params above (DspParams), not per-point routing/position: a preset is
  // a reusable "sound", not a snapshot of which object feeds which point
  // in this particular session.
  static juce::File getPresetsDirectory();
  juce::StringArray getAvailablePresetNames() const;
  void savePresetAs(const juce::String& name);
  bool loadPreset(const juce::String& name);
  void deletePreset(const juce::String& name);

  // "<track name> <letter>", e.g. "Rev L" - falls back to the bare
  // letter if the host hasn't reported a track name (yet, or at all).
  juce::String getPointDisplayName(int pointIndex) const;

  // Fired (on the message thread) whenever the host-reported track name
  // changes, so the editor can refresh every row's displayed name.
  std::function<void()> onTrackNameChanged;
  juce::Colour getTrackColour() const { return trackColour_; }

  // Whether the editor's "Output points" section is currently expanded -
  // kept here (not in the editor) so it survives the editor being closed
  // and reopened, not just the lifetime of one editor instance. Defaults
  // to collapsed for a freshly created processor only.
  bool getPointsExpanded() const { return pointsExpanded_; }
  void setPointsExpanded(bool expanded) { pointsExpanded_ = expanded; }

  const std::array<PointInfo, kNumPoints>& getPointLayout() const {
    return pointInfo_;
  }

  // Fired (on the message thread) whenever the discovered-objects list
  // changes, so the editor can refresh its combo boxes/list without
  // polling.
  std::function<void()> onAvailableObjectsChanged;

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
  // Shared by getStateInformation/setStateInformation's own "Dsp" XML node
  // and by the preset save/load methods above - a preset file is just
  // this same node written to its own standalone file.
  void writeDspXml(juce::XmlElement& dspXml) const;
  void applyDspXml(const juce::XmlElement& dspXml);
  void buildPointConnector(int pointIndex);
  void teardownPointConnector(int pointIndex);

  std::array<PointInfo, kNumPoints> pointInfo_;
  juce::String trackName_;
  juce::Colour trackColour_{juce::Colours::orange};
  bool pointsExpanded_{false};
  void pushAllPointPropertiesToScene();
  void notifyTrackNameChangedOnMessageThread();
  std::array<std::unique_ptr<ear::plugin::ReverbPointConnector>, kNumPoints>
      pointConnectors_;
  std::array<std::unique_ptr<ear::plugin::ObjectBackend>, kNumPoints>
      pointBackends_;
  std::array<bool, kNumPoints> pointActive_;
  std::array<int, kNumPoints> pointRouting_;
  std::array<PointParams, kNumPoints> pointParams_;
  mutable std::mutex pointParamsMutex_;

  std::array<InputSlotInfo, kMaxInputs> inputSlots_;
  mutable std::mutex inputSlotsMutex_;

  // One early-reflection generator per output point, each fed a different
  // direction-weighted mix of the active inputs (see processBlock()), plus
  // the diffuse tail shared by all 10 points - both engines are kept
  // prepared/running at all times (see reverb_tail_engine.hpp) so
  // dspParams_.tailEngine can switch which one is actually read from
  // without a rebuild or a gap.
  std::array<ear::plugin::reverbdsp::EarlyReflections, kNumPoints>
      earlyReflections_;
  ear::plugin::reverbdsp::FdnTailEngine fdnTailEngine_;
  ear::plugin::reverbdsp::MVerbTailEngine mverbTailEngine_;
  mutable std::mutex diffuseNetworkMutex_;
  DspParams dspParams_;

  // Shared pre-delay: one line per point's early-reflection feed, plus one
  // for the diffuse-tail feed, all set to the same preDelayMs.
  std::array<ear::plugin::reverbdsp::SimpleDelayLine, kNumPoints>
      earlyPreDelay_;
  ear::plugin::reverbdsp::SimpleDelayLine diffusePreDelay_;

  // Reused per-block scratch buffers (sized in prepareToPlay, never
  // allocated on the audio thread): one direction-weighted early-feed
  // buffer per point, plus the shared diffuse-send buffer.
  std::array<std::vector<float>, kNumPoints> earlyFeedBuffer_;
  std::vector<float> diffuseFeedBuffer_;
  std::vector<float> directMixBuffer_;  // sum of active inputs, for the Direct path

  ear::plugin::communication::MonitoringControlConnection
      sceneControlConnection_;
  std::unique_ptr<ear::plugin::communication::MonitoringMetadataReceiver>
      sceneMetadataReceiver_;

  mutable std::mutex availableObjectsMutex_;
  std::vector<SourceObjectInfo> availableObjects_;
  std::vector<UnsupportedInputInfo> availableDirectSpeakers_;
  std::vector<UnsupportedInputInfo> availableHoa_;

  // Each of the 10 points is registered with Scene via its own
  // ObjectBackend (see pointConnectors_ below), exactly like a real EAR
  // Object - but unlike a real EAR Object, nothing was ever telling Scene
  // (and, via its channel-mapping data, the reaper-adm-extension's ADM
  // render/export) what INPUT_INSTANCE_ID each point has. Every point
  // therefore reported id 0 (unset), which the exporter then failed to
  // resolve to a real plugin ("Unable to find Input plugin with instance
  // ID 0"). Fixed by requesting a real, extension-assigned id per point -
  // same mechanism (requestInstanceIdFromExtension(), reaper_integration.hpp)
  // real Object/DirectSpeakers/HOA/Object OSC plugins use - and exposing
  // each as its own read-only VST3 parameter (reaper-adm-extension reads
  // these by raw parameter INDEX, not by name: see exportaction_admsource-
  // earvst.h's paramReverbInstanceId array and isInputPlugin()'s "LF
  // Reverb" recognition). These must stay the ONLY addParameter()
  // calls in this processor, added in point order 0..9, so their indices
  // never shift.
  void setIHostApplication(Steinberg::FUnknown* unknown) override;
  IReaperHostApplication* reaperHost_{nullptr};
  std::array<ReadOnlyAudioParameterInt*, kNumPoints> inputInstanceIdParams_{};

  // On top of the instance id, reaper-adm-extension also needs to read
  // each point's actual position/gain to author the exported ADM Object's
  // metadata blocks - the same way it reads a real EAR Object's azimuth/
  // elevation/distance/gain/width/height/depth/diffuse parameters, just
  // at a different (point-dependent) raw VST3 parameter index, since one
  // Reverb instance holds 10 objects' worth of these instead of one (see
  // exportaction_admsource-earvst.cpp's ShiftedPluginParameter). These are
  // write-only mirrors of pointParams_ for that purpose - the UI/DSP never
  // reads them back, so automating one from REAPER has no audible effect,
  // only an effect on what gets exported.
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

  // OSC output: sends /point/<index>/active, /az, /el, /dist whenever a
  // point's own state/position changes - see Point Encoder's identical
  // scheme (point_encoder_plugin_processor.hpp) for the full rationale.
  void sendPointOsc(int pointIndex);
  void reconnectOscOutputLocked();

  bool oscOutputEnabled_{false};
  juce::String oscOutputIp_{"127.0.0.1"};
  int oscOutputPort_{9000};
  juce::OSCSender oscSender_;
  std::mutex oscSenderMutex_;

  class OscInputSubscriber : public ear::plugin::OscPortRegistry::Subscriber {
   public:
    explicit OscInputSubscriber(ReverbAudioProcessor& owner) : owner_(owner) {}
    void handleOscMessage(const juce::OSCMessage& message) override {
      owner_.handleOscMessage(message);
    }
   private:
    ReverbAudioProcessor& owner_;
  };
  void handleOscMessage(const juce::OSCMessage& message);

  bool oscInputEnabled_{false};
  int oscInputPort_{9001};
  OscInputSubscriber oscInputSubscriber_{*this};

  // setOscDebugText()-style deferral to the message thread via a captured
  // weak_ptr to this flag, mirroring object_osc's own aliveFlag_: the Scene
  // metadata callback can fire off the message thread, so a processor
  // destroyed before a deferred callAsync fires is never touched by it.
  std::shared_ptr<bool> aliveFlag_ = std::make_shared<bool>(true);

  int samplerate_{48000};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbAudioProcessor)
};
