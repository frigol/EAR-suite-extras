#include "reverb_plugin_processor.hpp"
#include "reverb_plugin_editor.hpp"

#include "communication/monitoring_metadata_receiver.hpp"
#include "detail/constants.hpp"
#include "reaper_integration.hpp"

#include <ear/bs2051.hpp>

#include <algorithm>
#include <cmath>

using namespace ear::plugin;
using std::placeholders::_1;
using std::placeholders::_2;

namespace {
// Conventional speaker-letter label (L/R/C/LS/RS/CB for the mid-layer
// hexagon, LTF/RTF/LTR/RTR for the top layer) derived from nominal
// azimuth/elevation, so each point reads like a normal channel name
// rather than a raw angle. CB ("centre back") is the mid hexagon's own
// 6th vertex, directly opposite C - the two points BS.2051's own 5-point
// "4+5+0" mid ring doesn't have an equivalent for.
juce::String channelLetterLabel(float azimuthDeg, float elevationDeg) {
  float absAz = std::abs(azimuthDeg);
  bool top = elevationDeg > 1.0f;
  if (absAz < 1.0f) return top ? "TC" : "C";     // exact front
  if (absAz > 179.0f) return top ? "TCB" : "CB";  // exact back
  bool front = absAz <= 90.0f;
  juce::String side = azimuthDeg > 0.0f ? "L" : "R";
  if (!top) return side + (front ? "" : "S");
  return side + (front ? "TF" : "TR");
}

// How concentrated a point source's early reflections are around its own
// direction: lower sharpness (large object) spreads the weight over most
// of the 9 points almost evenly, higher sharpness (small/point object)
// concentrates it tightly around the nearest point(s).
float sharpnessForSize(float width, float height) {
  float sizeMetric = juce::jlimit(0.0f, 1.0f, 0.5f * (width + height));
  return juce::jmap(sizeMetric, 0.0f, 1.0f, 7.0f, 1.2f);
}

// Closer sources throw stronger, more localized early reflections;
// farther sources send proportionally more into the shared diffuse tail.
float earlyLevelForDistance(float distance) {
  return 1.0f / juce::jmax(1.0f, distance);
}
float diffuseSendForDistance(float distance) {
  return 1.0f - std::exp(-juce::jmax(0.0f, distance) / 3.0f);
}
}  // namespace

ReverbAudioProcessor::ReverbAudioProcessor()
    : AudioProcessor(
         BusesProperties()
             .withInput("Input", AudioChannelSet::discreteChannels(kMaxInputs), true)
             .withOutput("Output", AudioChannelSet::discreteChannels(kNumPoints), true)) {
  buildPointLayout();

  pointRouting_.fill(-1);
  pointActive_.fill(true);  // every point is a live Scene Object by default

  // These must stay the ONLY addParameter() calls in this processor,
  // added in point order 0..9 - reaper-adm-extension reads each by raw
  // VST3 parameter INDEX (see exportaction_admsource-earvst.h), not by
  // name, so inserting any other parameter before or between these would
  // silently break ADM export again.
  for (int i = 0; i < kNumPoints; ++i) {
    addParameter(inputInstanceIdParams_[static_cast<size_t>(i)] =
                     new ReadOnlyAudioParameterInt(
                         "reverbInstanceId" + juce::String(i),
                         "Auto-set ID for point " + juce::String(i),
                         0, 65535, 0));
  }
  // Mirrors of each point's position/gain, read by reaper-adm-extension
  // for ADM export (see PointExportParams in the header) - ranges match
  // the stock EAR Object plugin's own parameters exactly (azimuth/
  // elevation in degrees, gain in dB, the rest 0..1 fractions), since
  // the exporter's value-reverse-mapping is shared with that plugin.
  // These 8*kNumPoints parameters MUST stay contiguous and in this exact
  // point/field order right after the instance-id block above - see
  // exportaction_admsource-earvst.h's reverb point-parameter layout.
  for (int i = 0; i < kNumPoints; ++i) {
    auto& ep = pointExportParams_[static_cast<size_t>(i)];
    juce::String n(i);
    addParameter(ep.azimuth = new juce::AudioParameterFloat(
                     "revAz" + n, "Point " + n + " Azimuth",
                     juce::NormalisableRange<float>{-180.f, 180.f}, 0.f));
    addParameter(ep.elevation = new juce::AudioParameterFloat(
                     "revEl" + n, "Point " + n + " Elevation",
                     juce::NormalisableRange<float>{-90.f, 90.f}, 0.f));
    addParameter(ep.distance = new juce::AudioParameterFloat(
                     "revDist" + n, "Point " + n + " Distance", 0.f, 1.f, 1.f));
    addParameter(ep.gain = new juce::AudioParameterFloat(
                     "revGain" + n, "Point " + n + " Gain",
                     juce::NormalisableRange<float>{-100.f, 6.f}, 0.f));
    addParameter(ep.width = new juce::AudioParameterFloat(
                     "revWidth" + n, "Point " + n + " Width", 0.f, 1.f, 0.f));
    addParameter(ep.height = new juce::AudioParameterFloat(
                     "revHeight" + n, "Point " + n + " Height", 0.f, 1.f, 0.f));
    addParameter(ep.depth = new juce::AudioParameterFloat(
                     "revDepth" + n, "Point " + n + " Depth", 0.f, 1.f, 0.f));
    addParameter(ep.diffuse = new juce::AudioParameterFloat(
                     "revDiffuse" + n, "Point " + n + " Diffuse", 0.f, 1.f, 0.f));
  }

  for (int i = 0; i < kNumPoints; ++i) {
    pointParams_[static_cast<size_t>(i)].azimuthDeg =
        pointInfo_[i].nominalAzimuthDeg;
    pointParams_[static_cast<size_t>(i)].elevationDeg =
        pointInfo_[i].nominalElevationDeg;

    if (pointActive_[static_cast<size_t>(i)]) buildPointConnector(i);
    pushExportParams(i);

    earlyReflections_[static_cast<size_t>(i)].prepare(48000.0, i);
  }

  sceneControlConnection_.onConnectionEstablished(std::bind(
      &ReverbAudioProcessor::onSceneControlConnection, this, _1, _2));
  sceneControlConnection_.onConnectionLost(
      std::bind(&ReverbAudioProcessor::onSceneControlConnectionLost, this));
  sceneControlConnection_.start(
      ear::plugin::detail::SCENE_MASTER_CONTROL_ENDPOINT);
}

void ReverbAudioProcessor::setIHostApplication(Steinberg::FUnknown* unknown) {
  reaperHost_ = dynamic_cast<IReaperHostApplication*>(unknown);
  VST3ClientExtensions::setIHostApplication(unknown);
  if (!reaperHost_) return;

  // Requested unconditionally for every point regardless of active state -
  // cheap (just a counter from the host extension), and means later
  // reactivating a point never needs to revisit this step.
  for (int i = 0; i < kNumPoints; ++i) {
    int id = static_cast<int>(requestInstanceIdFromExtension(reaperHost_));
    inputInstanceIdParams_[static_cast<size_t>(i)]->internalSetIntAndNotifyHost(id);
    if (pointConnectors_[static_cast<size_t>(i)])
      pointConnectors_[static_cast<size_t>(i)]->setInputInstanceId(id);
  }
}

ReverbAudioProcessor::~ReverbAudioProcessor() {
  *aliveFlag_ = false;
  if (oscInputEnabled_)
    ear::plugin::OscPortRegistry::getInstance().unsubscribe(oscInputPort_, &oscInputSubscriber_);
  if (sceneMetadataReceiver_) {
    sceneMetadataReceiver_->shutdown();
  }
  sceneControlConnection_.onConnectionLost(nullptr);
  sceneControlConnection_.onConnectionEstablished(nullptr);
}

void ReverbAudioProcessor::buildPointConnector(int pointIndex) {
  auto idx = static_cast<size_t>(pointIndex);
  if (pointConnectors_[idx]) return;  // already live
  pointConnectors_[idx] = std::make_unique<ReverbPointConnector>();
  pointBackends_[idx] = std::make_unique<ObjectBackend>(pointConnectors_[idx].get());
  pointConnectors_[idx]->setName(getPointDisplayName(pointIndex).toStdString());
  pointConnectors_[idx]->setColour(trackColour_.getARGB());
  pointConnectors_[idx]->setRouting(pointRouting_[idx]);
  auto const& params = pointParams_[idx];
  pointConnectors_[idx]->setPosition(params.azimuthDeg, params.elevationDeg,
                                     params.distance);
  pointConnectors_[idx]->setGain(params.gain);
  pointConnectors_[idx]->setWidth(params.width);
  pointConnectors_[idx]->setHeight(params.height);
  pointConnectors_[idx]->setDepth(params.depth);
  pointConnectors_[idx]->setDiffuse(params.diffuse);
  if (reaperHost_)
    pointConnectors_[idx]->setInputInstanceId(
        inputInstanceIdParams_[idx]->get());
}

void ReverbAudioProcessor::teardownPointConnector(int pointIndex) {
  auto idx = static_cast<size_t>(pointIndex);
  pointBackends_[idx].reset();
  pointConnectors_[idx].reset();
}

void ReverbAudioProcessor::setPointActive(int pointIndex, bool active) {
  if (pointIndex < 0 || pointIndex >= kNumPoints) return;
  auto idx = static_cast<size_t>(pointIndex);
  pointActive_[idx] = active;
  if (active)
    buildPointConnector(pointIndex);
  else
    teardownPointConnector(pointIndex);
  sendPointOsc(pointIndex);
}

bool ReverbAudioProcessor::getPointActive(int pointIndex) const {
  if (pointIndex < 0 || pointIndex >= kNumPoints) return false;
  return pointActive_[static_cast<size_t>(pointIndex)];
}

void ReverbAudioProcessor::buildPointLayout() {
  // Fixed azimuth/elevation for all 10 points, in this exact channel
  // order: L, R, C, LS, RS, CB, LTF, RTF, LTR, RTR - this specific order
  // was requested explicitly (matters for routing/track layout in
  // REAPER), not just whatever order the positions happen to be most
  // convenient to compute in.
  //
  // Mid layer (L/R/C/LS/RS/CB): a regular hexagon, vertex at front, every
  // 60 degrees - rather than ITU-R BS.2051's own asymmetric 5-point
  // "4+5+0" mid ring, this reverb's own "4+6+0". Requested specifically
  // to sidestep a couple of REAPER-specific routing quirks around odd/
  // asymmetric bus widths, and so a 10-wide output bus has no channel
  // left genuinely unused (a plain "4+5+0" only fills 9 of 10).
  //
  // Top layer (LTF/RTF/LTR/RTR): unchanged - the exact 4 BS.2051 "4+5+0"
  // top-layer corner angles (U+030/U-030/U+110/U-110), confirmed earlier
  // against libear's own layout data.
  struct Pos {
    float az;
    float el;
  };
  constexpr Pos kPositions[kNumPoints] = {
      {60.0f, 0.0f},     // L
      {-60.0f, 0.0f},    // R
      {0.0f, 0.0f},      // C
      {120.0f, 0.0f},    // LS
      {-120.0f, 0.0f},   // RS
      {180.0f, 0.0f},    // CB
      {30.0f, 30.0f},    // LTF
      {-30.0f, 30.0f},   // RTF
      {110.0f, 30.0f},   // LTR
      {-110.0f, 30.0f},  // RTR
  };
  for (int i = 0; i < kNumPoints; ++i) {
    pointInfo_[i].name = channelLetterLabel(kPositions[i].az, kPositions[i].el);
    pointInfo_[i].nominalAzimuthDeg = kPositions[i].az;
    pointInfo_[i].nominalElevationDeg = kPositions[i].el;
  }
}

void ReverbAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
  samplerate_ = static_cast<int>(sampleRate);

  for (int i = 0; i < kNumPoints; ++i) {
    earlyReflections_[static_cast<size_t>(i)].prepare(sampleRate, i);
    earlyFeedBuffer_[static_cast<size_t>(i)].assign(
        static_cast<size_t>(samplesPerBlock), 0.0f);
    earlyPreDelay_[static_cast<size_t>(i)].prepare(sampleRate, 200.0f);
  }
  diffuseFeedBuffer_.assign(static_cast<size_t>(samplesPerBlock), 0.0f);
  directMixBuffer_.assign(static_cast<size_t>(samplesPerBlock), 0.0f);
  diffusePreDelay_.prepare(sampleRate, 200.0f);

  std::lock_guard<std::mutex> lock(diffuseNetworkMutex_);
  for (auto* engine : {static_cast<reverbdsp::TailEngine*>(&fdnTailEngine_),
                       static_cast<reverbdsp::TailEngine*>(&mverbTailEngine_)}) {
    engine->prepare(sampleRate);
    engine->setDecaySeconds(dspParams_.decaySeconds);
    engine->setDamping(dspParams_.damping);
    engine->setRoomSize(dspParams_.roomSize);
    engine->setDensity(dspParams_.density);
    engine->setBandwidth(dspParams_.bandwidth);
  }
  for (auto& line : earlyPreDelay_) line.setDelayMs(dspParams_.preDelayMs);
  diffusePreDelay_.setDelayMs(dspParams_.preDelayMs);
}

void ReverbAudioProcessor::releaseResources() {}

bool ReverbAudioProcessor::isBusesLayoutSupported(
    const BusesLayout& layouts) const {
  if (layouts.inputBuses.size() != 1) return false;
  if (layouts.inputBuses[0] != AudioChannelSet::discreteChannels(kMaxInputs))
    return false;
  if (layouts.outputBuses.size() != 1) return false;
  if (layouts.outputBuses[0] != AudioChannelSet::discreteChannels(kNumPoints))
    return false;
  return true;
}

void ReverbAudioProcessor::processBlock(AudioBuffer<float>& buffer,
                                        MidiBuffer&) {
  const int numSamples = buffer.getNumSamples();

  // Each point's own live direction (defaults to nominal ITU, but follows
  // any user override - see setPointPosition()) drives the DSP's weighting
  // below, so moving a point's Object in Scene also moves where its early
  // reflections concentrate.
  std::array<reverbdsp::Direction, kNumPoints> pointDirection;
  {
    std::lock_guard<std::mutex> lock(pointParamsMutex_);
    for (int p = 0; p < kNumPoints; ++p)
      pointDirection[static_cast<size_t>(p)] = reverbdsp::Direction::fromAzEl(
          pointParams_[static_cast<size_t>(p)].azimuthDeg,
          pointParams_[static_cast<size_t>(p)].elevationDeg);
  }

  // Snapshot the active slots' live position/size once per block (cheap -
  // only a handful of slots are normally active), rather than holding the
  // lock while running the DSP below.
  struct ActiveSource {
    int channel;
    reverbdsp::Direction direction;
    float earlyWeight[kNumPoints];
    float earlyLevel;
    float diffuseSend;
  };
  std::vector<ActiveSource> sources;
  {
    std::lock_guard<std::mutex> lock(inputSlotsMutex_);
    for (int ch = 0; ch < kMaxInputs; ++ch) {
      auto const& slot = inputSlots_[static_cast<size_t>(ch)];
      if (slot.connectionId.empty() || ch >= buffer.getNumChannels())
        continue;

      ActiveSource src;
      src.channel = ch;
      src.direction =
          reverbdsp::Direction::fromAzEl(slot.azimuthDeg, slot.elevationDeg);
      src.earlyLevel = earlyLevelForDistance(slot.distance);
      src.diffuseSend = diffuseSendForDistance(slot.distance);

      float sharpness = sharpnessForSize(slot.width, slot.height);
      float sum = 0.0f;
      for (int p = 0; p < kNumPoints; ++p) {
        float w = reverbdsp::angularWeight(
            src.direction, pointDirection[static_cast<size_t>(p)], sharpness);
        src.earlyWeight[p] = w;
        sum += w;
      }
      if (sum > 1.0e-6f)
        for (int p = 0; p < kNumPoints; ++p) src.earlyWeight[p] /= sum;

      sources.push_back(src);
    }
  }

  // Build each point's direction-weighted early-reflection feed, the one
  // shared diffuse-tail feed, and the plain (unweighted) sum used by the
  // optional Direct path, from the active sources' own channels.
  for (int p = 0; p < kNumPoints; ++p)
    std::fill(earlyFeedBuffer_[static_cast<size_t>(p)].begin(),
              earlyFeedBuffer_[static_cast<size_t>(p)].begin() + numSamples,
              0.0f);
  std::fill(diffuseFeedBuffer_.begin(), diffuseFeedBuffer_.begin() + numSamples,
            0.0f);
  std::fill(directMixBuffer_.begin(), directMixBuffer_.begin() + numSamples,
            0.0f);

  for (auto const& src : sources) {
    const float* in = buffer.getReadPointer(src.channel);
    for (int n = 0; n < numSamples; ++n) {
      diffuseFeedBuffer_[static_cast<size_t>(n)] += src.diffuseSend * in[n];
      directMixBuffer_[static_cast<size_t>(n)] += in[n];
    }
    for (int p = 0; p < kNumPoints; ++p) {
      float gain = src.earlyLevel * src.earlyWeight[p];
      if (gain < 1.0e-5f) continue;
      auto& feed = earlyFeedBuffer_[static_cast<size_t>(p)];
      for (int n = 0; n < numSamples; ++n) feed[static_cast<size_t>(n)] += gain * in[n];
    }
  }

  float earlyOutGain;
  float diffuseOutGain;
  float directOutGain;
  bool earlyEnabled;
  bool diffuseEnabled;
  bool directEnabled;
  {
    std::lock_guard<std::mutex> lock(diffuseNetworkMutex_);
    earlyOutGain = dspParams_.earlyLevel;
    diffuseOutGain = dspParams_.diffuseLevel;
    directOutGain = dspParams_.directLevel;
    earlyEnabled = dspParams_.earlyEnabled;
    diffuseEnabled = dspParams_.diffuseEnabled;
    directEnabled = dspParams_.directEnabled;
    auto& tailEngine = dspParams_.tailEngine == reverbdsp::TailEngineType::MVerbPlate
                           ? static_cast<reverbdsp::TailEngine&>(mverbTailEngine_)
                           : static_cast<reverbdsp::TailEngine&>(fdnTailEngine_);

    // Pre-delay: the gap before reflections/tail start, applied in place
    // to each feed before it reaches its generator.
    diffusePreDelay_.process(diffuseFeedBuffer_.data(),
                             diffuseFeedBuffer_.data(), numSamples);
    for (int p = 0; p < kNumPoints; ++p)
      earlyPreDelay_[static_cast<size_t>(p)].process(
          earlyFeedBuffer_[static_cast<size_t>(p)].data(),
          earlyFeedBuffer_[static_cast<size_t>(p)].data(), numSamples);

    // Only the currently-selected engine actually runs each block - the
    // other stays prepared (see prepareToPlay()) but idle, so switching
    // is instant without either one's state going stale.
    tailEngine.process(diffuseFeedBuffer_.data(), numSamples);

    for (int p = 0; p < kNumPoints && p < buffer.getNumChannels(); ++p) {
      float* out = buffer.getWritePointer(p);
      std::fill(out, out + numSamples, 0.0f);

      if (earlyEnabled) {
        earlyReflections_[static_cast<size_t>(p)].process(
            earlyFeedBuffer_[static_cast<size_t>(p)].data(), out, numSamples);
        for (int n = 0; n < numSamples; ++n) out[n] *= earlyOutGain;
      }

      if (diffuseEnabled) {
        // Small per-point gain variety so the 10 taps of the same diffuse
        // field aren't level-identical, without meaningfully biasing any
        // direction.
        float diffuseVariation = 0.85f + 0.3f * ((p * 37) % 7) / 6.0f;
        for (int n = 0; n < numSamples; ++n)
          out[n] += diffuseOutGain * diffuseVariation * tailEngine.getTap(p, n);
      }

      // Direct: the unprocessed dry mix, off by default - the dry object
      // is already sent to Scene on its own channel, so this exists for
      // auditioning/special routing only (see DspParams::directEnabled).
      if (directEnabled) {
        for (int n = 0; n < numSamples; ++n)
          out[n] += directOutGain * directMixBuffer_[static_cast<size_t>(n)];
      }

      // Safety net: a user reported sporadic massive level spikes
      // (>450dB) with this engine. The feedback matrix math is stable by
      // construction (Householder reflection + per-line gains always <1),
      // so the exact trigger wasn't identified - could be MVerb (third-
      // party) hitting its own edge case at some parameter combination.
      // Hard-clamping output here doesn't fix a root cause that hasn't
      // been pinned down, but it guarantees this specific symptom (a
      // catastrophic level event) can't reach the user's ears or meters
      // regardless of what caused it.
      for (int n = 0; n < numSamples; ++n) {
        float s = out[n];
        if (!std::isfinite(s)) s = 0.0f;
        out[n] = juce::jlimit(-4.0f, 4.0f, s);
      }
    }
  }
  for (int outCh = kNumPoints; outCh < buffer.getNumChannels(); ++outCh) {
    buffer.clear(outCh, 0, numSamples);
  }

  for (auto const& backend : pointBackends_)
    if (backend) backend->triggerMetadataSend();
}

AudioProcessorEditor* ReverbAudioProcessor::createEditor() {
  return new ReverbAudioProcessorEditor(this);
}

const String ReverbAudioProcessor::getName() const { return JucePlugin_Name; }

std::vector<ReverbAudioProcessor::SourceObjectInfo>
ReverbAudioProcessor::getAvailableObjects() const {
  std::vector<SourceObjectInfo> result;
  {
    std::lock_guard<std::mutex> lock(availableObjectsMutex_);
    result = availableObjects_;
  }
  // Mirrors Scene's own "available items" checklist, which never offers an
  // item already on the current programme: an object already feeding one
  // of our input slots is excluded here so it can't be added a second time.
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

std::vector<ReverbAudioProcessor::UnsupportedInputInfo>
ReverbAudioProcessor::getAvailableDirectSpeakers() const {
  std::lock_guard<std::mutex> lock(availableObjectsMutex_);
  return availableDirectSpeakers_;
}

std::vector<ReverbAudioProcessor::UnsupportedInputInfo>
ReverbAudioProcessor::getAvailableHoa() const {
  std::lock_guard<std::mutex> lock(availableObjectsMutex_);
  return availableHoa_;
}

std::vector<ReverbAudioProcessor::ActiveInputSlot>
ReverbAudioProcessor::getActiveInputSlots() const {
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

int ReverbAudioProcessor::addInputSlot(const std::string& connectionId) {
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
      return i;  // already assigned to this slot
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
      }
      return i;
    }
  }
  return -1;  // every slot occupied
}

void ReverbAudioProcessor::setInputSlotSource(int slotIndex,
                                              const std::string& connectionId) {
  if (slotIndex < 0 || slotIndex >= kMaxInputs) return;
  std::lock_guard<std::mutex> lock(inputSlotsMutex_);
  if (connectionId.empty()) {
    inputSlots_[static_cast<size_t>(slotIndex)] = InputSlotInfo{};
  } else {
    inputSlots_[static_cast<size_t>(slotIndex)].connectionId = connectionId;
  }
}

std::string ReverbAudioProcessor::getInputSlotSource(int slotIndex) const {
  if (slotIndex < 0 || slotIndex >= kMaxInputs) return "";
  std::lock_guard<std::mutex> lock(inputSlotsMutex_);
  return inputSlots_[static_cast<size_t>(slotIndex)].connectionId;
}

ReverbAudioProcessor::InputSlotInfo ReverbAudioProcessor::getInputSlotInfo(
    int slotIndex) const {
  if (slotIndex < 0 || slotIndex >= kMaxInputs) return {};
  std::lock_guard<std::mutex> lock(inputSlotsMutex_);
  return inputSlots_[static_cast<size_t>(slotIndex)];
}

void ReverbAudioProcessor::setPointRouting(int pointIndex, int sceneChannel) {
  if (pointIndex < 0 || pointIndex >= kNumPoints) return;
  pointRouting_[static_cast<size_t>(pointIndex)] = sceneChannel;
  if (auto* c = pointConnectors_[static_cast<size_t>(pointIndex)].get())
    c->setRouting(sceneChannel);
}

int ReverbAudioProcessor::getPointRouting(int pointIndex) const {
  if (pointIndex < 0 || pointIndex >= kNumPoints) return -1;
  return pointRouting_[static_cast<size_t>(pointIndex)];
}

ReverbAudioProcessor::PointParams ReverbAudioProcessor::getPointParams(
    int pointIndex) const {
  if (pointIndex < 0 || pointIndex >= kNumPoints) return {};
  std::lock_guard<std::mutex> lock(pointParamsMutex_);
  return pointParams_[static_cast<size_t>(pointIndex)];
}

void ReverbAudioProcessor::pushExportParams(int pointIndex) {
  auto params = getPointParams(pointIndex);
  auto& ep = pointExportParams_[static_cast<size_t>(pointIndex)];
  *ep.azimuth = params.azimuthDeg;
  *ep.elevation = params.elevationDeg;
  *ep.distance = juce::jlimit(0.0f, 1.0f, params.distance);
  // Export's shared Object gain parameter is dB, this plugin's own gain
  // is linear (matches the UI's gainSlider_ range) - convert here, at
  // the one place the two meet.
  *ep.gain = juce::jlimit(-100.0f, 6.0f, juce::Decibels::gainToDecibels(params.gain, -100.0f));
  *ep.width = params.width;
  *ep.height = params.height;
  *ep.depth = params.depth;
  *ep.diffuse = params.diffuse;
}

void ReverbAudioProcessor::setPointPosition(int pointIndex, float azimuthDeg,
                                            float elevationDeg,
                                            float distance) {
  if (pointIndex < 0 || pointIndex >= kNumPoints) return;
  {
    std::lock_guard<std::mutex> lock(pointParamsMutex_);
    auto& params = pointParams_[static_cast<size_t>(pointIndex)];
    params.azimuthDeg = azimuthDeg;
    params.elevationDeg = elevationDeg;
    params.distance = juce::jmax(0.0f, distance);
  }
  if (auto* c = pointConnectors_[static_cast<size_t>(pointIndex)].get())
    c->setPosition(azimuthDeg, elevationDeg, juce::jmax(0.0f, distance));
  pushExportParams(pointIndex);
  sendPointOsc(pointIndex);
}

void ReverbAudioProcessor::setPointGain(int pointIndex, float linearGain) {
  if (pointIndex < 0 || pointIndex >= kNumPoints) return;
  {
    std::lock_guard<std::mutex> lock(pointParamsMutex_);
    pointParams_[static_cast<size_t>(pointIndex)].gain = linearGain;
  }
  if (auto* c = pointConnectors_[static_cast<size_t>(pointIndex)].get())
    c->setGain(linearGain);
  pushExportParams(pointIndex);
}

void ReverbAudioProcessor::setPointWidth(int pointIndex, float width) {
  if (pointIndex < 0 || pointIndex >= kNumPoints) return;
  {
    std::lock_guard<std::mutex> lock(pointParamsMutex_);
    pointParams_[static_cast<size_t>(pointIndex)].width = width;
  }
  if (auto* c = pointConnectors_[static_cast<size_t>(pointIndex)].get())
    c->setWidth(width);
  pushExportParams(pointIndex);
}

void ReverbAudioProcessor::setPointHeight(int pointIndex, float height) {
  if (pointIndex < 0 || pointIndex >= kNumPoints) return;
  {
    std::lock_guard<std::mutex> lock(pointParamsMutex_);
    pointParams_[static_cast<size_t>(pointIndex)].height = height;
  }
  if (auto* c = pointConnectors_[static_cast<size_t>(pointIndex)].get())
    c->setHeight(height);
  pushExportParams(pointIndex);
}

void ReverbAudioProcessor::setPointDepth(int pointIndex, float depth) {
  if (pointIndex < 0 || pointIndex >= kNumPoints) return;
  {
    std::lock_guard<std::mutex> lock(pointParamsMutex_);
    pointParams_[static_cast<size_t>(pointIndex)].depth = depth;
  }
  if (auto* c = pointConnectors_[static_cast<size_t>(pointIndex)].get())
    c->setDepth(depth);
  pushExportParams(pointIndex);
}

void ReverbAudioProcessor::setPointDiffuse(int pointIndex, float diffuse) {
  if (pointIndex < 0 || pointIndex >= kNumPoints) return;
  {
    std::lock_guard<std::mutex> lock(pointParamsMutex_);
    pointParams_[static_cast<size_t>(pointIndex)].diffuse = diffuse;
  }
  if (auto* c = pointConnectors_[static_cast<size_t>(pointIndex)].get())
    c->setDiffuse(diffuse);
  pushExportParams(pointIndex);
}

void ReverbAudioProcessor::resetPointToNominal(int pointIndex) {
  if (pointIndex < 0 || pointIndex >= kNumPoints) return;
  PointParams defaults;
  defaults.azimuthDeg = pointInfo_[static_cast<size_t>(pointIndex)].nominalAzimuthDeg;
  defaults.elevationDeg = pointInfo_[static_cast<size_t>(pointIndex)].nominalElevationDeg;
  {
    std::lock_guard<std::mutex> lock(pointParamsMutex_);
    pointParams_[static_cast<size_t>(pointIndex)] = defaults;
  }
  if (auto* c = pointConnectors_[static_cast<size_t>(pointIndex)].get()) {
    c->setPosition(defaults.azimuthDeg, defaults.elevationDeg, defaults.distance);
    c->setGain(defaults.gain);
    c->setWidth(defaults.width);
    c->setHeight(defaults.height);
    c->setDepth(defaults.depth);
    c->setDiffuse(defaults.diffuse);
  }
  pushExportParams(pointIndex);
}

ReverbAudioProcessor::DspParams ReverbAudioProcessor::getDspParams() const {
  std::lock_guard<std::mutex> lock(diffuseNetworkMutex_);
  return dspParams_;
}

void ReverbAudioProcessor::setDecaySeconds(float seconds) {
  std::lock_guard<std::mutex> lock(diffuseNetworkMutex_);
  dspParams_.decaySeconds = juce::jlimit(0.2f, 10.0f, seconds);
  fdnTailEngine_.setDecaySeconds(dspParams_.decaySeconds);
  mverbTailEngine_.setDecaySeconds(dspParams_.decaySeconds);
}

void ReverbAudioProcessor::setDamping(float damping01) {
  std::lock_guard<std::mutex> lock(diffuseNetworkMutex_);
  dspParams_.damping = juce::jlimit(0.0f, 0.95f, damping01);
  fdnTailEngine_.setDamping(dspParams_.damping);
  mverbTailEngine_.setDamping(dspParams_.damping);
}

void ReverbAudioProcessor::setEarlyLevel(float level) {
  std::lock_guard<std::mutex> lock(diffuseNetworkMutex_);
  dspParams_.earlyLevel = juce::jmax(0.0f, level);
}

void ReverbAudioProcessor::setDiffuseLevel(float level) {
  std::lock_guard<std::mutex> lock(diffuseNetworkMutex_);
  dspParams_.diffuseLevel = juce::jmax(0.0f, level);
}

void ReverbAudioProcessor::setRoomSize(float scale) {
  std::lock_guard<std::mutex> lock(diffuseNetworkMutex_);
  dspParams_.roomSize =
      juce::jlimit(0.5f, reverbdsp::FeedbackDelayNetwork::kMaxRoomSize, scale);
  fdnTailEngine_.setRoomSize(dspParams_.roomSize);
  mverbTailEngine_.setRoomSize(dspParams_.roomSize);
}

void ReverbAudioProcessor::setDensity(float density01) {
  std::lock_guard<std::mutex> lock(diffuseNetworkMutex_);
  dspParams_.density = juce::jlimit(0.0f, 1.0f, density01);
  fdnTailEngine_.setDensity(dspParams_.density);
  mverbTailEngine_.setDensity(dspParams_.density);
}

void ReverbAudioProcessor::setBandwidth(float bandwidth01) {
  std::lock_guard<std::mutex> lock(diffuseNetworkMutex_);
  dspParams_.bandwidth = juce::jlimit(0.0f, 1.0f, bandwidth01);
  fdnTailEngine_.setBandwidth(dspParams_.bandwidth);
  mverbTailEngine_.setBandwidth(dspParams_.bandwidth);
}

void ReverbAudioProcessor::setTailEngine(reverbdsp::TailEngineType type) {
  std::lock_guard<std::mutex> lock(diffuseNetworkMutex_);
  dspParams_.tailEngine = type;
}

void ReverbAudioProcessor::writeDspXml(juce::XmlElement& dspXml) const {
  auto dsp = getDspParams();
  dspXml.setAttribute("decay_seconds", static_cast<double>(dsp.decaySeconds));
  dspXml.setAttribute("damping", static_cast<double>(dsp.damping));
  dspXml.setAttribute("early_level", static_cast<double>(dsp.earlyLevel));
  dspXml.setAttribute("diffuse_level", static_cast<double>(dsp.diffuseLevel));
  dspXml.setAttribute("room_size", static_cast<double>(dsp.roomSize));
  dspXml.setAttribute("density", static_cast<double>(dsp.density));
  dspXml.setAttribute("pre_delay_ms", static_cast<double>(dsp.preDelayMs));
  dspXml.setAttribute("bandwidth", static_cast<double>(dsp.bandwidth));
  dspXml.setAttribute("direct_enabled", dsp.directEnabled);
  dspXml.setAttribute("direct_level", static_cast<double>(dsp.directLevel));
  dspXml.setAttribute("early_enabled", dsp.earlyEnabled);
  dspXml.setAttribute("diffuse_enabled", dsp.diffuseEnabled);
  dspXml.setAttribute("tail_engine", static_cast<int>(dsp.tailEngine));
}

void ReverbAudioProcessor::applyDspXml(const juce::XmlElement& dspXml) {
  setDecaySeconds(
      static_cast<float>(dspXml.getDoubleAttribute("decay_seconds", 1.8)));
  setDamping(static_cast<float>(dspXml.getDoubleAttribute("damping", 0.3)));
  setEarlyLevel(
      static_cast<float>(dspXml.getDoubleAttribute("early_level", 1.0)));
  setDiffuseLevel(
      static_cast<float>(dspXml.getDoubleAttribute("diffuse_level", 0.6)));
  setRoomSize(static_cast<float>(dspXml.getDoubleAttribute("room_size", 1.0)));
  setDensity(static_cast<float>(dspXml.getDoubleAttribute("density", 1.0)));
  setPreDelayMs(
      static_cast<float>(dspXml.getDoubleAttribute("pre_delay_ms", 0.0)));
  setBandwidth(static_cast<float>(dspXml.getDoubleAttribute("bandwidth", 1.0)));
  setDirectEnabled(dspXml.getBoolAttribute("direct_enabled", false));
  setDirectLevel(
      static_cast<float>(dspXml.getDoubleAttribute("direct_level", 1.0)));
  setEarlyEnabled(dspXml.getBoolAttribute("early_enabled", true));
  setDiffuseEnabled(dspXml.getBoolAttribute("diffuse_enabled", true));
  setTailEngine(static_cast<reverbdsp::TailEngineType>(
      dspXml.getIntAttribute("tail_engine", 0)));
}

juce::File ReverbAudioProcessor::getPresetsDirectory() {
  auto dir = juce::File::getSpecialLocation(
                 juce::File::userApplicationDataDirectory)
                 .getChildFile("EAR Production Suite")
                 .getChildFile("Reverb Presets");
  if (!dir.exists()) dir.createDirectory();
  return dir;
}

juce::StringArray ReverbAudioProcessor::getAvailablePresetNames() const {
  juce::StringArray names;
  for (auto const& f : juce::RangedDirectoryIterator(
           getPresetsDirectory(), false, "*.xml", juce::File::findFiles)) {
    names.add(f.getFile().getFileNameWithoutExtension());
  }
  names.sort(true);
  return names;
}

void ReverbAudioProcessor::savePresetAs(const juce::String& name) {
  juce::XmlElement xml("ReverbPreset");
  writeDspXml(xml);
  auto file = getPresetsDirectory().getChildFile(name + ".xml");
  xml.writeTo(file);
}

bool ReverbAudioProcessor::loadPreset(const juce::String& name) {
  auto file = getPresetsDirectory().getChildFile(name + ".xml");
  auto xml = juce::parseXML(file);
  if (!xml) return false;
  applyDspXml(*xml);
  return true;
}

void ReverbAudioProcessor::deletePreset(const juce::String& name) {
  getPresetsDirectory().getChildFile(name + ".xml").deleteFile();
}

void ReverbAudioProcessor::setPreDelayMs(float ms) {
  std::lock_guard<std::mutex> lock(diffuseNetworkMutex_);
  dspParams_.preDelayMs = juce::jlimit(0.0f, 200.0f, ms);
  for (auto& line : earlyPreDelay_) line.setDelayMs(dspParams_.preDelayMs);
  diffusePreDelay_.setDelayMs(dspParams_.preDelayMs);
}

void ReverbAudioProcessor::setDirectEnabled(bool enabled) {
  std::lock_guard<std::mutex> lock(diffuseNetworkMutex_);
  dspParams_.directEnabled = enabled;
}

void ReverbAudioProcessor::setDirectLevel(float level) {
  std::lock_guard<std::mutex> lock(diffuseNetworkMutex_);
  dspParams_.directLevel = juce::jmax(0.0f, level);
}

void ReverbAudioProcessor::setEarlyEnabled(bool enabled) {
  std::lock_guard<std::mutex> lock(diffuseNetworkMutex_);
  dspParams_.earlyEnabled = enabled;
}

void ReverbAudioProcessor::setDiffuseEnabled(bool enabled) {
  std::lock_guard<std::mutex> lock(diffuseNetworkMutex_);
  dspParams_.diffuseEnabled = enabled;
}

void ReverbAudioProcessor::onSceneControlConnection(
    communication::ConnectionId, const std::string& streamEndpoint) {
  sceneMetadataReceiver_ =
      std::make_unique<communication::MonitoringMetadataReceiver>();
  sceneMetadataReceiver_->start(
      streamEndpoint,
      std::bind(&ReverbAudioProcessor::onSceneReceived, this, _1));
}

void ReverbAudioProcessor::onSceneControlConnectionLost() {
  if (sceneMetadataReceiver_) sceneMetadataReceiver_->shutdown();
  {
    std::lock_guard<std::mutex> lock(availableObjectsMutex_);
    availableObjects_.clear();
    availableDirectSpeakers_.clear();
    availableHoa_.clear();
  }
  notifyAvailableObjectsChangedOnMessageThread();
}

void ReverbAudioProcessor::onSceneReceived(
    const ear::plugin::proto::SceneStore& store) {
  // Build the set of this plugin's own 9 point connection ids, so they're
  // excluded from the "objects available to reverberate" list once they
  // themselves become visible to Scene - otherwise, as soon as this
  // plugin's own points connect, they'd appear in their own source list.
  std::vector<std::string> ownIds;
  ownIds.reserve(kNumPoints);
  for (auto const& backend : pointBackends_)
    if (backend) ownIds.push_back(backend->getConnectionId().string());

  std::vector<SourceObjectInfo> found;
  std::vector<UnsupportedInputInfo> foundDirectSpeakers;
  std::vector<UnsupportedInputInfo> foundHoa;
  for (auto const& item : store.all_available_items()) {
    auto const& id = item.connection_id();
    if (std::find(ownIds.begin(), ownIds.end(), id) != ownIds.end()) continue;

    if (item.has_ds_metadata()) {
      foundDirectSpeakers.push_back(
          {id, juce::String(item.name()), item.colour()});
      continue;
    }
    if (item.has_hoa_metadata()) {
      foundHoa.push_back({id, juce::String(item.name()), item.colour()});
      continue;
    }
    if (!item.has_obj_metadata()) continue;  // not an input type we show

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

  // Keep any already-active slot's cached name/colour/position (used by
  // the "active inputs" list and by the DSP in processBlock()) in sync
  // with Scene - this also fills them in for slots restored from session
  // state, which only have a bare connectionId until the first Scene
  // broadcast arrives.
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
          break;
        }
      }
    }
  }

  notifyAvailableObjectsChangedOnMessageThread();
}

juce::String ReverbAudioProcessor::getPointDisplayName(int pointIndex) const {
  if (pointIndex < 0 || pointIndex >= kNumPoints) return {};
  auto const& letter = pointInfo_[static_cast<size_t>(pointIndex)].name;
  return trackName_.isEmpty() ? letter : trackName_ + " " + letter;
}

void ReverbAudioProcessor::pushAllPointPropertiesToScene() {
  for (int i = 0; i < kNumPoints; ++i) {
    auto* c = pointConnectors_[static_cast<size_t>(i)].get();
    if (!c) continue;
    c->setName(getPointDisplayName(i).toStdString());
    c->setColour(trackColour_.getARGB());
  }
}

void ReverbAudioProcessor::updateTrackProperties(
    const TrackProperties& properties) {
  trackName_ = properties.name;
  // Falls back to the same orange already used elsewhere in this plugin's
  // own UI for "this point is routed" when the host hasn't reported a
  // track colour (transparentBlack) - same convention object_frontend_
  // connector.cpp uses for a real Object (EarColours::PrimaryVariant
  // there; a plain colour here to avoid pulling a UI look-and-feel header
  // into the processor).
  trackColour_ = properties.colour.isTransparent() ? juce::Colours::orange
                                                   : properties.colour;
  pushAllPointPropertiesToScene();
  notifyTrackNameChangedOnMessageThread();
}

void ReverbAudioProcessor::notifyTrackNameChangedOnMessageThread() {
  std::weak_ptr<bool> weakAlive = aliveFlag_;
  MessageManager::callAsync([this, weakAlive]() {
    if (weakAlive.expired()) return;
    if (onTrackNameChanged) onTrackNameChanged();
  });
}

void ReverbAudioProcessor::notifyAvailableObjectsChangedOnMessageThread() {
  std::weak_ptr<bool> weakAlive = aliveFlag_;
  MessageManager::callAsync([this, weakAlive]() {
    if (weakAlive.expired()) return;
    if (onAvailableObjectsChanged) onAvailableObjectsChanged();
  });
}

void ReverbAudioProcessor::getStateInformation(MemoryBlock& destData) {
  std::unique_ptr<XmlElement> xml(new XmlElement("ReverbPlugin"));

  auto* pointsXml = xml->createNewChildElement("Points");
  for (int i = 0; i < kNumPoints; ++i) {
    auto* p = pointsXml->createNewChildElement("Point");
    p->setAttribute("index", i);
    p->setAttribute("active", pointActive_[static_cast<size_t>(i)]);
    if (pointBackends_[static_cast<size_t>(i)])
      p->setAttribute("connection_id",
                      pointBackends_[static_cast<size_t>(i)]
                          ->getConnectionId()
                          .string());
    p->setAttribute("routing", pointRouting_[static_cast<size_t>(i)]);

    auto params = getPointParams(i);
    p->setAttribute("azimuth", static_cast<double>(params.azimuthDeg));
    p->setAttribute("elevation", static_cast<double>(params.elevationDeg));
    p->setAttribute("distance", static_cast<double>(params.distance));
    p->setAttribute("gain", static_cast<double>(params.gain));
    p->setAttribute("width", static_cast<double>(params.width));
    p->setAttribute("height", static_cast<double>(params.height));
    p->setAttribute("depth", static_cast<double>(params.depth));
    p->setAttribute("diffuse", static_cast<double>(params.diffuse));
  }

  auto* dspXml = xml->createNewChildElement("Dsp");
  writeDspXml(*dspXml);

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

void ReverbAudioProcessor::setStateInformation(const void* data,
                                               int sizeInBytes) {
  std::unique_ptr<XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
  if (!xmlState || !xmlState->hasTagName("ReverbPlugin")) return;

  if (auto* pointsXml = xmlState->getChildByName("Points")) {
    for (auto* p : pointsXml->getChildWithTagNameIterator("Point")) {
      int i = p->getIntAttribute("index", -1);
      if (i < 0 || i >= kNumPoints) continue;

      bool active = p->getBoolAttribute("active", true);
      pointActive_[static_cast<size_t>(i)] = active;
      if (active)
        buildPointConnector(i);
      else
        teardownPointConnector(i);

      auto idStr = p->getStringAttribute("connection_id", "").toStdString();
      if (!idStr.empty() && pointBackends_[static_cast<size_t>(i)]) {
        pointBackends_[static_cast<size_t>(i)]->setConnectionId(
            communication::ConnectionId{idStr});
      }
      int routing = p->getIntAttribute("routing", -1);
      setPointRouting(i, routing);

      float az = static_cast<float>(p->getDoubleAttribute(
          "azimuth", pointInfo_[static_cast<size_t>(i)].nominalAzimuthDeg));
      float el = static_cast<float>(p->getDoubleAttribute(
          "elevation", pointInfo_[static_cast<size_t>(i)].nominalElevationDeg));
      float dist = static_cast<float>(p->getDoubleAttribute("distance", 1.0));
      setPointPosition(i, az, el, dist);
      setPointGain(i, static_cast<float>(p->getDoubleAttribute("gain", 1.0)));
      setPointWidth(i, static_cast<float>(p->getDoubleAttribute("width", 0.0)));
      setPointHeight(i,
                     static_cast<float>(p->getDoubleAttribute("height", 0.0)));
      setPointDepth(i, static_cast<float>(p->getDoubleAttribute("depth", 0.0)));
      setPointDiffuse(
          i, static_cast<float>(p->getDoubleAttribute("diffuse", 0.0)));
    }
  }

  if (auto* dspXml = xmlState->getChildByName("Dsp")) {
    applyDspXml(*dspXml);
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

void ReverbAudioProcessor::sendPointOsc(int pointIndex) {
  std::lock_guard<std::mutex> lock(oscSenderMutex_);
  if (!oscOutputEnabled_) return;
  bool active = getPointActive(pointIndex);
  auto params = getPointParams(pointIndex);
  juce::String base = "/point/" + juce::String(pointIndex);
  oscSender_.send(base + "/active", active ? 1 : 0);
  if (!active) return;
  oscSender_.send(base + "/az", params.azimuthDeg);
  oscSender_.send(base + "/el", params.elevationDeg);
  oscSender_.send(base + "/dist", params.distance);
}

void ReverbAudioProcessor::reconnectOscOutputLocked() {
  oscSender_.disconnect();
  if (oscOutputEnabled_) oscSender_.connect(oscOutputIp_, oscOutputPort_);
}

void ReverbAudioProcessor::setOscOutputEnabled(bool enabled) {
  std::lock_guard<std::mutex> lock(oscSenderMutex_);
  oscOutputEnabled_ = enabled;
  reconnectOscOutputLocked();
}

void ReverbAudioProcessor::setOscOutputIp(const juce::String& ip) {
  std::lock_guard<std::mutex> lock(oscSenderMutex_);
  oscOutputIp_ = ip;
  reconnectOscOutputLocked();
}

void ReverbAudioProcessor::setOscOutputPort(int port) {
  std::lock_guard<std::mutex> lock(oscSenderMutex_);
  oscOutputPort_ = port;
  reconnectOscOutputLocked();
}

void ReverbAudioProcessor::setOscInputEnabled(bool enabled) {
  if (enabled == oscInputEnabled_) return;
  if (oscInputEnabled_)
    ear::plugin::OscPortRegistry::getInstance().unsubscribe(oscInputPort_, &oscInputSubscriber_);
  oscInputEnabled_ = enabled;
  if (oscInputEnabled_)
    ear::plugin::OscPortRegistry::getInstance().subscribe(oscInputPort_, &oscInputSubscriber_);
}

void ReverbAudioProcessor::setOscInputPort(int port) {
  if (port == oscInputPort_) return;
  bool wasEnabled = oscInputEnabled_;
  if (wasEnabled)
    ear::plugin::OscPortRegistry::getInstance().unsubscribe(oscInputPort_, &oscInputSubscriber_);
  oscInputPort_ = port;
  if (wasEnabled)
    ear::plugin::OscPortRegistry::getInstance().subscribe(oscInputPort_, &oscInputSubscriber_);
}

// Same /point/<index>/{az,el,dist,active} scheme as Point Encoder's own
// (point_encoder_plugin_processor.cpp) - here <index> is one of the 10
// fixed "4+6+0" points (see buildPointLayout()), not a user-activated slot.
void ReverbAudioProcessor::handleOscMessage(const juce::OSCMessage& message) {
  auto addr = message.getAddressPattern().toString();
  if (!addr.startsWith("/point/")) return;
  auto rest = addr.substring(7);
  int slash = rest.indexOf("/");
  if (slash < 0) return;
  int idx = rest.substring(0, slash).getIntValue();
  juce::String field = rest.substring(slash + 1);
  if (idx < 0 || idx >= kNumPoints) return;
  if (message.size() < 1) return;
  auto const& arg = message[0];

  if (field == "az" && arg.isFloat32()) {
    auto params = getPointParams(idx);
    setPointPosition(idx, arg.getFloat32(), params.elevationDeg, params.distance);
  } else if (field == "el" && arg.isFloat32()) {
    auto params = getPointParams(idx);
    setPointPosition(idx, params.azimuthDeg, arg.getFloat32(), params.distance);
  } else if (field == "dist" && arg.isFloat32()) {
    auto params = getPointParams(idx);
    setPointPosition(idx, params.azimuthDeg, params.elevationDeg, arg.getFloat32());
  } else if (field == "active") {
    bool active = arg.isInt32() ? (arg.getInt32() != 0)
                                 : (arg.isFloat32() && arg.getFloat32() > 0.5f);
    setPointActive(idx, active);
  }
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
  return new ReverbAudioProcessor();
}
