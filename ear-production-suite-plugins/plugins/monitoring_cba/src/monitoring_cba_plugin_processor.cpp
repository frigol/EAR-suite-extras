#include "monitoring_cba_plugin_processor.hpp"
#include "monitoring_cba_plugin_editor.hpp"

#include "communication/monitoring_metadata_receiver.hpp"
#include "detail/constants.hpp"
#include "helper/custom_layout_file.hpp"

#include <ear/bs2051.hpp>

#include <algorithm>
#include <cmath>

using namespace ear::plugin;
using std::placeholders::_1;
using std::placeholders::_2;

MonitoringCbaAudioProcessor::MonitoringCbaAudioProcessor()
    : AudioProcessor(
         BusesProperties()
             .withInput("Input", AudioChannelSet::discreteChannels(kMaxInputs), true)
             .withOutput("Output",
                         AudioChannelSet::discreteChannels(kMaxOutputChannels),
                         true)) {
  setStandardLayout(0);  // "2.0" - see getStandardLayoutChoices()

  sceneControlConnection_.onConnectionEstablished(std::bind(
      &MonitoringCbaAudioProcessor::onSceneControlConnection, this, _1, _2));
  sceneControlConnection_.onConnectionLost(std::bind(
      &MonitoringCbaAudioProcessor::onSceneControlConnectionLost, this));
  sceneControlConnection_.start(
      ear::plugin::detail::SCENE_MASTER_CONTROL_ENDPOINT);
}

MonitoringCbaAudioProcessor::~MonitoringCbaAudioProcessor() {
  *aliveFlag_ = false;
  if (sceneMetadataReceiver_) sceneMetadataReceiver_->shutdown();
  sceneControlConnection_.onConnectionLost(nullptr);
  sceneControlConnection_.onConnectionEstablished(nullptr);
}

void MonitoringCbaAudioProcessor::prepareToPlay(double, int samplesPerBlock) {
  outputScratch_.setSize(kMaxOutputChannels, samplesPerBlock, false, false, true);
}

void MonitoringCbaAudioProcessor::releaseResources() {}

bool MonitoringCbaAudioProcessor::isBusesLayoutSupported(
    const BusesLayout& layouts) const {
  if (layouts.inputBuses.size() != 1) return false;
  if (layouts.inputBuses[0] != AudioChannelSet::discreteChannels(kMaxInputs))
    return false;
  if (layouts.outputBuses.size() != 1) return false;
  if (layouts.outputBuses[0] !=
      AudioChannelSet::discreteChannels(kMaxOutputChannels))
    return false;
  return true;
}

void MonitoringCbaAudioProcessor::processBlock(AudioBuffer<float>& buffer,
                                               MidiBuffer&) {
  const int numSamples = buffer.getNumSamples();
  const int numChannels = buffer.getNumChannels();

  struct ActiveSource {
    int channel;
    ear::ObjectsTypeMetadata meta;
  };
  std::vector<ActiveSource> sources;
  {
    std::lock_guard<std::mutex> lock(inputSlotsMutex_);
    for (int ch = 0; ch < kMaxInputs; ++ch) {
      auto const& slot = inputSlots_[static_cast<size_t>(ch)];
      if (slot.connectionId.empty() || ch >= numChannels) continue;
      ActiveSource src;
      src.channel = ch;
      src.meta.position =
          ear::PolarPosition(static_cast<double>(slot.azimuthDeg),
                             static_cast<double>(slot.elevationDeg),
                             static_cast<double>(slot.distance));
      src.meta.width = static_cast<double>(slot.width);
      src.meta.height = static_cast<double>(slot.height);
      src.meta.depth = static_cast<double>(slot.depth);
      sources.push_back(src);
    }
  }

  int outCh;
  {
    std::lock_guard<std::mutex> lock(layoutMutex_);
    outCh = outputChannelCount_;
    outCh = juce::jmin(outCh, kMaxOutputChannels);

    for (int ch = 0; ch < outCh; ++ch)
      outputScratch_.clear(ch, 0, numSamples);

    if (gainCalculator_) {
      // GainCalculatorObjects::calculate() requires its output vectors to
      // already be exactly outCh long - it throws ear::invalid_argument
      // rather than resizing them itself (see OutputGainsT::check_size()
      // in libear's own output_gains.hpp). Left as default-constructed
      // (empty) vectors here, that exception was uncaught in
      // processBlock() and crashed the host outright the moment an
      // object was added - this is what actually fixes that.
      std::vector<float> directGains(static_cast<size_t>(outCh), 0.0f);
      std::vector<float> diffuseGains(static_cast<size_t>(outCh), 0.0f);
      for (auto const& src : sources) {
        // Belt-and-braces: an uncaught exception crossing processBlock()
        // always kills the host outright, so no path into libear here is
        // allowed to propagate one, regardless of cause.
        try {
          gainCalculator_->calculate(src.meta, directGains, diffuseGains);
        } catch (const std::exception&) {
          continue;
        }
        const float* in = buffer.getReadPointer(src.channel);
        int n = juce::jmin(outCh, static_cast<int>(directGains.size()));
        for (int ch = 0; ch < n; ++ch) {
          float gain = directGains[static_cast<size_t>(ch)] +
                       diffuseGains[static_cast<size_t>(ch)];
          if (gain < 1.0e-6f) continue;
          float* out = outputScratch_.getWritePointer(ch);
          for (int s = 0; s < numSamples; ++s) out[s] += gain * in[s];
        }
      }
    }
  }

  for (int ch = 0; ch < numChannels; ++ch) {
    if (ch < outCh)
      buffer.copyFrom(ch, 0, outputScratch_, ch, 0, numSamples);
    else
      buffer.clear(ch, 0, numSamples);
  }
}

AudioProcessorEditor* MonitoringCbaAudioProcessor::createEditor() {
  return new MonitoringCbaAudioProcessorEditor(this);
}

const String MonitoringCbaAudioProcessor::getName() const {
  return JucePlugin_Name;
}

std::vector<MonitoringCbaAudioProcessor::SourceObjectInfo>
MonitoringCbaAudioProcessor::getAvailableObjects() const {
  std::vector<SourceObjectInfo> result;
  {
    std::lock_guard<std::mutex> lock(availableObjectsMutex_);
    result = availableObjects_;
  }
  std::lock_guard<std::mutex> slotsLock(inputSlotsMutex_);
  result.erase(
      std::remove_if(result.begin(), result.end(),
                     [this](SourceObjectInfo const& obj) {
                       for (auto const& slot : inputSlots_)
                         if (slot.connectionId == obj.connectionId) return true;
                       return false;
                     }),
      result.end());
  return result;
}

std::vector<MonitoringCbaAudioProcessor::UnsupportedInputInfo>
MonitoringCbaAudioProcessor::getAvailableDirectSpeakers() const {
  std::lock_guard<std::mutex> lock(availableObjectsMutex_);
  return availableDirectSpeakers_;
}

std::vector<MonitoringCbaAudioProcessor::UnsupportedInputInfo>
MonitoringCbaAudioProcessor::getAvailableHoa() const {
  std::lock_guard<std::mutex> lock(availableObjectsMutex_);
  return availableHoa_;
}

std::vector<MonitoringCbaAudioProcessor::ActiveInputSlot>
MonitoringCbaAudioProcessor::getActiveInputSlots() const {
  std::vector<std::string> liveIds;
  {
    std::lock_guard<std::mutex> lock(availableObjectsMutex_);
    liveIds.reserve(availableObjects_.size());
    for (auto const& obj : availableObjects_) liveIds.push_back(obj.connectionId);
  }

  std::vector<ActiveInputSlot> result;
  std::lock_guard<std::mutex> lock(inputSlotsMutex_);
  for (int i = 0; i < kMaxInputs; ++i) {
    auto const& slot = inputSlots_[static_cast<size_t>(i)];
    if (slot.connectionId.empty()) continue;
    bool isLive = std::find(liveIds.begin(), liveIds.end(), slot.connectionId) !=
                 liveIds.end();
    result.push_back({i, slot.connectionId, slot.name, slot.colour, isLive});
  }
  return result;
}

int MonitoringCbaAudioProcessor::addInputSlot(const std::string& connectionId) {
  if (connectionId.empty()) return -1;

  SourceObjectInfo match;
  bool found = false;
  {
    std::lock_guard<std::mutex> lock(availableObjectsMutex_);
    for (auto const& obj : availableObjects_) {
      if (obj.connectionId == connectionId) {
        match = obj;
        found = true;
        break;
      }
    }
  }

  std::lock_guard<std::mutex> lock(inputSlotsMutex_);
  for (int i = 0; i < kMaxInputs; ++i) {
    if (inputSlots_[static_cast<size_t>(i)].connectionId == connectionId)
      return i;
  }
  for (int i = 0; i < kMaxInputs; ++i) {
    auto& slot = inputSlots_[static_cast<size_t>(i)];
    if (slot.connectionId.empty()) {
      slot.connectionId = connectionId;
      if (found) {
        slot.name = match.name;
        slot.colour = match.colour;
        slot.azimuthDeg = match.azimuthDeg;
        slot.elevationDeg = match.elevationDeg;
        slot.distance = match.distance;
        slot.width = match.width;
        slot.height = match.height;
        slot.depth = match.depth;
      }
      return i;
    }
  }
  return -1;
}

void MonitoringCbaAudioProcessor::setInputSlotSource(
    int slotIndex, const std::string& connectionId) {
  if (slotIndex < 0 || slotIndex >= kMaxInputs) return;
  std::lock_guard<std::mutex> lock(inputSlotsMutex_);
  if (connectionId.empty()) {
    inputSlots_[static_cast<size_t>(slotIndex)] = InputSlotInfo{};
  } else {
    inputSlots_[static_cast<size_t>(slotIndex)].connectionId = connectionId;
  }
}

std::string MonitoringCbaAudioProcessor::getInputSlotSource(
    int slotIndex) const {
  if (slotIndex < 0 || slotIndex >= kMaxInputs) return "";
  std::lock_guard<std::mutex> lock(inputSlotsMutex_);
  return inputSlots_[static_cast<size_t>(slotIndex)].connectionId;
}

MonitoringCbaAudioProcessor::InputSlotInfo
MonitoringCbaAudioProcessor::getInputSlotInfo(int slotIndex) const {
  if (slotIndex < 0 || slotIndex >= kMaxInputs) return {};
  std::lock_guard<std::mutex> lock(inputSlotsMutex_);
  return inputSlots_[static_cast<size_t>(slotIndex)];
}

const std::vector<MonitoringCbaAudioProcessor::LayoutChoice>&
MonitoringCbaAudioProcessor::getStandardLayoutChoices() {
  // Same set the stock "EAR Monitoring" plugin family offers, one binary
  // per layout (see plugins/monitoring/CMakeLists.txt's own
  // add_monitoring_plugin() calls) - collected here into one runtime
  // dropdown instead.
  static const std::vector<LayoutChoice> kChoices = {
      {"2.0", "0+2+0"},        {"5.1", "0+5+0"},
      {"5.1+2H", "2+5+0"},     {"5.1+4H", "4+5+0"},
      {"7.2+3H", "3+7+0"},     {"7.1", "0+7+0"},
      {"7.1+2H", "2+7+0"},     {"7.1+4H (7.1.4)", "4+7+0"},
      {"9.1+4H", "4+9+0"},     {"22.2", "9+10+3"},
  };
  return kChoices;
}

void MonitoringCbaAudioProcessor::rebuildGainCalculator() {
  // Called with layoutMutex_ already held.
  gainCalculator_ = std::make_unique<ear::GainCalculatorObjects>(currentLayout_);
  outputChannelCount_ =
      static_cast<int>(currentLayout_.channels().size());
}

void MonitoringCbaAudioProcessor::setStandardLayout(int choiceIndex) {
  auto const& choices = getStandardLayoutChoices();
  if (choiceIndex < 0 || choiceIndex >= static_cast<int>(choices.size()))
    return;
  auto const& choice = choices[static_cast<size_t>(choiceIndex)];

  std::lock_guard<std::mutex> lock(layoutMutex_);
  // "2+7+0" has no direct BS.2051 entry of its own in libear - same
  // special-cased construction the stock Monitoring plugin itself uses
  // (see monitoring_plugin_processor.cpp's own getLayoutImpl()): its
  // first 8 speakers match "4+9+0", and its last 2 (U+090/U-090) come
  // from "9+10+3".
  if (choice.earLayoutName == "2+7+0") {
    auto channels_4_9_0 = ear::getLayout("4+9+0").channels();
    std::vector<ear::Channel> channels_2_7_0(channels_4_9_0.begin(),
                                             channels_4_9_0.begin() + 8);
    auto channels_9_10_3 = ear::getLayout("9+10+3").channels();
    channels_2_7_0.push_back(channels_9_10_3.at(12));
    channels_2_7_0.push_back(channels_9_10_3.at(13));
    currentLayout_ = ear::Layout{"2+7+0", channels_2_7_0};
  } else {
    currentLayout_ = ear::getLayout(choice.earLayoutName);
  }
  currentLayoutDisplayName_ = choice.displayName;
  rebuildGainCalculator();
}

void MonitoringCbaAudioProcessor::loadCustomLayoutFile(const juce::File& file) {
  auto customLayout =
      loadCustomLayoutFromFile(file.getFullPathName().toStdString());
  if (static_cast<int>(customLayout.layout.channels().size()) >
      kMaxOutputChannels) {
    throw CustomLayoutFileError(
        "Layout has more speakers (" +
        std::to_string(customLayout.layout.channels().size()) +
        ") than this plugin's own output width (" +
        std::to_string(kMaxOutputChannels) + ")");
  }
  std::lock_guard<std::mutex> lock(layoutMutex_);
  currentLayout_ = customLayout.layout;
  currentLayoutDisplayName_ = "Custom: " + file.getFileNameWithoutExtension();
  rebuildGainCalculator();
}

juce::String MonitoringCbaAudioProcessor::getCurrentLayoutDisplayName() const {
  std::lock_guard<std::mutex> lock(layoutMutex_);
  return currentLayoutDisplayName_;
}

int MonitoringCbaAudioProcessor::getOutputChannelCount() const {
  std::lock_guard<std::mutex> lock(layoutMutex_);
  return outputChannelCount_;
}

juce::StringArray MonitoringCbaAudioProcessor::getOutputChannelNames() const {
  juce::StringArray names;
  std::lock_guard<std::mutex> lock(layoutMutex_);
  for (auto const& ch : currentLayout_.channels())
    names.add(juce::String(ch.name()));
  return names;
}

void MonitoringCbaAudioProcessor::onSceneControlConnection(
    communication::ConnectionId, const std::string& streamEndpoint) {
  sceneMetadataReceiver_ =
      std::make_unique<communication::MonitoringMetadataReceiver>();
  sceneMetadataReceiver_->start(
      streamEndpoint,
      std::bind(&MonitoringCbaAudioProcessor::onSceneReceived, this, _1));
}

void MonitoringCbaAudioProcessor::onSceneControlConnectionLost() {
  if (sceneMetadataReceiver_) sceneMetadataReceiver_->shutdown();
  {
    std::lock_guard<std::mutex> lock(availableObjectsMutex_);
    availableObjects_.clear();
    availableDirectSpeakers_.clear();
    availableHoa_.clear();
  }
  notifyAvailableObjectsChangedOnMessageThread();
}

void MonitoringCbaAudioProcessor::onSceneReceived(
    const ear::plugin::proto::SceneStore& store) {
  std::vector<SourceObjectInfo> found;
  std::vector<UnsupportedInputInfo> foundDirectSpeakers;
  std::vector<UnsupportedInputInfo> foundHoa;
  for (auto const& item : store.all_available_items()) {
    auto const& id = item.connection_id();

    if (item.has_ds_metadata()) {
      foundDirectSpeakers.push_back(
          {id, juce::String(item.name()), item.colour()});
      continue;
    }
    if (item.has_hoa_metadata()) {
      foundHoa.push_back({id, juce::String(item.name()), item.colour()});
      continue;
    }
    if (!item.has_obj_metadata()) continue;

    SourceObjectInfo info;
    info.connectionId = id;
    info.name = juce::String(item.name());
    info.colour = item.colour();
    auto const& obj = item.obj_metadata();
    if (obj.has_position()) {
      info.azimuthDeg = static_cast<float>(obj.position().azimuth());
      info.elevationDeg = static_cast<float>(obj.position().elevation());
      info.distance = static_cast<float>(obj.position().distance());
    }
    info.width = static_cast<float>(obj.width());
    info.height = static_cast<float>(obj.height());
    info.depth = static_cast<float>(obj.depth());
    found.push_back(std::move(info));
  }

  {
    std::lock_guard<std::mutex> lock(availableObjectsMutex_);
    availableObjects_ = found;
    availableDirectSpeakers_ = std::move(foundDirectSpeakers);
    availableHoa_ = std::move(foundHoa);
  }

  {
    std::lock_guard<std::mutex> lock(inputSlotsMutex_);
    for (auto& slot : inputSlots_) {
      if (slot.connectionId.empty()) continue;
      for (auto const& obj : found) {
        if (obj.connectionId == slot.connectionId) {
          slot.name = obj.name;
          slot.colour = obj.colour;
          slot.azimuthDeg = obj.azimuthDeg;
          slot.elevationDeg = obj.elevationDeg;
          slot.distance = obj.distance;
          slot.width = obj.width;
          slot.height = obj.height;
          slot.depth = obj.depth;
          break;
        }
      }
    }
  }

  sendActiveSlotsOsc();
  notifyAvailableObjectsChangedOnMessageThread();
}

void MonitoringCbaAudioProcessor::notifyAvailableObjectsChangedOnMessageThread() {
  std::weak_ptr<bool> weakAlive = aliveFlag_;
  MessageManager::callAsync([this, weakAlive]() {
    if (weakAlive.expired()) return;
    if (onAvailableObjectsChanged) onAvailableObjectsChanged();
  });
}

void MonitoringCbaAudioProcessor::getStateInformation(MemoryBlock& destData) {
  std::unique_ptr<XmlElement> xml(new XmlElement("MonitoringCbaPlugin"));

  auto* layoutXml = xml->createNewChildElement("Layout");
  layoutXml->setAttribute("display_name", getCurrentLayoutDisplayName());

  auto* slotsXml = xml->createNewChildElement("InputSlots");
  {
    std::lock_guard<std::mutex> lock(inputSlotsMutex_);
    for (int i = 0; i < kMaxInputs; ++i) {
      auto* s = slotsXml->createNewChildElement("Slot");
      s->setAttribute("index", i);
      s->setAttribute(
          "source_connection_id",
          juce::String(inputSlots_[static_cast<size_t>(i)].connectionId));
    }
  }

  copyXmlToBinary(*xml, destData);
}

void MonitoringCbaAudioProcessor::setStateInformation(const void* data,
                                                      int sizeInBytes) {
  std::unique_ptr<XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
  if (!xmlState || !xmlState->hasTagName("MonitoringCbaPlugin")) return;

  if (auto* layoutXml = xmlState->getChildByName("Layout")) {
    auto displayName = layoutXml->getStringAttribute("display_name", "2.0");
    auto const& choices = getStandardLayoutChoices();
    for (int i = 0; i < static_cast<int>(choices.size()); ++i) {
      if (choices[static_cast<size_t>(i)].displayName == displayName) {
        setStandardLayout(i);
        break;
      }
    }
  }

  if (auto* slotsXml = xmlState->getChildByName("InputSlots")) {
    for (auto* s : slotsXml->getChildWithTagNameIterator("Slot")) {
      int i = s->getIntAttribute("index", -1);
      if (i < 0 || i >= kMaxInputs) continue;
      setInputSlotSource(
          i, s->getStringAttribute("source_connection_id", "").toStdString());
    }
  }
}

// /slot/<index>/active, /az, /el, /dist - <index> is this plugin's own
// input-slot number, not a Scene connection id, matching the editor's own
// active-inputs list order. Unoccupied slots are skipped entirely (there
// is no "inactive but present" state for a slot the way Reverb/IR Reverb/
// Point Encoder's own points have).
void MonitoringCbaAudioProcessor::sendActiveSlotsOsc() {
  std::lock_guard<std::mutex> lock(oscSenderMutex_);
  if (!oscOutputEnabled_) return;
  std::array<InputSlotInfo, kMaxInputs> snapshot;
  {
    std::lock_guard<std::mutex> slotsLock(inputSlotsMutex_);
    snapshot = inputSlots_;
  }
  for (int i = 0; i < kMaxInputs; ++i) {
    auto const& slot = snapshot[static_cast<size_t>(i)];
    bool active = !slot.connectionId.empty();
    juce::String base = "/slot/" + juce::String(i);
    oscSender_.send(base + "/active", active ? 1 : 0);
    if (!active) continue;
    oscSender_.send(base + "/az", slot.azimuthDeg);
    oscSender_.send(base + "/el", slot.elevationDeg);
    oscSender_.send(base + "/dist", slot.distance);
  }
}

void MonitoringCbaAudioProcessor::reconnectOscOutputLocked() {
  oscSender_.disconnect();
  if (oscOutputEnabled_) oscSender_.connect(oscOutputIp_, oscOutputPort_);
}

void MonitoringCbaAudioProcessor::setOscOutputEnabled(bool enabled) {
  std::lock_guard<std::mutex> lock(oscSenderMutex_);
  oscOutputEnabled_ = enabled;
  reconnectOscOutputLocked();
}

void MonitoringCbaAudioProcessor::setOscOutputIp(const juce::String& ip) {
  std::lock_guard<std::mutex> lock(oscSenderMutex_);
  oscOutputIp_ = ip;
  reconnectOscOutputLocked();
}

void MonitoringCbaAudioProcessor::setOscOutputPort(int port) {
  std::lock_guard<std::mutex> lock(oscSenderMutex_);
  oscOutputPort_ = port;
  reconnectOscOutputLocked();
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
  return new MonitoringCbaAudioProcessor();
}
