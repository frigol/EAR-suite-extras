#include "point_encoder_plugin_processor.hpp"
#include "point_encoder_plugin_editor.hpp"

#include "reaper_integration.hpp"
#include "helper/custom_layout_file.hpp"

#include <ear/bs2051.hpp>

#include <cmath>

using namespace ear::plugin;

PointEncoderAudioProcessor::PointEncoderAudioProcessor()
    : AudioProcessor(
         BusesProperties()
             .withInput("Input", AudioChannelSet::discreteChannels(kMaxPoints), true)
             .withOutput("Output", AudioChannelSet::discreteChannels(kMaxPoints), true)) {
  // These must stay the ONLY addParameter() calls in this processor, added
  // in point order 0..kMaxPoints-1 - see the header's own comment and
  // reaper-adm-extension-patch/exportaction_admsource-earvst.h.
  for (int i = 0; i < kMaxPoints; ++i) {
    addParameter(inputInstanceIdParams_[static_cast<size_t>(i)] =
                     new ReadOnlyAudioParameterInt(
                         "pointEncoderInstanceId" + juce::String(i),
                         "Auto-set ID for point " + juce::String(i), 0, 65535,
                         0));
  }
  for (int i = 0; i < kMaxPoints; ++i) {
    auto& ep = pointExportParams_[static_cast<size_t>(i)];
    juce::String n(i);
    addParameter(ep.azimuth = new juce::AudioParameterFloat(
                     "peAz" + n, "Point " + n + " Azimuth",
                     juce::NormalisableRange<float>{-180.f, 180.f}, 0.f));
    addParameter(ep.elevation = new juce::AudioParameterFloat(
                     "peEl" + n, "Point " + n + " Elevation",
                     juce::NormalisableRange<float>{-90.f, 90.f}, 0.f));
    addParameter(ep.distance = new juce::AudioParameterFloat(
                     "peDist" + n, "Point " + n + " Distance", 0.f, 1.f, 1.f));
    addParameter(ep.gain = new juce::AudioParameterFloat(
                     "peGain" + n, "Point " + n + " Gain",
                     juce::NormalisableRange<float>{-100.f, 6.f}, 0.f));
    addParameter(ep.width = new juce::AudioParameterFloat(
                     "peWidth" + n, "Point " + n + " Width", 0.f, 1.f, 0.f));
    addParameter(ep.height = new juce::AudioParameterFloat(
                     "peHeight" + n, "Point " + n + " Height", 0.f, 1.f, 0.f));
    addParameter(ep.depth = new juce::AudioParameterFloat(
                     "peDepth" + n, "Point " + n + " Depth", 0.f, 1.f, 0.f));
    addParameter(ep.diffuse = new juce::AudioParameterFloat(
                     "peDiffuse" + n, "Point " + n + " Diffuse", 0.f, 1.f, 0.f));
  }

  for (int i = 0; i < kMaxPoints; ++i) {
    pointState_[static_cast<size_t>(i)].inputChannel = i;
    pointState_[static_cast<size_t>(i)].name = "Point " + juce::String(i + 1);
    pushExportParams(i);
  }
}

PointEncoderAudioProcessor::~PointEncoderAudioProcessor() {
  *aliveFlag_ = false;
  if (oscInputEnabled_)
    OscPortRegistry::getInstance().unsubscribe(oscInputPort_, &oscInputSubscriber_);
}

void PointEncoderAudioProcessor::setIHostApplication(
    Steinberg::FUnknown* unknown) {
  reaperHost_ = dynamic_cast<IReaperHostApplication*>(unknown);
  VST3ClientExtensions::setIHostApplication(unknown);
  if (!reaperHost_) return;

  // Requested unconditionally for every slot regardless of active state -
  // cheap (just a counter from the host extension), and means activating a
  // point later never needs to revisit this step.
  for (int i = 0; i < kMaxPoints; ++i) {
    int id = static_cast<int>(requestInstanceIdFromExtension(reaperHost_));
    inputInstanceIdParams_[static_cast<size_t>(i)]->internalSetIntAndNotifyHost(id);
    if (pointConnectors_[static_cast<size_t>(i)])
      pointConnectors_[static_cast<size_t>(i)]->setInputInstanceId(id);
  }
}

void PointEncoderAudioProcessor::prepareToPlay(double, int samplesPerBlock) {
  inputCopy_.setSize(kMaxPoints, samplesPerBlock, false, false, true);
}

void PointEncoderAudioProcessor::releaseResources() {}

bool PointEncoderAudioProcessor::isBusesLayoutSupported(
    const BusesLayout& layouts) const {
  if (layouts.inputBuses.size() != 1) return false;
  if (layouts.inputBuses[0] != AudioChannelSet::discreteChannels(kMaxPoints))
    return false;
  if (layouts.outputBuses.size() != 1) return false;
  if (layouts.outputBuses[0] != AudioChannelSet::discreteChannels(kMaxPoints))
    return false;
  return true;
}

void PointEncoderAudioProcessor::processBlock(AudioBuffer<float>& buffer,
                                              MidiBuffer&) {
  const int numSamples = buffer.getNumSamples();
  const int numChannels = buffer.getNumChannels();

  // Plain per-point pass-through: each active point's output channel is a
  // copy of whichever input channel its own mapping selects (see
  // setPointInputChannel()) - no DSP here, this plugin only encodes
  // direction/metadata, it never alters the audio itself.
  std::array<PointState, kMaxPoints> snapshot;
  {
    std::lock_guard<std::mutex> lock(pointStateMutex_);
    snapshot = pointState_;
  }

  // Read every output channel's source first (into a reused scratch copy,
  // sized once in prepareToPlay() - no audio-thread allocation) - in-place
  // since input/output buses are the same width and a point's own input
  // channel is very often itself, but not guaranteed to be (another
  // point's mapping could read this channel too), so copy out before any
  // channel gets overwritten.
  for (int ch = 0; ch < numChannels && ch < kMaxPoints; ++ch)
    inputCopy_.copyFrom(ch, 0, buffer, ch, 0, numSamples);

  for (int p = 0; p < kMaxPoints && p < numChannels; ++p) {
    float* out = buffer.getWritePointer(p);
    auto const& st = snapshot[static_cast<size_t>(p)];
    if (st.active && st.inputChannel >= 0 && st.inputChannel < numChannels) {
      const float* in = inputCopy_.getReadPointer(st.inputChannel);
      std::memcpy(out, in, static_cast<size_t>(numSamples) * sizeof(float));
    } else {
      std::fill(out, out + numSamples, 0.0f);
    }
  }
  for (int ch = kMaxPoints; ch < numChannels; ++ch) buffer.clear(ch, 0, numSamples);

  for (auto const& backend : pointBackends_)
    if (backend) backend->triggerMetadataSend();
}

AudioProcessorEditor* PointEncoderAudioProcessor::createEditor() {
  return new PointEncoderAudioProcessorEditor(this);
}

const String PointEncoderAudioProcessor::getName() const {
  return JucePlugin_Name;
}

PointEncoderAudioProcessor::PointState
PointEncoderAudioProcessor::getPointState(int pointIndex) const {
  if (pointIndex < 0 || pointIndex >= kMaxPoints) return {};
  std::lock_guard<std::mutex> lock(pointStateMutex_);
  return pointState_[static_cast<size_t>(pointIndex)];
}

void PointEncoderAudioProcessor::buildPointConnector(int pointIndex) {
  auto idx = static_cast<size_t>(pointIndex);
  if (pointConnectors_[idx]) return;  // already live
  pointConnectors_[idx] = std::make_unique<PointEncoderPointConnector>();
  pointBackends_[idx] =
      std::make_unique<ObjectBackend>(pointConnectors_[idx].get());
  if (reaperHost_) {
    pointConnectors_[idx]->setInputInstanceId(
        inputInstanceIdParams_[idx]->get());
  }
  pushPointPropertiesToScene(pointIndex);
}

void PointEncoderAudioProcessor::teardownPointConnector(int pointIndex) {
  auto idx = static_cast<size_t>(pointIndex);
  pointBackends_[idx].reset();
  pointConnectors_[idx].reset();
}

void PointEncoderAudioProcessor::updateConnectorLifecycle(int pointIndex) {
  // A point only exists to Scene once BOTH active and reporting on a real
  // EAR channel - unlike the ten always-live points the algorithmic
  // Reverb used to have, this plugin's points are meant to be wired up in
  // two explicit steps (activate, then assign a channel), so a point
  // that's activated but not yet routed shouldn't show up in Scene as a
  // dangling, silent Object - see setPointEarChannel()'s own comment.
  auto st = getPointState(pointIndex);
  if (st.active && st.earChannel >= 0)
    buildPointConnector(pointIndex);
  else
    teardownPointConnector(pointIndex);
}

void PointEncoderAudioProcessor::pushPointPropertiesToScene(int pointIndex) {
  auto idx = static_cast<size_t>(pointIndex);
  auto* c = pointConnectors_[idx].get();
  if (!c) return;
  PointState st;
  {
    std::lock_guard<std::mutex> lock(pointStateMutex_);
    st = pointState_[idx];
  }
  c->setName(st.name.toStdString());
  c->setColour(trackColour_.getARGB());
  c->setRouting(st.earChannel);
  c->setPosition(st.azimuthDeg, st.elevationDeg, st.distance);
  c->setGain(st.gain);
  c->setWidth(st.width);
  c->setHeight(st.height);
  c->setDepth(st.depth);
  c->setDiffuse(st.diffuse);
}

void PointEncoderAudioProcessor::setPointActive(int pointIndex, bool active) {
  if (pointIndex < 0 || pointIndex >= kMaxPoints) return;
  {
    std::lock_guard<std::mutex> lock(pointStateMutex_);
    pointState_[static_cast<size_t>(pointIndex)].active = active;
  }
  updateConnectorLifecycle(pointIndex);
  notifyPointsChangedOnMessageThread();
  sendPointOsc(pointIndex);
}

void PointEncoderAudioProcessor::setPointName(int pointIndex,
                                              const juce::String& name) {
  if (pointIndex < 0 || pointIndex >= kMaxPoints) return;
  {
    std::lock_guard<std::mutex> lock(pointStateMutex_);
    pointState_[static_cast<size_t>(pointIndex)].name = name;
  }
  pushPointPropertiesToScene(pointIndex);
}

void PointEncoderAudioProcessor::setPointInputChannel(int pointIndex,
                                                       int inputChannel) {
  if (pointIndex < 0 || pointIndex >= kMaxPoints) return;
  std::lock_guard<std::mutex> lock(pointStateMutex_);
  pointState_[static_cast<size_t>(pointIndex)].inputChannel = inputChannel;
}

void PointEncoderAudioProcessor::setPointEarChannel(int pointIndex,
                                                    int earChannel) {
  if (pointIndex < 0 || pointIndex >= kMaxPoints) return;
  {
    std::lock_guard<std::mutex> lock(pointStateMutex_);
    pointState_[static_cast<size_t>(pointIndex)].earChannel = earChannel;
  }
  updateConnectorLifecycle(pointIndex);
  if (auto* c = pointConnectors_[static_cast<size_t>(pointIndex)].get())
    c->setRouting(earChannel);
  notifyPointsChangedOnMessageThread();
}

void PointEncoderAudioProcessor::setPointPosition(int pointIndex,
                                                  float azimuthDeg,
                                                  float elevationDeg,
                                                  float distance) {
  if (pointIndex < 0 || pointIndex >= kMaxPoints) return;
  {
    std::lock_guard<std::mutex> lock(pointStateMutex_);
    auto& st = pointState_[static_cast<size_t>(pointIndex)];
    st.azimuthDeg = azimuthDeg;
    st.elevationDeg = elevationDeg;
    st.distance = juce::jmax(0.0f, distance);
  }
  if (auto* c = pointConnectors_[static_cast<size_t>(pointIndex)].get())
    c->setPosition(azimuthDeg, elevationDeg, juce::jmax(0.0f, distance));
  pushExportParams(pointIndex);
  sendPointOsc(pointIndex);
}

void PointEncoderAudioProcessor::setPointGain(int pointIndex,
                                              float linearGain) {
  if (pointIndex < 0 || pointIndex >= kMaxPoints) return;
  {
    std::lock_guard<std::mutex> lock(pointStateMutex_);
    pointState_[static_cast<size_t>(pointIndex)].gain = linearGain;
  }
  if (auto* c = pointConnectors_[static_cast<size_t>(pointIndex)].get())
    c->setGain(linearGain);
  pushExportParams(pointIndex);
}

void PointEncoderAudioProcessor::setPointWidth(int pointIndex, float width) {
  if (pointIndex < 0 || pointIndex >= kMaxPoints) return;
  {
    std::lock_guard<std::mutex> lock(pointStateMutex_);
    pointState_[static_cast<size_t>(pointIndex)].width = width;
  }
  if (auto* c = pointConnectors_[static_cast<size_t>(pointIndex)].get())
    c->setWidth(width);
  pushExportParams(pointIndex);
}

void PointEncoderAudioProcessor::setPointHeight(int pointIndex, float height) {
  if (pointIndex < 0 || pointIndex >= kMaxPoints) return;
  {
    std::lock_guard<std::mutex> lock(pointStateMutex_);
    pointState_[static_cast<size_t>(pointIndex)].height = height;
  }
  if (auto* c = pointConnectors_[static_cast<size_t>(pointIndex)].get())
    c->setHeight(height);
  pushExportParams(pointIndex);
}

void PointEncoderAudioProcessor::setPointDepth(int pointIndex, float depth) {
  if (pointIndex < 0 || pointIndex >= kMaxPoints) return;
  {
    std::lock_guard<std::mutex> lock(pointStateMutex_);
    pointState_[static_cast<size_t>(pointIndex)].depth = depth;
  }
  if (auto* c = pointConnectors_[static_cast<size_t>(pointIndex)].get())
    c->setDepth(depth);
  pushExportParams(pointIndex);
}

void PointEncoderAudioProcessor::setPointDiffuse(int pointIndex,
                                                 float diffuse) {
  if (pointIndex < 0 || pointIndex >= kMaxPoints) return;
  {
    std::lock_guard<std::mutex> lock(pointStateMutex_);
    pointState_[static_cast<size_t>(pointIndex)].diffuse = diffuse;
  }
  if (auto* c = pointConnectors_[static_cast<size_t>(pointIndex)].get())
    c->setDiffuse(diffuse);
  pushExportParams(pointIndex);
}

void PointEncoderAudioProcessor::resetPointToDefault(int pointIndex) {
  if (pointIndex < 0 || pointIndex >= kMaxPoints) return;
  PointState defaults;
  defaults.active = getPointState(pointIndex).active;
  defaults.name = "Point " + juce::String(pointIndex + 1);
  defaults.inputChannel = pointIndex;
  {
    std::lock_guard<std::mutex> lock(pointStateMutex_);
    bool active = pointState_[static_cast<size_t>(pointIndex)].active;
    pointState_[static_cast<size_t>(pointIndex)] = defaults;
    pointState_[static_cast<size_t>(pointIndex)].active = active;
  }
  pushPointPropertiesToScene(pointIndex);
  pushExportParams(pointIndex);
}

void PointEncoderAudioProcessor::pushExportParams(int pointIndex) {
  auto st = getPointState(pointIndex);
  auto& ep = pointExportParams_[static_cast<size_t>(pointIndex)];
  *ep.azimuth = st.azimuthDeg;
  *ep.elevation = st.elevationDeg;
  *ep.distance = juce::jlimit(0.0f, 1.0f, st.distance);
  *ep.gain = juce::jlimit(-100.0f, 6.0f,
                          juce::Decibels::gainToDecibels(st.gain, -100.0f));
  *ep.width = st.width;
  *ep.height = st.height;
  *ep.depth = st.depth;
  *ep.diffuse = st.diffuse;
}

void PointEncoderAudioProcessor::loadLayoutFile(const juce::File& file) {
  auto customLayout =
      loadCustomLayoutFromFile(file.getFullPathName().toStdString());
  auto const& channels = customLayout.layout.channels();
  int n = juce::jmin(static_cast<int>(channels.size()), kMaxPoints);
  for (int i = 0; i < n; ++i) {
    auto const& ch = channels[static_cast<size_t>(i)];
    if (ch.isLfe()) continue;  // no single direction to encode
    auto pos = ch.polarPosition();
    double gain = i < static_cast<int>(customLayout.outputGains.size())
                      ? customLayout.outputGains[static_cast<size_t>(i)]
                      : 1.0;
    {
      std::lock_guard<std::mutex> lock(pointStateMutex_);
      auto& st = pointState_[static_cast<size_t>(i)];
      st.active = true;
      st.name = juce::String(ch.name());
      st.azimuthDeg = static_cast<float>(pos.azimuth);
      st.elevationDeg = static_cast<float>(pos.elevation);
      st.distance = static_cast<float>(pos.distance);
      st.gain = static_cast<float>(gain);
    }
    updateConnectorLifecycle(i);  // only live once an EAR channel is also set
    pushPointPropertiesToScene(i);
    pushExportParams(i);
  }
  notifyPointsChangedOnMessageThread();
}

const std::vector<PointEncoderAudioProcessor::LayoutChoice>&
PointEncoderAudioProcessor::getStandardLayoutChoices() {
  // Same list/display names as "LF Decoder" (see that
  // plugin's own getStandardLayoutChoices() for the full source/
  // reasoning - the stock "EAR Monitoring" plugin family, one binary per
  // layout).
  static const std::vector<LayoutChoice> kChoices = {
      {"2.0", "0+2+0"},        {"5.1", "0+5+0"},
      {"5.1+2H", "2+5+0"},     {"5.1+4H", "4+5+0"},
      {"7.2+3H", "3+7+0"},     {"7.1", "0+7+0"},
      {"7.1+2H", "2+7+0"},     {"7.1+4H (7.1.4)", "4+7+0"},
      {"9.1+4H", "4+9+0"},     {"22.2", "9+10+3"},
  };
  return kChoices;
}

void PointEncoderAudioProcessor::loadStandardLayout(int choiceIndex) {
  auto const& choices = getStandardLayoutChoices();
  if (choiceIndex < 0 || choiceIndex >= static_cast<int>(choices.size()))
    return;
  auto const& choice = choices[static_cast<size_t>(choiceIndex)];

  ear::Layout layout;
  if (choice.earLayoutName == "2+7+0") {
    // No direct BS.2051 entry of its own in libear - same special-cased
    // construction "LF Decoder" uses (see that
    // plugin's own setStandardLayout()): first 8 speakers match
    // "4+9+0", last 2 (U+090/U-090) come from "9+10+3".
    auto channels_4_9_0 = ear::getLayout("4+9+0").channels();
    std::vector<ear::Channel> channels_2_7_0(channels_4_9_0.begin(),
                                             channels_4_9_0.begin() + 8);
    auto channels_9_10_3 = ear::getLayout("9+10+3").channels();
    channels_2_7_0.push_back(channels_9_10_3.at(12));
    channels_2_7_0.push_back(channels_9_10_3.at(13));
    layout = ear::Layout{"2+7+0", channels_2_7_0};
  } else {
    layout = ear::getLayout(choice.earLayoutName);
  }

  auto const& channels = layout.channels();
  int n = juce::jmin(static_cast<int>(channels.size()), kMaxPoints);
  for (int i = 0; i < n; ++i) {
    auto const& ch = channels[static_cast<size_t>(i)];
    if (ch.isLfe()) continue;  // no single direction to encode
    auto pos = ch.polarPosition();
    {
      std::lock_guard<std::mutex> lock(pointStateMutex_);
      auto& st = pointState_[static_cast<size_t>(i)];
      st.active = true;
      st.name = juce::String(ch.name());
      st.azimuthDeg = static_cast<float>(pos.azimuth);
      st.elevationDeg = static_cast<float>(pos.elevation);
      st.distance = static_cast<float>(pos.distance);
      st.gain = 1.0f;
    }
    updateConnectorLifecycle(i);  // only live once an EAR channel is also set
    pushPointPropertiesToScene(i);
    pushExportParams(i);
  }
  notifyPointsChangedOnMessageThread();
}

void PointEncoderAudioProcessor::updateTrackProperties(
    const TrackProperties& properties) {
  trackColour_ = properties.colour.isTransparent() ? juce::Colours::orange
                                                   : properties.colour;
  for (int i = 0; i < kMaxPoints; ++i)
    if (pointConnectors_[static_cast<size_t>(i)])
      pointConnectors_[static_cast<size_t>(i)]->setColour(trackColour_.getARGB());
}

void PointEncoderAudioProcessor::notifyPointsChangedOnMessageThread() {
  std::weak_ptr<bool> weakAlive = aliveFlag_;
  MessageManager::callAsync([this, weakAlive]() {
    if (weakAlive.expired()) return;
    if (onPointsChanged) onPointsChanged();
  });
}

void PointEncoderAudioProcessor::getStateInformation(MemoryBlock& destData) {
  std::unique_ptr<XmlElement> xml(new XmlElement("PointEncoderPlugin"));

  auto* pointsXml = xml->createNewChildElement("Points");
  for (int i = 0; i < kMaxPoints; ++i) {
    auto st = getPointState(i);
    auto* p = pointsXml->createNewChildElement("Point");
    p->setAttribute("index", i);
    p->setAttribute("active", st.active);
    p->setAttribute("name", st.name);
    p->setAttribute("input_channel", st.inputChannel);
    p->setAttribute("ear_channel", st.earChannel);
    p->setAttribute("azimuth", static_cast<double>(st.azimuthDeg));
    p->setAttribute("elevation", static_cast<double>(st.elevationDeg));
    p->setAttribute("distance", static_cast<double>(st.distance));
    p->setAttribute("gain", static_cast<double>(st.gain));
    p->setAttribute("width", static_cast<double>(st.width));
    p->setAttribute("height", static_cast<double>(st.height));
    p->setAttribute("depth", static_cast<double>(st.depth));
    p->setAttribute("diffuse", static_cast<double>(st.diffuse));
    if (pointBackends_[static_cast<size_t>(i)])
      p->setAttribute("connection_id",
                      pointBackends_[static_cast<size_t>(i)]
                          ->getConnectionId()
                          .string());
  }

  copyXmlToBinary(*xml, destData);
}

void PointEncoderAudioProcessor::setStateInformation(const void* data,
                                                     int sizeInBytes) {
  std::unique_ptr<XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
  if (!xmlState || !xmlState->hasTagName("PointEncoderPlugin")) return;

  if (auto* pointsXml = xmlState->getChildByName("Points")) {
    for (auto* p : pointsXml->getChildWithTagNameIterator("Point")) {
      int i = p->getIntAttribute("index", -1);
      if (i < 0 || i >= kMaxPoints) continue;

      {
        std::lock_guard<std::mutex> lock(pointStateMutex_);
        auto& st = pointState_[static_cast<size_t>(i)];
        st.name = p->getStringAttribute(
            "name", "Point " + juce::String(i + 1));
        st.inputChannel = p->getIntAttribute("input_channel", i);
        st.earChannel = p->getIntAttribute("ear_channel", -1);
        st.azimuthDeg =
            static_cast<float>(p->getDoubleAttribute("azimuth", 0.0));
        st.elevationDeg =
            static_cast<float>(p->getDoubleAttribute("elevation", 0.0));
        st.distance =
            static_cast<float>(p->getDoubleAttribute("distance", 1.0));
        st.gain = static_cast<float>(p->getDoubleAttribute("gain", 1.0));
        st.width = static_cast<float>(p->getDoubleAttribute("width", 0.0));
        st.height = static_cast<float>(p->getDoubleAttribute("height", 0.0));
        st.depth = static_cast<float>(p->getDoubleAttribute("depth", 0.0));
        st.diffuse =
            static_cast<float>(p->getDoubleAttribute("diffuse", 0.0));
      }

      bool active = p->getBoolAttribute("active", false);
      {
        std::lock_guard<std::mutex> lock(pointStateMutex_);
        pointState_[static_cast<size_t>(i)].active = active;
      }
      updateConnectorLifecycle(i);  // only live once active AND ear channel set
      if (pointBackends_[static_cast<size_t>(i)]) {
        auto idStr = p->getStringAttribute("connection_id", "").toStdString();
        if (!idStr.empty())
          pointBackends_[static_cast<size_t>(i)]->setConnectionId(
              ear::plugin::communication::ConnectionId{idStr});
        pushPointPropertiesToScene(i);
      }
      pushExportParams(i);
    }
  }
  notifyPointsChangedOnMessageThread();
}

void PointEncoderAudioProcessor::sendPointOsc(int pointIndex) {
  std::lock_guard<std::mutex> lock(oscSenderMutex_);
  if (!oscOutputEnabled_) return;
  auto st = getPointState(pointIndex);
  juce::String base = "/point/" + juce::String(pointIndex);
  oscSender_.send(base + "/active", st.active ? 1 : 0);
  if (!st.active) return;
  oscSender_.send(base + "/az", st.azimuthDeg);
  oscSender_.send(base + "/el", st.elevationDeg);
  oscSender_.send(base + "/dist", st.distance);
}

void PointEncoderAudioProcessor::reconnectOscOutputLocked() {
  oscSender_.disconnect();
  if (oscOutputEnabled_) oscSender_.connect(oscOutputIp_, oscOutputPort_);
}

void PointEncoderAudioProcessor::setOscOutputEnabled(bool enabled) {
  std::lock_guard<std::mutex> lock(oscSenderMutex_);
  oscOutputEnabled_ = enabled;
  reconnectOscOutputLocked();
}

void PointEncoderAudioProcessor::setOscOutputIp(const juce::String& ip) {
  std::lock_guard<std::mutex> lock(oscSenderMutex_);
  oscOutputIp_ = ip;
  reconnectOscOutputLocked();
}

void PointEncoderAudioProcessor::setOscOutputPort(int port) {
  std::lock_guard<std::mutex> lock(oscSenderMutex_);
  oscOutputPort_ = port;
  reconnectOscOutputLocked();
}

void PointEncoderAudioProcessor::setOscInputEnabled(bool enabled) {
  if (enabled == oscInputEnabled_) return;
  if (oscInputEnabled_)
    OscPortRegistry::getInstance().unsubscribe(oscInputPort_, &oscInputSubscriber_);
  oscInputEnabled_ = enabled;
  if (oscInputEnabled_)
    OscPortRegistry::getInstance().subscribe(oscInputPort_, &oscInputSubscriber_);
}

void PointEncoderAudioProcessor::setOscInputPort(int port) {
  if (port == oscInputPort_) return;
  bool wasEnabled = oscInputEnabled_;
  if (wasEnabled)
    OscPortRegistry::getInstance().unsubscribe(oscInputPort_, &oscInputSubscriber_);
  oscInputPort_ = port;
  if (wasEnabled)
    OscPortRegistry::getInstance().subscribe(oscInputPort_, &oscInputSubscriber_);
}

// Address scheme shared by all 4 "EAR Suite extras" plugins' OSC input/
// output: /point/<index>/az, /el, /dist (float, same ranges as the UI
// sliders) and /point/<index>/active (int 0/1) - <index> is the point's
// own slot number (0-based), matching the editor's own point-list order.
void PointEncoderAudioProcessor::handleOscMessage(const juce::OSCMessage& message) {
  auto addr = message.getAddressPattern().toString();
  if (!addr.startsWith("/point/")) return;
  auto rest = addr.substring(7);
  int slash = rest.indexOf("/");
  if (slash < 0) return;
  int idx = rest.substring(0, slash).getIntValue();
  juce::String field = rest.substring(slash + 1);
  if (idx < 0 || idx >= kMaxPoints) return;
  if (message.size() < 1) return;
  auto const& arg = message[0];

  if (field == "az" && arg.isFloat32()) {
    auto st = getPointState(idx);
    setPointPosition(idx, arg.getFloat32(), st.elevationDeg, st.distance);
  } else if (field == "el" && arg.isFloat32()) {
    auto st = getPointState(idx);
    setPointPosition(idx, st.azimuthDeg, arg.getFloat32(), st.distance);
  } else if (field == "dist" && arg.isFloat32()) {
    auto st = getPointState(idx);
    setPointPosition(idx, st.azimuthDeg, st.elevationDeg, arg.getFloat32());
  } else if (field == "active") {
    bool active = arg.isInt32() ? (arg.getInt32() != 0)
                                 : (arg.isFloat32() && arg.getFloat32() > 0.5f);
    setPointActive(idx, active);
  }
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
  return new PointEncoderAudioProcessor();
}
