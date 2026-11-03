#include "ir_reverb_plugin_processor.hpp"
#include "ir_reverb_plugin_editor.hpp"

#include "communication/monitoring_metadata_receiver.hpp"
#include "detail/constants.hpp"
#include "reaper_integration.hpp"

#include <algorithm>
#include <cmath>

using namespace ear::plugin;
using std::placeholders::_1;
using std::placeholders::_2;

namespace {
// Identical letter convention to the algorithmic Reverb's own - see that
// plugin's channelLetterLabel() for the full explanation.
juce::String channelLetterLabel(float azimuthDeg, float elevationDeg) {
  float absAz = std::abs(azimuthDeg);
  bool top = elevationDeg > 1.0f;
  if (absAz < 1.0f) return top ? "TC" : "C";
  if (absAz > 179.0f) return top ? "TCB" : "CB";
  bool front = absAz <= 90.0f;
  juce::String side = azimuthDeg > 0.0f ? "L" : "R";
  if (!top) return side + (front ? "" : "S");
  return side + (front ? "TF" : "TR");
}

// Same per-source panning shape as the algorithmic Reverb's early
// reflections: how concentrated a source's energy is around its own
// direction when spread across the 10 points.
float sharpnessForSize(float width, float height) {
  float sizeMetric = juce::jlimit(0.0f, 1.0f, 0.5f * (width + height));
  return juce::jmap(sizeMetric, 0.0f, 1.0f, 7.0f, 1.2f);
}

// Distance-based send level. Modelled on the algorithmic Reverb's own
// diffuseSendForDistance, but floored rather than letting it reach 0 at
// distance 0: the algorithmic engine gets away with going to 0 there
// because its *separate* early-reflection path (earlyLevelForDistance)
// stays strong at close range, so there's always something audible. This
// plugin has only one send (one loaded IR, early and late together), so
// the same unfloored law would silence it completely for an object at or
// near distance 0 - a real regression that was fed back as "IR shows no
// signal anymore".
float sendLevelForDistance(float distance) {
  constexpr float kMinSend = 0.2f;
  float d = juce::jmax(0.0f, distance);
  return kMinSend + (1.0f - kMinSend) * (1.0f - std::exp(-d / 3.0f));
}
}  // namespace

IrReverbAudioProcessor::IrReverbAudioProcessor()
    : AudioProcessor(
         BusesProperties()
             .withInput("Input", AudioChannelSet::discreteChannels(kMaxInputs), true)
             .withOutput("Output", AudioChannelSet::discreteChannels(kNumPoints), true)) {
  buildPointLayout();
  pointRouting_.fill(-1);
  pointActive_.fill(true);  // every point is a live Scene Object by default

  // Same mechanism as the algorithmic Reverb's own instance-id + export
  // parameters (see that plugin's constructor comment and PointExportParams
  // in the header): reaper-adm-extension reads these by raw VST3 parameter
  // INDEX, not by name (see exportaction_admsource-earvst.h/.cpp's "IR
  // LF Reverb" recognition and its reverb point-parameter
  // layout). These must stay the ONLY addParameter() calls in this
  // processor, added in point order 0..9, so their indices never shift.
  for (int i = 0; i < kNumPoints; ++i) {
    addParameter(inputInstanceIdParams_[static_cast<size_t>(i)] =
                     new ReadOnlyAudioParameterInt(
                         "irReverbInstanceId" + juce::String(i),
                         "Auto-set ID for point " + juce::String(i), 0, 65535,
                         0));
  }
  for (int i = 0; i < kNumPoints; ++i) {
    auto& ep = pointExportParams_[static_cast<size_t>(i)];
    juce::String n(i);
    addParameter(ep.azimuth = new juce::AudioParameterFloat(
                     "irRevAz" + n, "Point " + n + " Azimuth",
                     juce::NormalisableRange<float>{-180.f, 180.f}, 0.f));
    addParameter(ep.elevation = new juce::AudioParameterFloat(
                     "irRevEl" + n, "Point " + n + " Elevation",
                     juce::NormalisableRange<float>{-90.f, 90.f}, 0.f));
    addParameter(ep.distance = new juce::AudioParameterFloat(
                     "irRevDist" + n, "Point " + n + " Distance", 0.f, 1.f, 1.f));
    addParameter(ep.gain = new juce::AudioParameterFloat(
                     "irRevGain" + n, "Point " + n + " Gain",
                     juce::NormalisableRange<float>{-100.f, 6.f}, 0.f));
    addParameter(ep.width = new juce::AudioParameterFloat(
                     "irRevWidth" + n, "Point " + n + " Width", 0.f, 1.f, 0.f));
    addParameter(ep.height = new juce::AudioParameterFloat(
                     "irRevHeight" + n, "Point " + n + " Height", 0.f, 1.f, 0.f));
    addParameter(ep.depth = new juce::AudioParameterFloat(
                     "irRevDepth" + n, "Point " + n + " Depth", 0.f, 1.f, 0.f));
    addParameter(ep.diffuse = new juce::AudioParameterFloat(
                     "irRevDiffuse" + n, "Point " + n + " Diffuse", 0.f, 1.f, 0.f));
  }

  for (int i = 0; i < kNumPoints; ++i) {
    pointParams_[static_cast<size_t>(i)].azimuthDeg =
        pointInfo_[static_cast<size_t>(i)].nominalAzimuthDeg;
    pointParams_[static_cast<size_t>(i)].elevationDeg =
        pointInfo_[static_cast<size_t>(i)].nominalElevationDeg;

    if (pointActive_[static_cast<size_t>(i)]) buildPointConnector(i);
    pushExportParams(i);
  }

  sceneControlConnection_.onConnectionEstablished(std::bind(
      &IrReverbAudioProcessor::onSceneControlConnection, this, _1, _2));
  sceneControlConnection_.onConnectionLost(
      std::bind(&IrReverbAudioProcessor::onSceneControlConnectionLost, this));
  sceneControlConnection_.start(
      ear::plugin::detail::SCENE_MASTER_CONTROL_ENDPOINT);
}

void IrReverbAudioProcessor::setIHostApplication(Steinberg::FUnknown* unknown) {
  reaperHost_ = dynamic_cast<IReaperHostApplication*>(unknown);
  VST3ClientExtensions::setIHostApplication(unknown);
  if (!reaperHost_) return;

  for (int i = 0; i < kNumPoints; ++i) {
    int id = static_cast<int>(requestInstanceIdFromExtension(reaperHost_));
    inputInstanceIdParams_[static_cast<size_t>(i)]->internalSetIntAndNotifyHost(id);
    if (pointConnectors_[static_cast<size_t>(i)])
      pointConnectors_[static_cast<size_t>(i)]->setInputInstanceId(id);
  }
}

IrReverbAudioProcessor::~IrReverbAudioProcessor() {
  *aliveFlag_ = false;
  if (oscInputEnabled_)
    ear::plugin::OscPortRegistry::getInstance().unsubscribe(oscInputPort_, &oscInputSubscriber_);
  if (sceneMetadataReceiver_) sceneMetadataReceiver_->shutdown();
  sceneControlConnection_.onConnectionLost(nullptr);
  sceneControlConnection_.onConnectionEstablished(nullptr);
}

void IrReverbAudioProcessor::buildPointConnector(int pointIndex) {
  auto idx = static_cast<size_t>(pointIndex);
  if (pointConnectors_[idx]) return;  // already live
  pointConnectors_[idx] = std::make_unique<IrReverbPointConnector>();
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

void IrReverbAudioProcessor::teardownPointConnector(int pointIndex) {
  auto idx = static_cast<size_t>(pointIndex);
  pointBackends_[idx].reset();
  pointConnectors_[idx].reset();
}

void IrReverbAudioProcessor::setPointActive(int pointIndex, bool active) {
  if (pointIndex < 0 || pointIndex >= kNumPoints) return;
  auto idx = static_cast<size_t>(pointIndex);
  pointActive_[idx] = active;
  if (active)
    buildPointConnector(pointIndex);
  else
    teardownPointConnector(pointIndex);
  sendPointOsc(pointIndex);
}

bool IrReverbAudioProcessor::getPointActive(int pointIndex) const {
  if (pointIndex < 0 || pointIndex >= kNumPoints) return false;
  return pointActive_[static_cast<size_t>(pointIndex)];
}

void IrReverbAudioProcessor::buildPointLayout() {
  // Identical "4+6+0" layout, in the same explicit channel order, as the
  // algorithmic Reverb - see that plugin's buildPointLayout() for why.
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

void IrReverbAudioProcessor::prepareToPlay(double sampleRate,
                                           int samplesPerBlock) {
  preparedSampleRate_ = sampleRate;
  preparedBlockSize_ = samplesPerBlock;

  juce::dsp::ProcessSpec spec{sampleRate,
                             static_cast<juce::uint32>(samplesPerBlock), 1};
  std::lock_guard<std::mutex> lock(dspMutex_);
  for (int i = 0; i < kNumPoints; ++i) {
    convolutions_[static_cast<size_t>(i)].prepare(spec);
    preDelay_[static_cast<size_t>(i)].prepare(sampleRate, 500.0f);
    preDelay_[static_cast<size_t>(i)].setDelayMs(preDelayMs_);
    pointFeedBuffer_[static_cast<size_t>(i)].assign(
        static_cast<size_t>(samplesPerBlock), 0.0f);
  }
  if (loadedIrFile_.existsAsFile()) {
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(loadedIrFile_));
    if (reader) decideModeAndLoad(*reader);
  }
}

void IrReverbAudioProcessor::releaseResources() {}

bool IrReverbAudioProcessor::isBusesLayoutSupported(
    const BusesLayout& layouts) const {
  if (layouts.inputBuses.size() != 1) return false;
  if (layouts.inputBuses[0] != AudioChannelSet::discreteChannels(kMaxInputs))
    return false;
  if (layouts.outputBuses.size() != 1) return false;
  if (layouts.outputBuses[0] != AudioChannelSet::discreteChannels(kNumPoints))
    return false;
  return true;
}

void IrReverbAudioProcessor::reloadMonoConvolutions() {
  // Called with dspMutex_ already held, or before audio starts.
  juce::dsp::ProcessSpec spec{preparedSampleRate_,
                             static_cast<juce::uint32>(preparedBlockSize_), 1};
  for (int i = 0; i < kNumPoints; ++i) {
    convolutions_[static_cast<size_t>(i)].prepare(spec);
    juce::AudioBuffer<float> mono(1, shapedIr_.getNumSamples());
    mono.copyFrom(0, 0, shapedIr_, 0, 0, shapedIr_.getNumSamples());
    convolutions_[static_cast<size_t>(i)].loadImpulseResponse(
        std::move(mono), shapedIrSampleRate_, juce::dsp::Convolution::Stereo::no,
        juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
  }
}

void IrReverbAudioProcessor::loadAmbisonicIr(int numChannels) {
  // Called with dspMutex_ already held. Reads from shapedIr_ (already
  // stretched/decayed/damped - see applyIrShaping()).
  ambiConvolutions_.clear();
  ambiPreDelay_.clear();
  ambiFeedBuffer_.assign(
      static_cast<size_t>(numChannels),
      std::vector<float>(static_cast<size_t>(preparedBlockSize_), 0.0f));
  ambiConvolvedBuffer_.assign(
      static_cast<size_t>(numChannels),
      std::vector<float>(static_cast<size_t>(preparedBlockSize_), 0.0f));

  juce::dsp::ProcessSpec spec{preparedSampleRate_,
                             static_cast<juce::uint32>(preparedBlockSize_), 1};
  for (int ch = 0; ch < numChannels; ++ch) {
    auto conv = std::make_unique<juce::dsp::Convolution>();
    conv->prepare(spec);

    juce::AudioBuffer<float> mono(1, shapedIr_.getNumSamples());
    mono.copyFrom(0, 0, shapedIr_, ch, 0, shapedIr_.getNumSamples());
    conv->loadImpulseResponse(
        std::move(mono), shapedIrSampleRate_, juce::dsp::Convolution::Stereo::no,
        juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    ambiConvolutions_.push_back(std::move(conv));

    irreverbdsp::SimpleDelayLine line;
    line.prepare(preparedSampleRate_, 500.0f);
    line.setDelayMs(preDelayMs_);
    ambiPreDelay_.push_back(std::move(line));
  }
}

void IrReverbAudioProcessor::loadStereoIr() {
  // Called with dspMutex_ already held. Reads from shapedIr_.
  juce::dsp::ProcessSpec spec{preparedSampleRate_,
                             static_cast<juce::uint32>(preparedBlockSize_), 1};
  for (int p = 0; p < kNumPoints; ++p) {
    float az = pointInfo_[static_cast<size_t>(p)].nominalAzimuthDeg;
    // C (az ~ 0) takes channel 0, CB (az ~ 180) takes channel 1, everything
    // else follows its own side.
    int channel = (az < -0.5f) ? 1 : (std::abs(az) > 179.5f ? 1 : 0);

    convolutions_[static_cast<size_t>(p)].prepare(spec);
    juce::AudioBuffer<float> mono(1, shapedIr_.getNumSamples());
    mono.copyFrom(0, 0, shapedIr_, channel, 0, shapedIr_.getNumSamples());
    convolutions_[static_cast<size_t>(p)].loadImpulseResponse(
        std::move(mono), shapedIrSampleRate_, juce::dsp::Convolution::Stereo::no,
        juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
  }
}

namespace {
// ITU-R BS.2051 "4+5+0" positions (M+030, M-030, M+000, M+110, M-110,
// U+030, U-030, U+110, U-110 - LFE1 excluded, it has no real spatial
// position), taken from libear's own bs2051_layouts.cpp rather than
// retyped from memory, since this is the geometry PCMA-3D is built on
// (per the user: "4+5+0", missing the centre-back point our own "4+6+0"
// layout has). The channel ORDER here (5 mid, then 4 top, in this exact
// +/- pairing) is this same order libear's own getLayout("4+5+0")
// returns them in - assumed to match a real PCMA-3D file's own channel
// order too, since no independent documentation of that was available to
// verify against; may need correcting once a real file's own
// documentation/metadata can be checked.
struct ArrayPoint {
  float az;
  float el;
};
constexpr ArrayPoint kPcma3dPoints[9] = {
    {30.0f, 0.0f},    // M+030
    {-30.0f, 0.0f},   // M-030
    {0.0f, 0.0f},     // M+000
    {110.0f, 0.0f},   // M+110
    {-110.0f, 0.0f},  // M-110
    {30.0f, 30.0f},   // U+030
    {-30.0f, 30.0f},  // U-030
    {110.0f, 30.0f},  // U+110
    {-110.0f, 30.0f}, // U-110
};
}  // namespace

void IrReverbAudioProcessor::loadArrayIr(int numChannels) {
  // Called with dspMutex_ already held. Reads from shapedIr_.
  arrayConvolutions_.clear();
  arrayPreDelay_.clear();
  arrayFeedBuffer_.assign(
      static_cast<size_t>(numChannels),
      std::vector<float>(static_cast<size_t>(preparedBlockSize_), 0.0f));
  arrayConvolvedBuffer_.assign(
      static_cast<size_t>(numChannels),
      std::vector<float>(static_cast<size_t>(preparedBlockSize_), 0.0f));

  juce::dsp::ProcessSpec spec{preparedSampleRate_,
                             static_cast<juce::uint32>(preparedBlockSize_), 1};
  for (int ch = 0; ch < numChannels; ++ch) {
    auto conv = std::make_unique<juce::dsp::Convolution>();
    conv->prepare(spec);

    juce::AudioBuffer<float> mono(1, shapedIr_.getNumSamples());
    mono.copyFrom(0, 0, shapedIr_, ch, 0, shapedIr_.getNumSamples());
    conv->loadImpulseResponse(
        std::move(mono), shapedIrSampleRate_, juce::dsp::Convolution::Stereo::no,
        juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    arrayConvolutions_.push_back(std::move(conv));

    irreverbdsp::SimpleDelayLine line;
    line.prepare(preparedSampleRate_, 500.0f);
    line.setDelayMs(preDelayMs_);
    arrayPreDelay_.push_back(std::move(line));
  }
}

void IrReverbAudioProcessor::decideModeAndLoad(juce::AudioFormatReader& reader) {
  // Called with dspMutex_ already held. Reads the whole file once into
  // rawIr_ - everything downstream (mode decision's own channel count,
  // and the actual convolutions, via rebuildConvolutionsFromShapedIr())
  // works from that in-memory copy, not the reader, so Stretch/Decay/
  // Damping changes later never need to re-touch the file.
  int numChannels = static_cast<int>(reader.numChannels);
  int numSamples = static_cast<int>(reader.lengthInSamples);
  rawIr_.setSize(numChannels, juce::jmax(1, numSamples), false, false, true);
  reader.read(&rawIr_, 0, numSamples, 0, true, true);
  rawIrSampleRate_ = reader.sampleRate;

  if (preferArrayInterpretation_ && numChannels == kNumPoints) {
    irMode_ = IrMode::Array;
    arrayPresetName_ = "Direct (this plugin's own point order)";
    arrayChannelDirections_.clear();
    for (int i = 0; i < kNumPoints; ++i)
      arrayChannelDirections_.push_back(irreverbdsp::Direction::fromAzEl(
          pointInfo_[static_cast<size_t>(i)].nominalAzimuthDeg,
          pointInfo_[static_cast<size_t>(i)].nominalElevationDeg));
  } else if (preferArrayInterpretation_ && numChannels == 9) {
    irMode_ = IrMode::Array;
    arrayPresetName_ = "PCMA-3D (ITU-R BS.2051 \"4+5+0\")";
    arrayChannelDirections_.clear();
    for (auto const& pt : kPcma3dPoints)
      arrayChannelDirections_.push_back(
          irreverbdsp::Direction::fromAzEl(pt.az, pt.el));
  } else {
    int order = irreverbdsp::ambisonicOrderForChannelCount(numChannels);
    if (order >= 1) {
      irMode_ = IrMode::Ambisonic;
      ambisonicOrder_ = order;
    } else if (numChannels == 2) {
      irMode_ = IrMode::Stereo;
      ambisonicOrder_ = 0;
    } else {
      irMode_ = IrMode::Mono;
      ambisonicOrder_ = 0;
    }
  }

  rebuildConvolutionsFromShapedIr();
}

void IrReverbAudioProcessor::rebuildConvolutionsFromShapedIr() {
  // Called with dspMutex_ already held.
  if (rawIr_.getNumSamples() == 0) return;
  applyIrShaping();
  int numChannels = rawIr_.getNumChannels();
  switch (irMode_) {
    case IrMode::Array:
      loadArrayIr(numChannels);
      break;
    case IrMode::Ambisonic:
      loadAmbisonicIr(numChannels);
      break;
    case IrMode::Stereo:
      loadStereoIr();
      break;
    case IrMode::Mono:
      reloadMonoConvolutions();
      break;
  }
}

void IrReverbAudioProcessor::applyIrShaping() {
  // Called with dspMutex_ already held.
  int numCh = rawIr_.getNumChannels();
  int srcLen = rawIr_.getNumSamples();
  double sr = rawIrSampleRate_ > 0.0 ? rawIrSampleRate_ : preparedSampleRate_;

  // --- Stretch: resample by reading the source at 1/stretch speed, so
  // stretch > 1 genuinely lengthens the buffer (and darkens its spectral
  // content, same trade-off as any simple resample-based stretch - see
  // IrShapeParams::stretch's own comment).
  float stretch = juce::jlimit(0.25f, 4.0f, irShape_.stretch);
  int dstLen = juce::jmax(1, static_cast<int>(std::lround(srcLen * stretch)));
  shapedIr_.setSize(numCh, dstLen, false, false, true);
  for (int ch = 0; ch < numCh; ++ch) {
    const float* src = rawIr_.getReadPointer(ch);
    float* dst = shapedIr_.getWritePointer(ch);
    for (int n = 0; n < dstLen; ++n) {
      float srcPosF = static_cast<float>(n) / stretch;
      int i0 = static_cast<int>(srcPosF);
      float frac = srcPosF - static_cast<float>(i0);
      float s0 = (i0 >= 0 && i0 < srcLen) ? src[i0] : 0.0f;
      float s1 = (i0 + 1 >= 0 && i0 + 1 < srcLen) ? src[i0 + 1] : 0.0f;
      dst[n] = s0 + frac * (s1 - s0);
    }
  }

  // --- Decay: exponential-fade trim, applied after stretch so it always
  // acts on the final (possibly lengthened) duration. 1.0 leaves the
  // buffer untouched (bit-exact) rather than applying a no-op envelope.
  float decay = juce::jlimit(0.05f, 1.0f, irShape_.decay);
  if (decay < 0.999f) {
    double durationSeconds = dstLen / sr;
    double tau = juce::jmax(0.01, decay * durationSeconds * 0.5);
    for (int ch = 0; ch < numCh; ++ch) {
      float* d = shapedIr_.getWritePointer(ch);
      for (int n = 0; n < dstLen; ++n) {
        double t = n / sr;
        d[n] *= static_cast<float>(std::exp(-t / tau));
      }
    }
  }

  // --- Damping: a single one-pole lowpass across the whole shaped IR -
  // same qualitative "HF absorption" role as the algorithmic Reverb's own
  // Damping, just applied once at load time here instead of per-block.
  float damping = juce::jlimit(0.0f, 1.0f, irShape_.damping);
  if (damping > 0.001f) {
    float cutoffHz = juce::jmap(damping, 0.0f, 1.0f, 18000.0f, 800.0f);
    float a = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * cutoffHz /
                              static_cast<float>(sr));
    for (int ch = 0; ch < numCh; ++ch) {
      float* d = shapedIr_.getWritePointer(ch);
      float state = 0.0f;
      for (int n = 0; n < dstLen; ++n) {
        state += a * (d[n] - state);
        d[n] = state;
      }
    }
  }

  shapedIrSampleRate_ = sr;
}

void IrReverbAudioProcessor::setPreferArrayInterpretation(bool prefer) {
  std::lock_guard<std::mutex> lock(dspMutex_);
  preferArrayInterpretation_ = prefer;
  if (loadedIrFile_.existsAsFile()) {
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(loadedIrFile_));
    if (reader) decideModeAndLoad(*reader);
  }
}

void IrReverbAudioProcessor::processBlock(AudioBuffer<float>& buffer,
                                          MidiBuffer&) {
  const int numSamples = buffer.getNumSamples();

  // Snapshot every point's current (possibly user-overridden) position
  // once per block, under one lock, rather than reading pointParams_
  // repeatedly further down (ambisonic/array decode read it once per
  // point; the mono path reads it kNumPoints times too).
  std::array<PointParams, kNumPoints> pointParamsSnapshot;
  {
    std::lock_guard<std::mutex> lock(pointParamsMutex_);
    pointParamsSnapshot = pointParams_;
  }

  std::lock_guard<std::mutex> lock(dspMutex_);
  float wet = wetLevel_;

  if (irMode_ == IrMode::Ambisonic && !ambiConvolutions_.empty()) {
    const int numAmbi = static_cast<int>(ambiConvolutions_.size());

    for (auto& ch : ambiFeedBuffer_)
      std::fill(ch.begin(), ch.begin() + numSamples, 0.0f);

    // Encode: each active source's mono signal, weighted by its own
    // direction's SN3D coefficients, summed into the shared ambisonic bus.
    {
      std::lock_guard<std::mutex> slotLock(inputSlotsMutex_);
      for (int ch = 0; ch < kMaxInputs; ++ch) {
        auto const& slot = inputSlots_[static_cast<size_t>(ch)];
        if (slot.connectionId.empty() || ch >= buffer.getNumChannels())
          continue;
        auto coeffs = irreverbdsp::ambisonicEncodingSN3D(
            ambisonicOrder_, slot.azimuthDeg, slot.elevationDeg);
        float distanceGain = sendLevelForDistance(slot.distance);
        const float* in = buffer.getReadPointer(ch);
        for (int a = 0; a < numAmbi; ++a) {
          float gain = coeffs[static_cast<size_t>(a)] * distanceGain;
          if (std::abs(gain) < 1.0e-6f) continue;
          auto& feed = ambiFeedBuffer_[static_cast<size_t>(a)];
          for (int n = 0; n < numSamples; ++n)
            feed[static_cast<size_t>(n)] += gain * in[n];
        }
      }
    }

    // Pre-delay + convolve: one pass per ACN channel, regardless of how
    // many sources fed into it above.
    for (int a = 0; a < numAmbi; ++a) {
      auto& feed = ambiFeedBuffer_[static_cast<size_t>(a)];
      ambiPreDelay_[static_cast<size_t>(a)].process(feed.data(), feed.data(),
                                                     numSamples);

      float* channels[1] = {feed.data()};
      juce::dsp::AudioBlock<float> block(channels, 1,
                                        static_cast<size_t>(numSamples));
      juce::dsp::ProcessContextReplacing<float> context(block);
      ambiConvolutions_[static_cast<size_t>(a)]->process(context);

      std::copy(feed.begin(), feed.begin() + numSamples,
                ambiConvolvedBuffer_[static_cast<size_t>(a)].begin());
    }

    // Decode: each output point reads the convolved bus back out using
    // the same coefficient formula evaluated at its own direction.
    float norm = wet / static_cast<float>(kNumPoints);
    for (int p = 0; p < kNumPoints && p < buffer.getNumChannels(); ++p) {
      auto decodeCoeffs = irreverbdsp::ambisonicDecodingTapered(
          ambisonicOrder_, pointParamsSnapshot[static_cast<size_t>(p)].azimuthDeg,
          pointParamsSnapshot[static_cast<size_t>(p)].elevationDeg);
      float* out = buffer.getWritePointer(p);
      std::fill(out, out + numSamples, 0.0f);
      for (int a = 0; a < numAmbi; ++a) {
        float g = decodeCoeffs[static_cast<size_t>(a)];
        if (std::abs(g) < 1.0e-6f) continue;
        auto const& conv = ambiConvolvedBuffer_[static_cast<size_t>(a)];
        for (int n = 0; n < numSamples; ++n)
          out[n] += g * conv[static_cast<size_t>(n)];
      }
      for (int n = 0; n < numSamples; ++n) out[n] *= norm;
    }
  } else if (irMode_ == IrMode::Array && !arrayConvolutions_.empty()) {
    const int numArr = static_cast<int>(arrayConvolutions_.size());

    for (auto& ch : arrayFeedBuffer_)
      std::fill(ch.begin(), ch.begin() + numSamples, 0.0f);

    // Encode: each active source's mono signal, blended across the
    // array's own channel directions by angular closeness, summed into
    // the shared array bus - the array-geometry analogue of the
    // ambisonic path's spherical-harmonic encode above.
    {
      std::lock_guard<std::mutex> slotLock(inputSlotsMutex_);
      for (int ch = 0; ch < kMaxInputs; ++ch) {
        auto const& slot = inputSlots_[static_cast<size_t>(ch)];
        if (slot.connectionId.empty() || ch >= buffer.getNumChannels())
          continue;
        auto direction =
            irreverbdsp::Direction::fromAzEl(slot.azimuthDeg, slot.elevationDeg);
        float sharpness = sharpnessForSize(slot.width, slot.height);
        auto weights = irreverbdsp::directionalBlendWeights(
            direction, arrayChannelDirections_, sharpness, 0.15f);
        float distanceGain = sendLevelForDistance(slot.distance);
        const float* in = buffer.getReadPointer(ch);
        for (int a = 0; a < numArr; ++a) {
          float gain = weights[static_cast<size_t>(a)] * distanceGain;
          if (gain < 1.0e-6f) continue;
          auto& feed = arrayFeedBuffer_[static_cast<size_t>(a)];
          for (int n = 0; n < numSamples; ++n)
            feed[static_cast<size_t>(n)] += gain * in[n];
        }
      }
    }

    // Pre-delay + convolve: one pass per array channel, regardless of how
    // many sources fed into it above.
    for (int a = 0; a < numArr; ++a) {
      auto& feed = arrayFeedBuffer_[static_cast<size_t>(a)];
      arrayPreDelay_[static_cast<size_t>(a)].process(feed.data(), feed.data(),
                                                      numSamples);

      float* channels[1] = {feed.data()};
      juce::dsp::AudioBlock<float> block(channels, 1,
                                        static_cast<size_t>(numSamples));
      juce::dsp::ProcessContextReplacing<float> context(block);
      arrayConvolutions_[static_cast<size_t>(a)]->process(context);

      std::copy(feed.begin(), feed.begin() + numSamples,
                arrayConvolvedBuffer_[static_cast<size_t>(a)].begin());
    }

    // Decode: each output point reads its own blended share back out of
    // the convolved array channels.
    float norm = wet;
    for (int p = 0; p < kNumPoints && p < buffer.getNumChannels(); ++p) {
      auto pointDir = irreverbdsp::Direction::fromAzEl(
          pointParamsSnapshot[static_cast<size_t>(p)].azimuthDeg,
          pointParamsSnapshot[static_cast<size_t>(p)].elevationDeg);
      auto weights = irreverbdsp::directionalBlendWeights(
          pointDir, arrayChannelDirections_, 3.0f, 0.15f);
      float* out = buffer.getWritePointer(p);
      std::fill(out, out + numSamples, 0.0f);
      for (int a = 0; a < numArr; ++a) {
        float g = weights[static_cast<size_t>(a)];
        if (g < 1.0e-6f) continue;
        auto const& conv = arrayConvolvedBuffer_[static_cast<size_t>(a)];
        for (int n = 0; n < numSamples; ++n)
          out[n] += g * conv[static_cast<size_t>(n)];
      }
      for (int n = 0; n < numSamples; ++n) out[n] *= norm;
    }
  } else {
    // --- Mono path: one convolution per output point, each fed that
    // point's own direction-weighted sum of the active sources. ---
    std::array<irreverbdsp::Direction, kNumPoints> pointDirection;
    for (int p = 0; p < kNumPoints; ++p)
      pointDirection[static_cast<size_t>(p)] = irreverbdsp::Direction::fromAzEl(
          pointParamsSnapshot[static_cast<size_t>(p)].azimuthDeg,
          pointParamsSnapshot[static_cast<size_t>(p)].elevationDeg);

    struct ActiveSource {
      int channel;
      float weight[kNumPoints];
      float distanceGain;
    };
    std::vector<ActiveSource> sources;
    {
      std::lock_guard<std::mutex> slotLock(inputSlotsMutex_);
      for (int ch = 0; ch < kMaxInputs; ++ch) {
        auto const& slot = inputSlots_[static_cast<size_t>(ch)];
        if (slot.connectionId.empty() || ch >= buffer.getNumChannels())
          continue;

        ActiveSource src;
        src.channel = ch;
        src.distanceGain = sendLevelForDistance(slot.distance);
        auto direction =
            irreverbdsp::Direction::fromAzEl(slot.azimuthDeg, slot.elevationDeg);
        float sharpness = sharpnessForSize(slot.width, slot.height);
        float sum = 0.0f;
        for (int p = 0; p < kNumPoints; ++p) {
          // angularWeight() alone reaches an exact 0 at the point
          // diametrically opposite the source (appropriate for the
          // algorithmic Reverb's own early reflections, which are meant
          // to stay concentrated - wrong here, where this weight decides
          // the WHOLE reverb send per point, not just an early-reflection
          // accent: a real room's reverb still reaches every point, just
          // more strongly near the source). Floored so no point goes
          // fully silent.
          constexpr float kDirectionalFloor = 0.15f;
          float w = irreverbdsp::angularWeight(
              direction, pointDirection[static_cast<size_t>(p)], sharpness);
          w = kDirectionalFloor + (1.0f - kDirectionalFloor) * w;
          src.weight[p] = w;
          sum += w;
        }
        if (sum > 1.0e-6f)
          for (int p = 0; p < kNumPoints; ++p) src.weight[p] /= sum;
        sources.push_back(src);
      }
    }

    for (int p = 0; p < kNumPoints; ++p)
      std::fill(pointFeedBuffer_[static_cast<size_t>(p)].begin(),
                pointFeedBuffer_[static_cast<size_t>(p)].begin() + numSamples,
                0.0f);

    for (auto const& src : sources) {
      const float* in = buffer.getReadPointer(src.channel);
      for (int p = 0; p < kNumPoints; ++p) {
        float gain = src.weight[p] * src.distanceGain;
        if (gain < 1.0e-5f) continue;
        auto& feed = pointFeedBuffer_[static_cast<size_t>(p)];
        for (int n = 0; n < numSamples; ++n) feed[static_cast<size_t>(n)] += gain * in[n];
      }
    }

    for (int p = 0; p < kNumPoints && p < buffer.getNumChannels(); ++p) {
      auto& feed = pointFeedBuffer_[static_cast<size_t>(p)];
      preDelay_[static_cast<size_t>(p)].process(feed.data(), feed.data(),
                                                numSamples);

      float* channels[1] = {feed.data()};
      juce::dsp::AudioBlock<float> block(channels, 1,
                                        static_cast<size_t>(numSamples));
      juce::dsp::ProcessContextReplacing<float> context(block);
      convolutions_[static_cast<size_t>(p)].process(context);

      float* out = buffer.getWritePointer(p);
      for (int n = 0; n < numSamples; ++n) out[n] = wet * feed[static_cast<size_t>(n)];
    }
  }

  for (int outCh = kNumPoints; outCh < buffer.getNumChannels(); ++outCh)
    buffer.clear(outCh, 0, numSamples);

  for (auto const& backend : pointBackends_)
    if (backend) backend->triggerMetadataSend();
}

AudioProcessorEditor* IrReverbAudioProcessor::createEditor() {
  return new IrReverbAudioProcessorEditor(this);
}

const String IrReverbAudioProcessor::getName() const { return JucePlugin_Name; }

std::vector<IrReverbAudioProcessor::SourceObjectInfo>
IrReverbAudioProcessor::getAvailableObjects() const {
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

std::vector<IrReverbAudioProcessor::UnsupportedInputInfo>
IrReverbAudioProcessor::getAvailableDirectSpeakers() const {
  std::lock_guard<std::mutex> lock(availableObjectsMutex_);
  return availableDirectSpeakers_;
}

std::vector<IrReverbAudioProcessor::UnsupportedInputInfo>
IrReverbAudioProcessor::getAvailableHoa() const {
  std::lock_guard<std::mutex> lock(availableObjectsMutex_);
  return availableHoa_;
}

std::vector<IrReverbAudioProcessor::ActiveInputSlot>
IrReverbAudioProcessor::getActiveInputSlots() const {
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

int IrReverbAudioProcessor::addInputSlot(const std::string& connectionId) {
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
  for (int i = 0; i < kMaxInputs; ++i)
    if (inputSlots_[static_cast<size_t>(i)].connectionId == connectionId)
      return i;
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
  return -1;
}

void IrReverbAudioProcessor::setInputSlotSource(
    int slotIndex, const std::string& connectionId) {
  if (slotIndex < 0 || slotIndex >= kMaxInputs) return;
  std::lock_guard<std::mutex> lock(inputSlotsMutex_);
  if (connectionId.empty())
    inputSlots_[static_cast<size_t>(slotIndex)] = InputSlotInfo{};
  else
    inputSlots_[static_cast<size_t>(slotIndex)].connectionId = connectionId;
}

std::string IrReverbAudioProcessor::getInputSlotSource(int slotIndex) const {
  if (slotIndex < 0 || slotIndex >= kMaxInputs) return "";
  std::lock_guard<std::mutex> lock(inputSlotsMutex_);
  return inputSlots_[static_cast<size_t>(slotIndex)].connectionId;
}

IrReverbAudioProcessor::InputSlotInfo
IrReverbAudioProcessor::getInputSlotInfo(int slotIndex) const {
  if (slotIndex < 0 || slotIndex >= kMaxInputs) return {};
  std::lock_guard<std::mutex> lock(inputSlotsMutex_);
  return inputSlots_[static_cast<size_t>(slotIndex)];
}

IrReverbAudioProcessor::PointParams IrReverbAudioProcessor::getPointParams(
    int pointIndex) const {
  if (pointIndex < 0 || pointIndex >= kNumPoints) return {};
  std::lock_guard<std::mutex> lock(pointParamsMutex_);
  return pointParams_[static_cast<size_t>(pointIndex)];
}

void IrReverbAudioProcessor::setPointRouting(int pointIndex, int sceneChannel) {
  if (pointIndex < 0 || pointIndex >= kNumPoints) return;
  pointRouting_[static_cast<size_t>(pointIndex)] = sceneChannel;
  if (auto* c = pointConnectors_[static_cast<size_t>(pointIndex)].get())
    c->setRouting(sceneChannel);
}

int IrReverbAudioProcessor::getPointRouting(int pointIndex) const {
  if (pointIndex < 0 || pointIndex >= kNumPoints) return -1;
  return pointRouting_[static_cast<size_t>(pointIndex)];
}

void IrReverbAudioProcessor::pushExportParams(int pointIndex) {
  auto params = getPointParams(pointIndex);
  auto& ep = pointExportParams_[static_cast<size_t>(pointIndex)];
  *ep.azimuth = params.azimuthDeg;
  *ep.elevation = params.elevationDeg;
  *ep.distance = juce::jlimit(0.0f, 1.0f, params.distance);
  // Export's shared Object gain parameter is dB, this plugin's own gain
  // is linear - same conversion as the algorithmic Reverb's own
  // pushExportParams().
  *ep.gain = juce::jlimit(-100.0f, 6.0f, juce::Decibels::gainToDecibels(params.gain, -100.0f));
  *ep.width = params.width;
  *ep.height = params.height;
  *ep.depth = params.depth;
  *ep.diffuse = params.diffuse;
}

void IrReverbAudioProcessor::setPointPosition(int pointIndex, float azimuthDeg,
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

void IrReverbAudioProcessor::setPointGain(int pointIndex, float linearGain) {
  if (pointIndex < 0 || pointIndex >= kNumPoints) return;
  {
    std::lock_guard<std::mutex> lock(pointParamsMutex_);
    pointParams_[static_cast<size_t>(pointIndex)].gain = linearGain;
  }
  if (auto* c = pointConnectors_[static_cast<size_t>(pointIndex)].get())
    c->setGain(linearGain);
  pushExportParams(pointIndex);
}

void IrReverbAudioProcessor::setPointWidth(int pointIndex, float width) {
  if (pointIndex < 0 || pointIndex >= kNumPoints) return;
  {
    std::lock_guard<std::mutex> lock(pointParamsMutex_);
    pointParams_[static_cast<size_t>(pointIndex)].width = width;
  }
  if (auto* c = pointConnectors_[static_cast<size_t>(pointIndex)].get())
    c->setWidth(width);
  pushExportParams(pointIndex);
}

void IrReverbAudioProcessor::setPointHeight(int pointIndex, float height) {
  if (pointIndex < 0 || pointIndex >= kNumPoints) return;
  {
    std::lock_guard<std::mutex> lock(pointParamsMutex_);
    pointParams_[static_cast<size_t>(pointIndex)].height = height;
  }
  if (auto* c = pointConnectors_[static_cast<size_t>(pointIndex)].get())
    c->setHeight(height);
  pushExportParams(pointIndex);
}

void IrReverbAudioProcessor::setPointDepth(int pointIndex, float depth) {
  if (pointIndex < 0 || pointIndex >= kNumPoints) return;
  {
    std::lock_guard<std::mutex> lock(pointParamsMutex_);
    pointParams_[static_cast<size_t>(pointIndex)].depth = depth;
  }
  if (auto* c = pointConnectors_[static_cast<size_t>(pointIndex)].get())
    c->setDepth(depth);
  pushExportParams(pointIndex);
}

void IrReverbAudioProcessor::setPointDiffuse(int pointIndex, float diffuse) {
  if (pointIndex < 0 || pointIndex >= kNumPoints) return;
  {
    std::lock_guard<std::mutex> lock(pointParamsMutex_);
    pointParams_[static_cast<size_t>(pointIndex)].diffuse = diffuse;
  }
  if (auto* c = pointConnectors_[static_cast<size_t>(pointIndex)].get())
    c->setDiffuse(diffuse);
  pushExportParams(pointIndex);
}

void IrReverbAudioProcessor::resetPointToNominal(int pointIndex) {
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

juce::File IrReverbAudioProcessor::getIrDirectory() {
  auto dir = juce::File::getSpecialLocation(
                 juce::File::userApplicationDataDirectory)
                 .getChildFile("EAR Production Suite")
                 .getChildFile("IR Reverb Impulses");
  if (!dir.exists()) dir.createDirectory();
  return dir;
}

juce::StringArray IrReverbAudioProcessor::getAvailableIrNames() const {
  juce::StringArray names;
  for (auto const& f : juce::RangedDirectoryIterator(
           getIrDirectory(), false, "*.wav;*.aif;*.aiff", juce::File::findFiles)) {
    names.add(f.getFile().getFileName());
  }
  names.sort(true);
  return names;
}

juce::File IrReverbAudioProcessor::importAndLoadIr(const juce::File& sourceFile) {
  if (!sourceFile.existsAsFile()) return {};
  auto dest = getIrDirectory().getChildFile(sourceFile.getFileName());
  if (dest != sourceFile) {
    if (!sourceFile.copyFileTo(dest)) return {};
  }
  loadIrByName(dest.getFileName());
  return dest;
}

bool IrReverbAudioProcessor::loadIrByName(const juce::String& name) {
  auto file = getIrDirectory().getChildFile(name);
  if (!file.existsAsFile()) return false;

  juce::AudioFormatManager formatManager;
  formatManager.registerBasicFormats();
  std::unique_ptr<juce::AudioFormatReader> reader(
      formatManager.createReaderFor(file));
  if (!reader) return false;

  std::lock_guard<std::mutex> lock(dspMutex_);
  loadedIrFile_ = file;
  loadedIrName_ = name;
  loadedIrNumChannels_ = static_cast<int>(reader->numChannels);
  decideModeAndLoad(*reader);
  return true;
}

float IrReverbAudioProcessor::getWetLevel() const {
  std::lock_guard<std::mutex> lock(dspMutex_);
  return wetLevel_;
}

void IrReverbAudioProcessor::setWetLevel(float level) {
  std::lock_guard<std::mutex> lock(dspMutex_);
  wetLevel_ = juce::jmax(0.0f, level);
}

float IrReverbAudioProcessor::getPreDelayMs() const {
  std::lock_guard<std::mutex> lock(dspMutex_);
  return preDelayMs_;
}

void IrReverbAudioProcessor::setPreDelayMs(float ms) {
  std::lock_guard<std::mutex> lock(dspMutex_);
  preDelayMs_ = juce::jlimit(0.0f, 500.0f, ms);
  for (auto& line : preDelay_) line.setDelayMs(preDelayMs_);
  for (auto& line : ambiPreDelay_) line.setDelayMs(preDelayMs_);
  for (auto& line : arrayPreDelay_) line.setDelayMs(preDelayMs_);
}

IrReverbAudioProcessor::IrShapeParams
IrReverbAudioProcessor::getIrShapeParams() const {
  std::lock_guard<std::mutex> lock(dspMutex_);
  return irShape_;
}

void IrReverbAudioProcessor::setIrStretch(float stretch) {
  std::lock_guard<std::mutex> lock(dspMutex_);
  irShape_.stretch = juce::jlimit(0.25f, 4.0f, stretch);
  rebuildConvolutionsFromShapedIr();
}

void IrReverbAudioProcessor::setIrDecay(float decay) {
  std::lock_guard<std::mutex> lock(dspMutex_);
  irShape_.decay = juce::jlimit(0.05f, 1.0f, decay);
  rebuildConvolutionsFromShapedIr();
}

void IrReverbAudioProcessor::setIrDamping(float damping) {
  std::lock_guard<std::mutex> lock(dspMutex_);
  irShape_.damping = juce::jlimit(0.0f, 1.0f, damping);
  rebuildConvolutionsFromShapedIr();
}

void IrReverbAudioProcessor::writeDspXml(juce::XmlElement& dspXml) const {
  dspXml.setAttribute("wet_level", static_cast<double>(getWetLevel()));
  dspXml.setAttribute("pre_delay_ms", static_cast<double>(getPreDelayMs()));
  auto shape = getIrShapeParams();
  dspXml.setAttribute("ir_stretch", static_cast<double>(shape.stretch));
  dspXml.setAttribute("ir_decay", static_cast<double>(shape.decay));
  dspXml.setAttribute("ir_damping", static_cast<double>(shape.damping));
}

void IrReverbAudioProcessor::applyDspXml(const juce::XmlElement& dspXml) {
  setWetLevel(static_cast<float>(dspXml.getDoubleAttribute("wet_level", 1.0)));
  setPreDelayMs(
      static_cast<float>(dspXml.getDoubleAttribute("pre_delay_ms", 0.0)));
  setIrStretch(
      static_cast<float>(dspXml.getDoubleAttribute("ir_stretch", 1.0)));
  setIrDecay(static_cast<float>(dspXml.getDoubleAttribute("ir_decay", 1.0)));
  setIrDamping(
      static_cast<float>(dspXml.getDoubleAttribute("ir_damping", 0.0)));
}

juce::File IrReverbAudioProcessor::getPresetsDirectory() {
  auto dir = juce::File::getSpecialLocation(
                 juce::File::userApplicationDataDirectory)
                 .getChildFile("EAR Production Suite")
                 .getChildFile("IR Reverb Presets");
  if (!dir.exists()) dir.createDirectory();
  return dir;
}

juce::StringArray IrReverbAudioProcessor::getAvailablePresetNames() const {
  juce::StringArray names;
  for (auto const& f : juce::RangedDirectoryIterator(
           getPresetsDirectory(), false, "*.xml", juce::File::findFiles)) {
    names.add(f.getFile().getFileNameWithoutExtension());
  }
  names.sort(true);
  return names;
}

void IrReverbAudioProcessor::savePresetAs(const juce::String& name) {
  juce::XmlElement xml("IrReverbPreset");
  writeDspXml(xml);
  auto file = getPresetsDirectory().getChildFile(name + ".xml");
  xml.writeTo(file);
}

bool IrReverbAudioProcessor::loadPreset(const juce::String& name) {
  auto file = getPresetsDirectory().getChildFile(name + ".xml");
  auto xml = juce::parseXML(file);
  if (!xml) return false;
  applyDspXml(*xml);
  return true;
}

void IrReverbAudioProcessor::deletePreset(const juce::String& name) {
  getPresetsDirectory().getChildFile(name + ".xml").deleteFile();
}

void IrReverbAudioProcessor::onSceneControlConnection(
    communication::ConnectionId, const std::string& streamEndpoint) {
  sceneMetadataReceiver_ =
      std::make_unique<communication::MonitoringMetadataReceiver>();
  sceneMetadataReceiver_->start(
      streamEndpoint,
      std::bind(&IrReverbAudioProcessor::onSceneReceived, this, _1));
}

void IrReverbAudioProcessor::onSceneControlConnectionLost() {
  if (sceneMetadataReceiver_) sceneMetadataReceiver_->shutdown();
  {
    std::lock_guard<std::mutex> lock(availableObjectsMutex_);
    availableObjects_.clear();
    availableDirectSpeakers_.clear();
    availableHoa_.clear();
  }
  notifyAvailableObjectsChangedOnMessageThread();
}

void IrReverbAudioProcessor::onSceneReceived(
    const ear::plugin::proto::SceneStore& store) {
  // Build the set of this plugin's own point connection ids, so they're
  // excluded from the "objects available to reverberate" list once they
  // themselves become visible to Scene.
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
          break;
        }
      }
    }
  }

  notifyAvailableObjectsChangedOnMessageThread();
}

juce::String IrReverbAudioProcessor::getPointDisplayName(int pointIndex) const {
  if (pointIndex < 0 || pointIndex >= kNumPoints) return {};
  auto const& letter = pointInfo_[static_cast<size_t>(pointIndex)].name;
  return trackName_.isEmpty() ? letter : trackName_ + " " + letter;
}

void IrReverbAudioProcessor::pushAllPointPropertiesToScene() {
  for (int i = 0; i < kNumPoints; ++i) {
    auto* c = pointConnectors_[static_cast<size_t>(i)].get();
    if (!c) continue;
    c->setName(getPointDisplayName(i).toStdString());
    c->setColour(trackColour_.getARGB());
  }
}

void IrReverbAudioProcessor::updateTrackProperties(
    const TrackProperties& properties) {
  trackName_ = properties.name;
  trackColour_ = properties.colour.isTransparent() ? juce::Colours::orange
                                                   : properties.colour;
  pushAllPointPropertiesToScene();
  notifyTrackNameChangedOnMessageThread();
}

void IrReverbAudioProcessor::notifyTrackNameChangedOnMessageThread() {
  std::weak_ptr<bool> weakAlive = aliveFlag_;
  MessageManager::callAsync([this, weakAlive]() {
    if (weakAlive.expired()) return;
    if (onTrackNameChanged) onTrackNameChanged();
  });
}

void IrReverbAudioProcessor::notifyAvailableObjectsChangedOnMessageThread() {
  std::weak_ptr<bool> weakAlive = aliveFlag_;
  MessageManager::callAsync([this, weakAlive]() {
    if (weakAlive.expired()) return;
    if (onAvailableObjectsChanged) onAvailableObjectsChanged();
  });
}

void IrReverbAudioProcessor::getStateInformation(MemoryBlock& destData) {
  std::unique_ptr<XmlElement> xml(new XmlElement("IrReverbPlugin"));

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
  dspXml->setAttribute("ir_file", loadedIrFile_.getFullPathName());
  dspXml->setAttribute("prefer_array", preferArrayInterpretation_);

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

void IrReverbAudioProcessor::setStateInformation(const void* data,
                                                 int sizeInBytes) {
  std::unique_ptr<XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
  if (!xmlState || !xmlState->hasTagName("IrReverbPlugin")) return;

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
      setPointRouting(i, p->getIntAttribute("routing", -1));

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
    preferArrayInterpretation_ = dspXml->getBoolAttribute("prefer_array", false);
    auto irPath = dspXml->getStringAttribute("ir_file", "");
    if (irPath.isNotEmpty()) {
      juce::File f(irPath);
      if (f.existsAsFile()) loadIrByName(f.getFileName());
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

void IrReverbAudioProcessor::sendPointOsc(int pointIndex) {
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

void IrReverbAudioProcessor::reconnectOscOutputLocked() {
  oscSender_.disconnect();
  if (oscOutputEnabled_) oscSender_.connect(oscOutputIp_, oscOutputPort_);
}

void IrReverbAudioProcessor::setOscOutputEnabled(bool enabled) {
  std::lock_guard<std::mutex> lock(oscSenderMutex_);
  oscOutputEnabled_ = enabled;
  reconnectOscOutputLocked();
}

void IrReverbAudioProcessor::setOscOutputIp(const juce::String& ip) {
  std::lock_guard<std::mutex> lock(oscSenderMutex_);
  oscOutputIp_ = ip;
  reconnectOscOutputLocked();
}

void IrReverbAudioProcessor::setOscOutputPort(int port) {
  std::lock_guard<std::mutex> lock(oscSenderMutex_);
  oscOutputPort_ = port;
  reconnectOscOutputLocked();
}

void IrReverbAudioProcessor::setOscInputEnabled(bool enabled) {
  if (enabled == oscInputEnabled_) return;
  if (oscInputEnabled_)
    ear::plugin::OscPortRegistry::getInstance().unsubscribe(oscInputPort_, &oscInputSubscriber_);
  oscInputEnabled_ = enabled;
  if (oscInputEnabled_)
    ear::plugin::OscPortRegistry::getInstance().subscribe(oscInputPort_, &oscInputSubscriber_);
}

void IrReverbAudioProcessor::setOscInputPort(int port) {
  if (port == oscInputPort_) return;
  bool wasEnabled = oscInputEnabled_;
  if (wasEnabled)
    ear::plugin::OscPortRegistry::getInstance().unsubscribe(oscInputPort_, &oscInputSubscriber_);
  oscInputPort_ = port;
  if (wasEnabled)
    ear::plugin::OscPortRegistry::getInstance().subscribe(oscInputPort_, &oscInputSubscriber_);
}

void IrReverbAudioProcessor::handleOscMessage(const juce::OSCMessage& message) {
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
  return new IrReverbAudioProcessor();
}
