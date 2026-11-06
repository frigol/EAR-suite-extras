#include "object_stereo_plugin_processor.hpp"

#include <cmath>

#include "components/look_and_feel/colours.hpp"
#include "object_backend.hpp"
#include "object_stereo_frontend_connector.hpp"
#include "object_stereo_plugin_editor.hpp"
#include "reaper_integration.hpp"

using namespace ear::plugin;

namespace {
// Wraps into (-180, 180].
float normaliseAzimuthDeg(float deg) {
  deg = std::fmod(deg + 180.0f, 360.0f);
  if (deg < 0.0f) deg += 360.0f;
  return deg - 180.0f;
}

String describeOscMessage(const OSCMessage& message) {
  String text = message.getAddressPattern().toString();
  for (auto const& arg : message) {
    text << " ";
    if (arg.isFloat32()) text << arg.getFloat32();
    else if (arg.isInt32()) text << arg.getInt32();
    else if (arg.isString()) text << arg.getString();
    else text << "?";
  }
  return text;
}

// Same ControlGRIS2 DOME-mode pad convention as "LF Object OSC"'s own
// applyCartesian() - see that plugin's long comment for the full derivation.
void applyCartesian(ObjectStereoAudioProcessor* p, float x, float y, float z) {
  float dx = x - 0.5f;
  float dy = y - 0.5f;
  float azDegCg = radiansToDegrees(std::atan2(dx, dy));
  float elDeg = jlimit(-90.0f, 90.0f, z * 90.0f);
  p->setAzimuth(normaliseAzimuthDeg(-azDegCg));
  p->setElevation(elDeg);
  p->setDistance(1.0f);
}

// CUBE-mode equivalent - see "LF Object OSC"'s applyCartesianCube().
void applyCartesianCube(ObjectStereoAudioProcessor* p, float x, float y, float z) {
  float dx = x - 0.5f;
  float dy = y - 0.5f;
  float azDegCg = radiansToDegrees(std::atan2(dx, dy));
  float elDeg = jlimit(-90.0f, 90.0f, z * 90.0f);
  float dist = 2.0f * std::sqrt(dx * dx + dy * dy);
  p->setAzimuth(normaliseAzimuthDeg(-azDegCg));
  p->setElevation(elDeg);
  p->setDistance(dist);
}
}  // namespace

ObjectStereoAudioProcessor::ObjectStereoAudioProcessor()
    // See the stock Object plugin's processor for why this is 2 discrete
    // 1ch input buses with no output bus - purely so the level meter has
    // something sensible to show; actual object audio routing to Scene is
    // external (REAPER sends), driven by routingA_/routingB_ below.
    : AudioProcessor(BusesProperties()
                     .withInput("Input", AudioChannelSet::discreteChannels(1), true)
                     .withInput("Input", AudioChannelSet::discreteChannels(1), true)),
      samplerate_(48000),
      levelMeter_(std::make_shared<LevelMeterCalculator>(1, samplerate_)) {
  // reaper-adm-extension reads these by raw VST3 parameter INDEX as a
  // 2-point multi-object input (see the class comment and
  // exportaction_admsource-earvst.cpp's isReverbPlugin()/
  // numPointsForMultiObjectPlugin()) - these 18 calls, in this exact order,
  // MUST stay the ONLY addParameter() calls in this processor.
  /* clang-format off */
  addParameter(inputInstanceIdA_ = new ReadOnlyAudioParameterInt(
    "inputInstanceIdA", "Auto-set ID for object L", 0, 65535, 0));
  addParameter(inputInstanceIdB_ = new ReadOnlyAudioParameterInt(
    "inputInstanceIdB", "Auto-set ID for object R", 0, 65535, 0));

  addParameter(exportA_.azimuth = new AudioParameterFloat(
    "exportAzimuthL", "Export Azimuth L", NormalisableRange<float>{-180.f, 180.f}, 0.f));
  addParameter(exportA_.elevation = new AudioParameterFloat(
    "exportElevationL", "Export Elevation L", NormalisableRange<float>{-90.f, 90.f}, 0.f));
  addParameter(exportA_.distance = new AudioParameterFloat(
    "exportDistanceL", "Export Distance L", 0.f, 1.f, 1.f));
  addParameter(exportA_.gain = new AudioParameterFloat(
    "exportGainL", "Export Gain L", NormalisableRange<float>{-100.f, 6.f}, 0.f));
  addParameter(exportA_.width = new AudioParameterFloat(
    "exportWidthL", "Export Width L", 0.f, 1.f, 0.f));
  addParameter(exportA_.height = new AudioParameterFloat(
    "exportHeightL", "Export Height L", 0.f, 1.f, 0.f));
  addParameter(exportA_.depth = new AudioParameterFloat(
    "exportDepthL", "Export Depth L", 0.f, 1.f, 0.f));
  addParameter(exportA_.diffuse = new AudioParameterFloat(
    "exportDiffuseL", "Export Diffuse L", 0.f, 1.f, 0.f));

  addParameter(exportB_.azimuth = new AudioParameterFloat(
    "exportAzimuthR", "Export Azimuth R", NormalisableRange<float>{-180.f, 180.f}, 0.f));
  addParameter(exportB_.elevation = new AudioParameterFloat(
    "exportElevationR", "Export Elevation R", NormalisableRange<float>{-90.f, 90.f}, 0.f));
  addParameter(exportB_.distance = new AudioParameterFloat(
    "exportDistanceR", "Export Distance R", 0.f, 1.f, 1.f));
  addParameter(exportB_.gain = new AudioParameterFloat(
    "exportGainR", "Export Gain R", NormalisableRange<float>{-100.f, 6.f}, 0.f));
  addParameter(exportB_.width = new AudioParameterFloat(
    "exportWidthR", "Export Width R", 0.f, 1.f, 0.f));
  addParameter(exportB_.height = new AudioParameterFloat(
    "exportHeightR", "Export Height R", 0.f, 1.f, 0.f));
  addParameter(exportB_.depth = new AudioParameterFloat(
    "exportDepthR", "Export Depth R", 0.f, 1.f, 0.f));
  addParameter(exportB_.diffuse = new AudioParameterFloat(
    "exportDiffuseR", "Export Diffuse R", 0.f, 1.f, 0.f));
  /* clang-format on */

  connector_ = std::make_unique<ui::ObjectStereoJuceFrontendConnector>(this);
  backendA_ = std::make_unique<ObjectBackend>(&derivedConnectorA_);
  backendB_ = std::make_unique<ObjectBackend>(&derivedConnectorB_);

  recomputeAndPush();
}

ObjectStereoAudioProcessor::~ObjectStereoAudioProcessor() {
  *aliveFlag_ = false;
  if (oscSubscribedPort_ >= 0)
    ear::plugin::OscPortRegistry::getInstance().unsubscribe(oscSubscribedPort_, this);
}

const String ObjectStereoAudioProcessor::getName() const { return JucePlugin_Name; }

bool ObjectStereoAudioProcessor::acceptsMidi() const { return false; }
bool ObjectStereoAudioProcessor::producesMidi() const { return false; }
bool ObjectStereoAudioProcessor::isMidiEffect() const { return false; }
double ObjectStereoAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int ObjectStereoAudioProcessor::getNumPrograms() { return 1; }
int ObjectStereoAudioProcessor::getCurrentProgram() { return 0; }
void ObjectStereoAudioProcessor::setCurrentProgram(int index) {}
const String ObjectStereoAudioProcessor::getProgramName(int index) { return {}; }
void ObjectStereoAudioProcessor::changeProgramName(int index, const String& newName) {}

void ObjectStereoAudioProcessor::prepareToPlay(double samplerate, int samplesPerBlock) {
  auto channels = getTotalNumInputChannels();
  if (samplerate_ != static_cast<int>(samplerate) || numPluginChannels_ != channels) {
    samplerate_ = static_cast<int>(samplerate);
    numPluginChannels_ = channels;
    levelMeter_->setup(numPluginChannels_, samplerate_);
  }
}

void ObjectStereoAudioProcessor::releaseResources() {}

bool ObjectStereoAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
  if (layouts.inputBuses.size() != 2) return false;
  if (layouts.inputBuses[0] != AudioChannelSet::discreteChannels(1)) return false;
  if (layouts.inputBuses[1] != AudioChannelSet::discreteChannels(1)) return false;
  if (layouts.outputBuses.size() != 0) return false;
  return true;
}

void ObjectStereoAudioProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer&) {
  if (getActiveEditor()) levelMeter_->process(buffer);
  backendA_->triggerMetadataSend();
  backendB_->triggerMetadataSend();
}

bool ObjectStereoAudioProcessor::hasEditor() const { return true; }

AudioProcessorEditor* ObjectStereoAudioProcessor::createEditor() {
  levelMeter_->resetLevels();
  return new ObjectStereoAudioProcessorEditor(this);
}

void ObjectStereoAudioProcessor::setRoutingA(int routing) { routingA_ = routing; recomputeAndPush(); }
void ObjectStereoAudioProcessor::setRoutingB(int routing) { routingB_ = routing; recomputeAndPush(); }
void ObjectStereoAudioProcessor::setGainDb(float gainDb) { gainDb_ = gainDb; recomputeAndPush(); }
void ObjectStereoAudioProcessor::setAzimuth(float deg) { azimuthDeg_ = deg; recomputeAndPush(); }
void ObjectStereoAudioProcessor::setElevation(float deg) { elevationDeg_ = deg; recomputeAndPush(); }
void ObjectStereoAudioProcessor::setDistance(float dist) { distance_ = dist; recomputeAndPush(); }
void ObjectStereoAudioProcessor::setPairDistance(float deg) { pairDistanceDeg_ = deg; recomputeAndPush(); }
void ObjectStereoAudioProcessor::setPairRotation(float deg) { pairRotationDeg_ = deg; recomputeAndPush(); }
void ObjectStereoAudioProcessor::setLinkSize(bool link) { linkSize_ = link; }
void ObjectStereoAudioProcessor::setSize(float size) {
  size_ = size;
  if (linkSize_) {
    width_ = size;
    height_ = size;
    depth_ = size;
    recomputeAndPush();
  }
}
void ObjectStereoAudioProcessor::setWidth(float w) { width_ = w; recomputeAndPush(); }
void ObjectStereoAudioProcessor::setHeight(float h) { height_ = h; recomputeAndPush(); }
void ObjectStereoAudioProcessor::setDepth(float d) { depth_ = d; recomputeAndPush(); }
void ObjectStereoAudioProcessor::setDiffuse(float d) { diffuse_ = d; recomputeAndPush(); }

void ObjectStereoAudioProcessor::setUseTrackName(bool useTrackName) {
  useTrackName_ = useTrackName;
  setName(useTrackName || lastKnownCustomName_.empty() ? lastKnownTrackName_
                                                       : lastKnownCustomName_);
}

void ObjectStereoAudioProcessor::setName(const std::string& name) {
  if (!useTrackName_ || lastKnownCustomName_.empty()) lastKnownCustomName_ = name;
  activeName_ = name;
  recomputeAndPush();
}

void ObjectStereoAudioProcessor::setColour(Colour colour) {
  activeColour_ = colour;
  recomputeAndPush();
}

void ObjectStereoAudioProcessor::recomputeAndPush() {
  float halfSep = pairDistanceDeg_ / 2.0f;
  float rotRad = degreesToRadians(pairRotationDeg_);
  float offsetAz = halfSep * std::cos(rotRad);
  float offsetEl = halfSep * std::sin(rotRad);

  float azA = normaliseAzimuthDeg(azimuthDeg_ + offsetAz);
  float azB = normaliseAzimuthDeg(azimuthDeg_ - offsetAz);
  float elA = jlimit(-90.f, 90.f, elevationDeg_ + offsetEl);
  float elB = jlimit(-90.f, 90.f, elevationDeg_ - offsetEl);
  float dist = distance_;

  float gainLinear = Decibels::decibelsToGain(gainDb_);
  float widthDeg = width_ * 360.f;
  float heightDeg = height_ * 360.f;

  derivedConnectorA_.push(routingA_, gainLinear, azA, elA, dist, widthDeg, heightDeg, depth_,
                          diffuse_, activeName_ + " L", activeColour_.getARGB());
  derivedConnectorB_.push(routingB_, gainLinear, azB, elB, dist, widthDeg, heightDeg, depth_,
                          diffuse_, activeName_ + " R", activeColour_.getARGB());

  // Export mirrors for reaper-adm-extension (see the class comment) -
  // distance/width/height/depth/diffuse stay in their 0..1/linear-gain
  // units here, matching point_encoder's own pushExportParams() convention
  // (NOT the *360 degrees / linear-gain-already-applied units the live
  // Scene push above uses).
  *exportA_.azimuth = azA;
  *exportA_.elevation = elA;
  *exportA_.distance = jlimit(0.f, 1.f, dist);
  *exportA_.gain = jlimit(-100.f, 6.f, gainDb_);
  *exportA_.width = width_;
  *exportA_.height = height_;
  *exportA_.depth = depth_;
  *exportA_.diffuse = diffuse_;

  *exportB_.azimuth = azB;
  *exportB_.elevation = elB;
  *exportB_.distance = jlimit(0.f, 1.f, dist);
  *exportB_.gain = jlimit(-100.f, 6.f, gainDb_);
  *exportB_.width = width_;
  *exportB_.height = height_;
  *exportB_.depth = depth_;
  *exportB_.diffuse = diffuse_;

  if (onDerivedPositionsChanged) {
    onDerivedPositionsChanged(DerivedPositions{azA, elA, azB, elB, dist});
  }
}

void ObjectStereoAudioProcessor::getStateInformation(MemoryBlock& destData) {
  std::unique_ptr<XmlElement> xml(new XmlElement("ObjectStereoPlugin"));
  connectionIdA_ = backendA_->getConnectionId();
  connectionIdB_ = backendB_->getConnectionId();
  xml->setAttribute("connection_id_a", connectionIdA_.string());
  xml->setAttribute("connection_id_b", connectionIdB_.string());
  xml->setAttribute("routing_a", routingA_);
  xml->setAttribute("routing_b", routingB_);
  xml->setAttribute("gain", (double)gainDb_);
  xml->setAttribute("azimuth", (double)azimuthDeg_);
  xml->setAttribute("elevation", (double)elevationDeg_);
  xml->setAttribute("distance", (double)distance_);
  xml->setAttribute("pair_distance", (double)pairDistanceDeg_);
  xml->setAttribute("pair_rotation", (double)pairRotationDeg_);
  xml->setAttribute("link_size", linkSize_);
  xml->setAttribute("size", (double)size_);
  xml->setAttribute("width", (double)width_);
  xml->setAttribute("height", (double)height_);
  xml->setAttribute("depth", (double)depth_);
  xml->setAttribute("diffuse", (double)diffuse_);
  xml->setAttribute("use_track_name", useTrackName_);
  xml->setAttribute("name", activeName_);
  xml->setAttribute("osc_enabled", oscEnabled_);
  xml->setAttribute("osc_port", oscPort_);
  xml->setAttribute("osc_source_index", oscSourceIndex_);
  xml->setAttribute("osc_protocol", static_cast<int>(oscProtocol_));
  xml->setAttribute("osc_controlgris_pad_mode", static_cast<int>(controlGrisPadMode_));
  copyXmlToBinary(*xml, destData);
}

void ObjectStereoAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
  std::unique_ptr<XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
  if (xmlState) setStateInformation(xmlState.get());
}

void ObjectStereoAudioProcessor::setStateInformation(XmlElement* xmlState,
                                                     bool useDefaultsIfUnspecified) {
  if (!xmlState->hasTagName("ObjectStereoPlugin")) return;

  if (useDefaultsIfUnspecified || xmlState->hasAttribute("connection_id_a")) {
    connectionIdA_ = communication::ConnectionId{
        xmlState->getStringAttribute("connection_id_a", "00000000-0000-0000-0000-000000000000").toStdString()};
    backendA_->setConnectionId(connectionIdA_);
  }
  if (useDefaultsIfUnspecified || xmlState->hasAttribute("connection_id_b")) {
    connectionIdB_ = communication::ConnectionId{
        xmlState->getStringAttribute("connection_id_b", "00000000-0000-0000-0000-000000000000").toStdString()};
    backendB_->setConnectionId(connectionIdB_);
  }

  if (useDefaultsIfUnspecified || xmlState->hasAttribute("routing_a"))
    routingA_ = xmlState->getIntAttribute("routing_a", -1);
  if (useDefaultsIfUnspecified || xmlState->hasAttribute("routing_b"))
    routingB_ = xmlState->getIntAttribute("routing_b", -1);
  if (useDefaultsIfUnspecified || xmlState->hasAttribute("gain"))
    gainDb_ = static_cast<float>(xmlState->getDoubleAttribute("gain", 0.0));
  if (useDefaultsIfUnspecified || xmlState->hasAttribute("azimuth"))
    azimuthDeg_ = static_cast<float>(xmlState->getDoubleAttribute("azimuth", 0.0));
  if (useDefaultsIfUnspecified || xmlState->hasAttribute("elevation"))
    elevationDeg_ = static_cast<float>(xmlState->getDoubleAttribute("elevation", 0.0));
  if (useDefaultsIfUnspecified || xmlState->hasAttribute("distance"))
    distance_ = static_cast<float>(xmlState->getDoubleAttribute("distance", 1.0));
  if (useDefaultsIfUnspecified || xmlState->hasAttribute("pair_distance"))
    pairDistanceDeg_ = static_cast<float>(xmlState->getDoubleAttribute("pair_distance", 30.0));
  if (useDefaultsIfUnspecified || xmlState->hasAttribute("pair_rotation"))
    pairRotationDeg_ = static_cast<float>(xmlState->getDoubleAttribute("pair_rotation", 0.0));
  if (useDefaultsIfUnspecified || xmlState->hasAttribute("link_size"))
    linkSize_ = xmlState->getBoolAttribute("link_size", false);
  if (useDefaultsIfUnspecified || xmlState->hasAttribute("size"))
    size_ = static_cast<float>(xmlState->getDoubleAttribute("size", 0.0));
  if (useDefaultsIfUnspecified || xmlState->hasAttribute("width"))
    width_ = static_cast<float>(xmlState->getDoubleAttribute("width", 0.0));
  if (useDefaultsIfUnspecified || xmlState->hasAttribute("height"))
    height_ = static_cast<float>(xmlState->getDoubleAttribute("height", 0.0));
  if (useDefaultsIfUnspecified || xmlState->hasAttribute("depth"))
    depth_ = static_cast<float>(xmlState->getDoubleAttribute("depth", 0.0));
  if (useDefaultsIfUnspecified || xmlState->hasAttribute("diffuse"))
    diffuse_ = static_cast<float>(xmlState->getDoubleAttribute("diffuse", 0.0));
  if (useDefaultsIfUnspecified || xmlState->hasAttribute("use_track_name"))
    useTrackName_ = xmlState->getBoolAttribute("use_track_name", true);
  if (useDefaultsIfUnspecified || xmlState->hasAttribute("name"))
    activeName_ = xmlState->getStringAttribute("name", "No Name").toStdString();

  if (useDefaultsIfUnspecified || xmlState->hasAttribute("osc_port"))
    oscPort_ = jlimit(1024, 65535, xmlState->getIntAttribute("osc_port", 9000));
  if (useDefaultsIfUnspecified || xmlState->hasAttribute("osc_source_index"))
    oscSourceIndex_ = jlimit(1, 128, xmlState->getIntAttribute("osc_source_index", 1));
  if (useDefaultsIfUnspecified || xmlState->hasAttribute("osc_protocol"))
    oscProtocol_ = static_cast<OscProtocol>(
        jlimit(0, 1, xmlState->getIntAttribute("osc_protocol", 1)));
  if (useDefaultsIfUnspecified || xmlState->hasAttribute("osc_controlgris_pad_mode"))
    controlGrisPadMode_ = static_cast<ControlGrisPadMode>(jlimit(
        0, 1, xmlState->getIntAttribute("osc_controlgris_pad_mode", 0)));
  if (useDefaultsIfUnspecified || xmlState->hasAttribute("osc_enabled"))
    oscEnabled_ = xmlState->getBoolAttribute("osc_enabled", false);
  applyOscConnection();

  if (connector_) connector_->refreshFromProcessor();
  recomputeAndPush();
}

void ObjectStereoAudioProcessor::updateTrackProperties(const TrackProperties& properties) {
  lastKnownTrackName_ = properties.name.toStdString();
  if (useTrackName_) setName(lastKnownTrackName_);
  setColour(properties.colour.isTransparent() ? ear::plugin::ui::EarColours::PrimaryVariant
                                              : properties.colour);
  if (connector_) connector_->refreshFromProcessor();
}

void ObjectStereoAudioProcessor::setIHostApplication(Steinberg::FUnknown* unknown) {
  reaperHost = dynamic_cast<IReaperHostApplication*>(unknown);
  VST3ClientExtensions::setIHostApplication(unknown);
  if (reaperHost) {
    // One instance ID per derived object (see the member comment on
    // inputInstanceIdA_/B_) - each pushed straight to its own backend's
    // connector as object metadata (point_encoder's setInputInstanceId()
    // pattern), not just stored as a plugin parameter.
    int idA = static_cast<int>(requestInstanceIdFromExtension(reaperHost));
    inputInstanceIdA_->internalSetIntAndNotifyHost(idA);
    derivedConnectorA_.setInputInstanceId(idA);

    int idB = static_cast<int>(requestInstanceIdFromExtension(reaperHost));
    inputInstanceIdB_->internalSetIntAndNotifyHost(idB);
    derivedConnectorB_.setInputInstanceId(idB);

    registerPluginLoadWithExtension(reaperHost,
                                    [this](std::string const& xmlState) {
                                      this->extensionSetState(xmlState);
                                    });

    numDawChannels_ = DetermineChannelCount(reaperHost);
  }
}

void ObjectStereoAudioProcessor::extensionSetState(std::string const& xmlStateStr) {
  auto doc = XmlDocument(xmlStateStr);
  auto xmlState = doc.getDocumentElement();
  setStateInformation(xmlState.get(), false);
}

void ObjectStereoAudioProcessor::setOscDebugText(const String& text) {
  oscDebugText_ = text;
  std::weak_ptr<bool> weakAlive = aliveFlag_;
  MessageManager::callAsync([this, weakAlive]() {
    if (weakAlive.expired()) return;
    if (onOscDebugChanged) onOscDebugChanged();
  });
}

void ObjectStereoAudioProcessor::handleOscMessage(const OSCMessage& message) {
  auto address = message.getAddressPattern().toString();
  setOscDebugText(describeOscMessage(message));
  switch (oscProtocol_) {
    case OscProtocol::ControlGris2:
      if (address.startsWith("/controlgris/")) handleControlGris2Message(address, message);
      break;
    case OscProtocol::Custom:
      handleObjectAddressMessage(address, message);
      break;
  }
}

void ObjectStereoAudioProcessor::handleObjectAddressMessage(const String& address,
                                                            const OSCMessage& message) {
  if (message.size() != 1 || !message[0].isFloat32()) return;
  float value = message[0].getFloat32();

  if (address == "/object/azimuth") setAzimuth(jlimit(-180.f, 180.f, value));
  else if (address == "/object/elevation") setElevation(jlimit(-90.f, 90.f, value));
  else if (address == "/object/distance") setDistance(jlimit(0.f, 2.f, value));
  else if (address == "/object/gain") setGainDb(jlimit(-100.f, 6.f, value));
  else if (address == "/object/size") setSize(jlimit(0.f, 1.f, value));
  else if (address == "/object/width") setWidth(jlimit(0.f, 1.f, value));
  else if (address == "/object/height") setHeight(jlimit(0.f, 1.f, value));
  else if (address == "/object/depth") setDepth(jlimit(0.f, 1.f, value));
  else if (address == "/object/diffuse") setDiffuse(jlimit(0.f, 1.f, value));
}

void ObjectStereoAudioProcessor::handleControlGris2Message(const String& address,
                                                           const OSCMessage& message) {
  auto tokens = StringArray::fromTokens(address, "/", "");
  if (tokens.size() < 4 || tokens[1] != "controlgris") return;

  int pluginId = tokens[2].getIntValue();
  if (pluginId != oscSourceIndex_) {
    setOscDebugText(describeOscMessage(message) + " [ignored: plugin id " + String(pluginId) +
                   " != Src# " + String(oscSourceIndex_) + "]");
    return;
  }

  if (tokens.size() == 4 && (tokens[3] == "azispan" || tokens[3] == "elespan")) {
    if (message.size() != 1 || !message[0].isFloat32()) return;
    setOscDebugText(describeOscMessage(message) + " [applied]");
    if (linkSize_) setLinkSize(false);
    if (tokens[3] == "azispan") setWidth(jlimit(0.f, 1.f, message[0].getFloat32()));
    else setHeight(jlimit(0.f, 1.f, message[0].getFloat32()));
    return;
  }

  if (tokens.size() < 6 || tokens[3] != "traj") return;

  if (tokens.size() == 6 && tokens[5] == "xyz") {
    if (message.size() != 3 || !message[0].isFloat32() || !message[1].isFloat32() ||
        !message[2].isFloat32())
      return;
    setOscDebugText(describeOscMessage(message) + " [applied]");
    if (controlGrisPadMode_ == ControlGrisPadMode::Cube) {
      applyCartesianCube(this, message[0].getFloat32(), message[1].getFloat32(),
                         message[2].getFloat32());
    } else {
      applyCartesian(this, message[0].getFloat32(), message[1].getFloat32(),
                    message[2].getFloat32());
    }
    return;
  }
}

void ObjectStereoAudioProcessor::setOscEnabled(bool enabled) {
  oscEnabled_ = enabled;
  applyOscConnection();
}

void ObjectStereoAudioProcessor::setOscPort(int port) {
  oscPort_ = jlimit(1024, 65535, port);
  if (oscEnabled_) applyOscConnection();
}

void ObjectStereoAudioProcessor::setOscSourceIndex(int index) {
  oscSourceIndex_ = jlimit(1, 128, index);
}

void ObjectStereoAudioProcessor::setOscProtocol(OscProtocol protocol) {
  oscProtocol_ = protocol;
}

void ObjectStereoAudioProcessor::setControlGrisPadMode(ControlGrisPadMode mode) {
  controlGrisPadMode_ = mode;
}

void ObjectStereoAudioProcessor::applyOscConnection() {
  auto& registry = ear::plugin::OscPortRegistry::getInstance();
  if (oscSubscribedPort_ >= 0) {
    registry.unsubscribe(oscSubscribedPort_, this);
    oscSubscribedPort_ = -1;
  }
  if (oscEnabled_) {
    if (registry.subscribe(oscPort_, this)) {
      oscSubscribedPort_ = oscPort_;
    }
  }
}

//==============================================================================
AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
  return new ObjectStereoAudioProcessor();
}
