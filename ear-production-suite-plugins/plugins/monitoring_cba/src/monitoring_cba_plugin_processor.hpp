#pragma once

#include "JuceHeader.h"

#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <ear/ear.hpp>

#include "communication/monitoring_control_connection.hpp"
#include "scene_store.pb.h"
#include <daw_channel_count.h>

namespace ear {
namespace plugin {
namespace communication {
class MonitoringMetadataReceiver;
}
}  // namespace plugin
}  // namespace ear

/** @brief "LF Decoder" - decodes a user-picked set of EAR
 * Objects straight to channel-based audio (CBA), entirely on its own,
 * without needing EAR Scene's own render path in the chain at all ("pre
 * EAR Scene", per the project's own notes) - only Scene's metadata
 * broadcast is used (for live object position), exactly like "Reverb for
 * EAR Suite" already does for its own input sources (see that plugin's
 * SourceObjectInfo/InputSlotInfo/getAvailableObjects()/addInputSlot() -
 * copied verbatim here).
 *
 * Unlike the stock "EAR Monitoring"/"Custom Monitoring" plugins (which
 * decode *every* object Scene currently has active, assuming each already
 * has a Scene-assigned routing channel), this plugin decodes only the
 * objects the user has explicitly added to one of its own input slots -
 * same "+ Item" picker workflow as the reverb plugins, not a Scene-wide
 * passthrough. That's the point: the output is a self-contained, ordinary
 * multichannel signal (any standard ITU/BS.2051 layout, or a Custom
 * Monitoring-style loaded layout file) that can be sent into a
 * traditional channel-based-audio reverb (even a 7.1.4 one) with no EAR
 * awareness at all - and, since each input is still a live Object with
 * real position metadata upstream of this plugin, nothing about the
 * EAR/ADM side of the session is lost by doing so.
 *
 * DSP: one ear::GainCalculatorObjects per currently-selected output
 * layout (rebuilt whenever the layout changes - see setStandardLayout()/
 * loadCustomLayoutFile()), called once per block per active input slot
 * with that slot's live position/width/height/depth/diffuse (the same
 * public API EAR Scene/Monitoring's own decode uses internally - see
 * ear/gain_calculators.hpp). v1 simplification: the ADM "diffuse" gains
 * are summed directly into the output alongside the "direct" ones,
 * without per-channel decorrelation (the real spec calls for a
 * dedicated FIR filter per output channel, see GainCalculatorObjects'
 * own class comment) - a reasonable approximation at low/moderate
 * diffuseness, clearly less accurate for a fully diffuse object - real
 * decorrelation can be added later without changing this plugin's
 * interface.
 */
class MonitoringCbaAudioProcessor : public juce::AudioProcessor {
 public:
  static constexpr int kMaxInputs = MAX_DAW_CHANNELS;
  // Widest standard layout this plugin offers is "9+10+3" (22.2 - 22 main
  // + 2 LFE = 24 channels) - see getStandardLayoutNames(). A loaded
  // custom layout (see loadCustomLayoutFile()) is capped to this too.
  static constexpr int kMaxOutputChannels = 24;

  /** @brief A snapshot of one Object currently visible to Scene - same
   * shape/role as the algorithmic Reverb's own SourceObjectInfo. */
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

  /** @brief A DirectSpeakers or HOA item visible on Scene - listed for
   * display/parity with the "+ Item" dialog, but not selectable as an
   * input here (same reasoning/limitation as the reverb plugins' own
   * UnsupportedInputInfo - deferred, not forgotten: libear can decode
   * both types too, via GainCalculatorDirectSpeakers/GainCalculatorHOA,
   * this plugin just doesn't wire that up yet). */
  struct UnsupportedInputInfo {
    std::string connectionId;
    juce::String name;
    uint32_t colour{0};
  };

  struct ActiveInputSlot {
    int slotIndex;
    std::string connectionId;
    juce::String name;
    uint32_t colour{0};
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
    float depth{0.0f};
  };

  MonitoringCbaAudioProcessor();
  ~MonitoringCbaAudioProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;
  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
  void processBlock(AudioBuffer<float>&, MidiBuffer&) override;

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

  // --- Output layout -----------------------------------------------------
  // Standard BS.2051-derived layouts offered by the stock "EAR Monitoring"
  // plugin family (see monitoring/CMakeLists.txt's own add_monitoring_plugin
  // calls) - display name first ("5.1"), libear layout name second
  // ("0+5+0"); index into this list is what setStandardLayout() takes.
  struct LayoutChoice {
    juce::String displayName;
    std::string earLayoutName;
  };
  static const std::vector<LayoutChoice>& getStandardLayoutChoices();
  void setStandardLayout(int choiceIndex);
  // Loads a plain-text loudspeaker-layout file - same format/parser as
  // "LF Custom Monitoring" (see ear::plugin::
  // loadCustomLayoutFromFile()'s own doc comment). Throws
  // CustomLayoutFileError on a parse error.
  void loadCustomLayoutFile(const juce::File& file);
  juce::String getCurrentLayoutDisplayName() const;
  int getOutputChannelCount() const;
  // Output channel N's own conventional name (e.g. "M+030"), for the UI.
  juce::StringArray getOutputChannelNames() const;

  std::function<void()> onAvailableObjectsChanged;

  // --- Output OSC (collapsed by default in the editor) - reports each
  // active input slot's live position/state. No Input OSC here: unlike
  // Reverb/IR Reverb/Point Encoder's own fixed "points", this plugin's
  // slot positions come live from Scene, not from a value this plugin
  // itself owns - there is nothing locally meaningful to drive remotely.
  bool getOscOutputEnabled() const { return oscOutputEnabled_; }
  void setOscOutputEnabled(bool enabled);
  juce::String getOscOutputIp() const { return oscOutputIp_; }
  void setOscOutputIp(const juce::String& ip);
  int getOscOutputPort() const { return oscOutputPort_; }
  void setOscOutputPort(int port);

 private:
  void onSceneReceived(const ear::plugin::proto::SceneStore& store);
  void onSceneControlConnection(ear::plugin::communication::ConnectionId id,
                                const std::string& streamEndpoint);
  void onSceneControlConnectionLost();
  void notifyAvailableObjectsChangedOnMessageThread();
  void rebuildGainCalculator();  // called with layoutMutex_ already held
  void sendActiveSlotsOsc();
  void reconnectOscOutputLocked();

  bool oscOutputEnabled_{false};
  juce::String oscOutputIp_{"127.0.0.1"};
  int oscOutputPort_{9000};
  juce::OSCSender oscSender_;
  std::mutex oscSenderMutex_;

  std::array<InputSlotInfo, kMaxInputs> inputSlots_;
  mutable std::mutex inputSlotsMutex_;

  mutable std::mutex layoutMutex_;
  ear::Layout currentLayout_;
  std::unique_ptr<ear::GainCalculatorObjects> gainCalculator_;
  juce::String currentLayoutDisplayName_{"2.0"};
  int outputChannelCount_{2};

  // Reused scratch accumulation buffer (sized kMaxOutputChannels x
  // blockSize in prepareToPlay) - every active source's contribution is
  // summed here before being copied to the real output channels, so an
  // output channel index that happens to coincide with another slot's
  // still-unread input channel is never clobbered mid-block.
  juce::AudioBuffer<float> outputScratch_;

  ear::plugin::communication::MonitoringControlConnection
      sceneControlConnection_;
  std::unique_ptr<ear::plugin::communication::MonitoringMetadataReceiver>
      sceneMetadataReceiver_;

  mutable std::mutex availableObjectsMutex_;
  std::vector<SourceObjectInfo> availableObjects_;
  std::vector<UnsupportedInputInfo> availableDirectSpeakers_;
  std::vector<UnsupportedInputInfo> availableHoa_;

  std::shared_ptr<bool> aliveFlag_ = std::make_shared<bool>(true);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MonitoringCbaAudioProcessor)
};
