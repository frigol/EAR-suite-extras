#include "object_plugin_processor.hpp"

#include <cmath>

#include "components/non_automatable_parameter.hpp"
#include "object_backend.hpp"
#include "object_frontend_connector.hpp"
#include "object_plugin_editor.hpp"
#include "reaper_integration.hpp"

using namespace ear::plugin;

ObjectsAudioProcessor::ObjectsAudioProcessor()
    // The 2 channels of input here is intentional.
    /// 2ch is the narrowest track width REAPER will assign.
    /// If we were to set 1ch input, the meter would only monitor the first channel of the track.
    /// However, REAPERs default send behaviour is to send from the first 2 channels regardless of track width, and even if you are only sending to 1 channel.
    /// Therefore it is more appropriate to be monitoring activity on the first 2 input channels.
    /// The use of 2 buses rather than one 2 channel bus is also intentional. It avoids REAPER applying left and right labels to the inputs.
    // The omission of an output bus is also intentional.
    /// We do not actually manipulate audio here - only analyse it for level.
    /// On a side-note; If we were to set mono output, REAPERs default pin-mapping causes the output to be passed to both output channels on a stereo track
    /// This can cause dropping of the original audio for the second channel, which is problematic if the second channel is in fact used by an additional plugin.

    : AudioProcessor(BusesProperties()
                     .withInput("Input", AudioChannelSet::discreteChannels(1), true)
                     .withInput("Input", AudioChannelSet::discreteChannels(1), true)),
      samplerate_(48000),
      levelMeter_(std::make_shared<LevelMeterCalculator>(1, samplerate_)) {
  /* clang-format off */
  addParameter(routing_ = new ui::NonAutomatedParameter<AudioParameterInt>("routing", "Routing", -1, MAX_DAW_CHANNELS-1, -1));
  addParameter(gain_ = new AudioParameterFloat("gain", "Gain", NormalisableRange<float>{-100.f, 6.f}, 0.f));
  addParameter(azimuth_ = new AudioParameterFloat("azimuth", "Azimuth", NormalisableRange<float>{-180.f, 180.f}, 0.f));
  addParameter(elevation_ = new AudioParameterFloat("elevation", "Elevation", NormalisableRange<float>{-90.f, 90.f}, 0.f));
  addParameter(distance_ = new AudioParameterFloat("distance", "Distance", NormalisableRange<float>{0.f, 1.f}, 1.f));
  addParameter(linkSize_ = new AudioParameterBool("linkSize", "Link Size", false));
  addParameter(size_ = new AudioParameterFloat("size", "Size", 0.f, 1.f, 0.f));
  addParameter(width_ = new AudioParameterFloat("width", "Width", 0.f, 1.f, 0.f));
  addParameter(height_ = new AudioParameterFloat("height", "Height", 0.f, 1.f, 0.f));
  addParameter(depth_ = new AudioParameterFloat("depth","Depth", 0.f, 1.f, 0.f));
  addParameter(diffuse_ = new AudioParameterFloat("diffuse", "Diffuse", 0.f, 1.f, 0.f));
  addParameter(divergence_ = new AudioParameterBool("divergence", "Divergence", false));
  addParameter(factor_ = new AudioParameterFloat("factor", "Factor", 0.f, 1.f, 0.f));
  addParameter(range_ = new AudioParameterFloat("range", "Range", 0.f, 180.f, 45.f));
  addParameter(bypass_ = new AudioParameterBool("byps", "Bypass", false));
  addParameter(useTrackName_ = new AudioParameterBool("useTrackName", "Use Track Name", true));
  addParameter(oscEnabled_ = new AudioParameterBool("oscEnabled", "OSC Enabled", false));
  addParameter(inputInstanceId_ = new ReadOnlyAudioParameterInt(
    "inputInstanceId",  // parameter ID
    "Auto-set ID to uniquely identify plugin",  // parameter name
    0, 65535, 0));  // range and default value

  /* clang-format on */

  static_cast<ui::NonAutomatedParameter<AudioParameterInt>*>(routing_)->markPluginStateAsDirty = [this]() {
    bypass_->setValueNotifyingHost(bypass_->get());
  };

  connector_ = std::make_unique<ui::ObjectsJuceFrontendConnector>(this);
  backend_ = std::make_unique<ObjectBackend>(connector_.get());

  connector_->parameterValueChanged(0, routing_->get());
  connector_->parameterValueChanged(1, gain_->get());
  connector_->parameterValueChanged(2, azimuth_->get());
  connector_->parameterValueChanged(3, elevation_->get());
  connector_->parameterValueChanged(4, distance_->get());
  connector_->parameterValueChanged(5, linkSize_->get());
  connector_->parameterValueChanged(6, size_->get());
  connector_->parameterValueChanged(7, width_->get());
  connector_->parameterValueChanged(8, height_->get());
  connector_->parameterValueChanged(9, depth_->get());
  connector_->parameterValueChanged(10, diffuse_->get());
  connector_->parameterValueChanged(11, divergence_->get());
  connector_->parameterValueChanged(12, factor_->get());
  connector_->parameterValueChanged(13, range_->get());
  connector_->parameterValueChanged(14, useTrackName_->get());
}

ObjectsAudioProcessor::~ObjectsAudioProcessor() {
  *aliveFlag_ = false;
  if (oscSubscribedPort_ >= 0)
    ear::plugin::OscPortRegistry::getInstance().unsubscribe(oscSubscribedPort_, this);
}

const String ObjectsAudioProcessor::getName() const { return JucePlugin_Name; }

bool ObjectsAudioProcessor::acceptsMidi() const { return false; }

bool ObjectsAudioProcessor::producesMidi() const { return false; }

bool ObjectsAudioProcessor::isMidiEffect() const { return false; }

double ObjectsAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int ObjectsAudioProcessor::getNumPrograms() {
  return 1;  // NB: some hosts don't cope very well if you tell them there are 0
             // programs, so this should be at least 1, even if you're not
             // really implementing programs.
}

int ObjectsAudioProcessor::getCurrentProgram() { return 0; }

void ObjectsAudioProcessor::setCurrentProgram(int index) {}

const String ObjectsAudioProcessor::getProgramName(int index) { return {}; }

void ObjectsAudioProcessor::changeProgramName(int index,
                                              const String& newName) {}

void ObjectsAudioProcessor::prepareToPlay(double samplerate,
                                          int samplesPerBlock) {
  auto channels = getTotalNumInputChannels();
  if (samplerate_ != static_cast<int>(samplerate) || numPluginChannels_ != channels) {
    samplerate_ = static_cast<int>(samplerate);
    numPluginChannels_= channels;
    levelMeter_->setup(numPluginChannels_, samplerate_);
  }
}

void ObjectsAudioProcessor::releaseResources() {
  // When playback stops, you can use this as an opportunity to free up any
  // spare memory, etc.
}

bool ObjectsAudioProcessor::isBusesLayoutSupported(
    const BusesLayout& layouts) const {

  // Must accept default config specified in ctor

  if(layouts.inputBuses.size() != 2)
    return false;
  if(layouts.inputBuses[0] != AudioChannelSet::discreteChannels(1))
    return false;
  if(layouts.inputBuses[1] != AudioChannelSet::discreteChannels(1))
    return false;

  if(layouts.outputBuses.size() != 0)
    return false;

  return true;
}

void ObjectsAudioProcessor::processBlock(AudioBuffer<float>& buffer,
                                         MidiBuffer& midiMessages) {
  if(!bypass_->get()) {
    if(getActiveEditor()) {
      levelMeter_->process(buffer);
    }
    backend_->triggerMetadataSend();
  }
}

bool ObjectsAudioProcessor::hasEditor() const { return true; }

AudioProcessorEditor* ObjectsAudioProcessor::createEditor() {
  levelMeter_->resetLevels();
  return new ObjectAudioProcessorEditor(this);
}

void ObjectsAudioProcessor::getStateInformation(MemoryBlock& destData) {
  std::unique_ptr<XmlElement> xml(new XmlElement("ObjectsPlugin"));
  connectionId_ = backend_->getConnectionId();
  xml->setAttribute("connection_id", connectionId_.string());
  xml->setAttribute("routing", (int)*routing_);
  xml->setAttribute("gain", (double)*gain_);
  xml->setAttribute("azimuth", (double)*azimuth_);
  xml->setAttribute("elevation", (double)*elevation_);
  xml->setAttribute("distance", (double)*distance_);
  xml->setAttribute("link_size", (bool)*linkSize_);
  xml->setAttribute("size", (double)*size_);
  xml->setAttribute("width", (double)*width_);
  xml->setAttribute("height", (double)*height_);
  xml->setAttribute("depth", (double)*depth_);
  xml->setAttribute("diffuse", (double)*diffuse_);
  xml->setAttribute("divergence", (bool)*divergence_);
  xml->setAttribute("factor", (double)*factor_);
  xml->setAttribute("range", (double)*range_);
  xml->setAttribute("use_track_name", (bool)*useTrackName_);
  xml->setAttribute("osc_enabled", (bool)*oscEnabled_);
  xml->setAttribute("osc_port", oscPort_);
  xml->setAttribute("osc_source_index", oscSourceIndex_);
  xml->setAttribute("osc_protocol", static_cast<int>(oscProtocol_));
  xml->setAttribute("name", connector_->getActiveName());
  copyXmlToBinary(*xml, destData);
}

void ObjectsAudioProcessor::setStateInformation(const void* data,
                                                int sizeInBytes) {
  std::unique_ptr<XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
  if(xmlState) setStateInformation(xmlState.get());
}

void ObjectsAudioProcessor::setStateInformation(XmlElement * xmlState, bool useDefaultsIfUnspecified)
{
  if (xmlState->hasTagName("ObjectsPlugin")) {
    if(useDefaultsIfUnspecified || xmlState->hasAttribute("connection_id")) {
      connectionId_ = communication::ConnectionId{
        xmlState
        ->getStringAttribute("connection_id",
                             "00000000-0000-0000-0000-000000000000")
        .toStdString() };
      backend_->setConnectionId(connectionId_);
    }

    if(useDefaultsIfUnspecified || xmlState->hasAttribute("routing"))
      *routing_ = xmlState->getIntAttribute("routing", -1);

    if(useDefaultsIfUnspecified || xmlState->hasAttribute("gain"))
      *gain_ = xmlState->getDoubleAttribute("gain", 0.0);

    if(useDefaultsIfUnspecified || xmlState->hasAttribute("azimuth"))
      *azimuth_ = xmlState->getDoubleAttribute("azimuth", 0.0);

    if(useDefaultsIfUnspecified || xmlState->hasAttribute("elevation"))
      *elevation_ = xmlState->getDoubleAttribute("elevation", 0.0);

    if(useDefaultsIfUnspecified || xmlState->hasAttribute("distance"))
      *distance_ = xmlState->getDoubleAttribute("distance", 1.0);

    if(useDefaultsIfUnspecified || xmlState->hasAttribute("link_size"))
      *linkSize_ = xmlState->getBoolAttribute("link_size", true);

    if(useDefaultsIfUnspecified || xmlState->hasAttribute("size"))
      *size_ = xmlState->getDoubleAttribute("size", 0.0);

    if(useDefaultsIfUnspecified || xmlState->hasAttribute("width"))
      *width_ = xmlState->getDoubleAttribute("width", 0.0);

    if(useDefaultsIfUnspecified || xmlState->hasAttribute("height"))
      *height_ = xmlState->getDoubleAttribute("height", 0.0);

    if(useDefaultsIfUnspecified || xmlState->hasAttribute("depth"))
      *depth_ = xmlState->getDoubleAttribute("depth", 0.0);

    if(useDefaultsIfUnspecified || xmlState->hasAttribute("diffuse"))
      *diffuse_ = xmlState->getDoubleAttribute("diffuse", 0.0);

    if(useDefaultsIfUnspecified || xmlState->hasAttribute("divergence"))
      *divergence_ = xmlState->getBoolAttribute("divergence", false);

    if(useDefaultsIfUnspecified || xmlState->hasAttribute("factor"))
      *factor_ = xmlState->getDoubleAttribute("factor", 0.0);

    if(useDefaultsIfUnspecified || xmlState->hasAttribute("range"))
      *range_ = xmlState->getDoubleAttribute("range", 0.0);

    if(useDefaultsIfUnspecified || xmlState->hasAttribute("use_track_name"))
      *useTrackName_ = xmlState->getBoolAttribute("use_track_name", true);

    if(useDefaultsIfUnspecified || xmlState->hasAttribute("osc_port"))
      oscPort_ = jlimit(1024, 65535, xmlState->getIntAttribute("osc_port", 9000));
    if(useDefaultsIfUnspecified || xmlState->hasAttribute("osc_source_index"))
      oscSourceIndex_ = jlimit(1, 128, xmlState->getIntAttribute("osc_source_index", 1));
    if(useDefaultsIfUnspecified || xmlState->hasAttribute("osc_protocol"))
      oscProtocol_ = static_cast<OscProtocol>(
          jlimit(0, 1, xmlState->getIntAttribute("osc_protocol", 1)));
    if(useDefaultsIfUnspecified || xmlState->hasAttribute("osc_enabled"))
      *oscEnabled_ = xmlState->getBoolAttribute("osc_enabled", false);
    applyOscConnection();

    if(useDefaultsIfUnspecified || xmlState->hasAttribute("name"))
      connector_->setName(xmlState->getStringAttribute("name", "No Name").toStdString());
  }
}

void ObjectsAudioProcessor::setOscEnabled(bool enabled) {
  oscEnabled_->setValueNotifyingHost(enabled ? 1.0f : 0.0f);
  applyOscConnection();
}

void ObjectsAudioProcessor::setOscPort(int port) {
  oscPort_ = jlimit(1024, 65535, port);
  if (oscEnabled_->get()) applyOscConnection();
}

void ObjectsAudioProcessor::setOscSourceIndex(int index) {
  oscSourceIndex_ = jlimit(1, 128, index);
}

void ObjectsAudioProcessor::setOscProtocol(OscProtocol protocol) {
  oscProtocol_ = protocol;
}

void ObjectsAudioProcessor::applyOscConnection() {
  auto& registry = ear::plugin::OscPortRegistry::getInstance();
  if (oscSubscribedPort_ >= 0) {
    registry.unsubscribe(oscSubscribedPort_, this);
    oscSubscribedPort_ = -1;
  }
  if (oscEnabled_->get()) {
    if (registry.subscribe(oscPort_, this)) {
      oscSubscribedPort_ = oscPort_;
    }
    // else: the port could not be bound at all - most likely something
    // outside this plugin (another application) already has it. There's
    // no AlertWindow here - this runs from the message thread via a UI
    // callback or preset load, not somewhere a modal dialog would be
    // appropriate - so this just silently stays unsubscribed; the OSC
    // toggle in the editor reflects oscEnabled_, not whether the
    // subscription actually succeeded, which is a known gap rather than a
    // deliberate design choice. Note this is no longer the usual cause of
    // "nothing received" when several Object instances share a port -
    // OscPortRegistry lets them all subscribe to the same port
    // successfully, each filtering by its own Src#.
  }
}

namespace {
void applyParam(AudioParameterFloat* param, float realValue) {
  param->setValueNotifyingHost(param->convertTo0to1(jlimit(
      param->getNormalisableRange().start, param->getNormalisableRange().end, realValue)));
}

// Wraps into (-180, 180].
float normaliseAzimuthDeg(float deg) {
  deg = std::fmod(deg + 180.0f, 360.0f);
  if (deg < 0.0f) deg += 360.0f;
  return deg - 180.0f;
}

// ControlGRIS2's /xyz triplet in DOME-SpatGris mode is NOT a cartesian unit
// vector - it's a normalised pad position, reverse engineered from 7 paired
// ControlGRIS2/EAR Object OSC calibration screenshots (az 0/90/180/270 at
// el 0, and el 0.5/1.0 at az 0/180/270):
//   x, y in [0,1], centred on (0.5, 0.5); the radius from that centre encodes
//   elevation, shrinking LINEARLY to 0 at the zenith - i.e. dx=x-0.5,
//   dy=y-0.5 give an azimuth-only direction, confirmed exact at all 4
//   cardinal azimuths (dx,dy magnitude was always 0.5 at elevation 0).
//   z in [0,1] is elevation itself, linearly normalised for 0..90 degrees
//   (z=0.5 measured at dx/dy magnitude 0.25 = 0.5*(1-0.5), confirming the
//   linear - not cosine/spherical - radius falloff).
// Azimuth in CG's pad is clockwise from front (dy>0); ADM is anticlockwise
// from front, hence the sign flip. ControlGRIS2's DOME mode has no distance
// control (sources live on a unit dome), so distance is left at 1.0 rather
// than guessed from x/y/z magnitude.
void applyCartesian(AudioParameterFloat* azimuth, AudioParameterFloat* elevation,
                    AudioParameterFloat* distance, float x, float y, float z) {
  float dx = x - 0.5f;
  float dy = y - 0.5f;
  float azDegCg = radiansToDegrees(std::atan2(dx, dy));
  float elDeg = jlimit(-90.0f, 90.0f, z * 90.0f);
  applyParam(azimuth, normaliseAzimuthDeg(-azDegCg));
  applyParam(elevation, elDeg);
  applyParam(distance, 1.0f);
}
}  // namespace

namespace {
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
}  // namespace

void ObjectsAudioProcessor::setOscDebugText(const String& text) {
  oscDebugText_ = text;
  std::weak_ptr<bool> weakAlive = aliveFlag_;
  MessageManager::callAsync([this, weakAlive]() {
    if (weakAlive.expired()) return;
    if (onOscDebugChanged) onOscDebugChanged();
  });
}

void ObjectsAudioProcessor::handleOscMessage(const OSCMessage& message) {
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

void ObjectsAudioProcessor::handleObjectAddressMessage(
    const String& address, const OSCMessage& message) {
  if (message.size() != 1 || !message[0].isFloat32()) return;
  float value = message[0].getFloat32();

  if (address == "/object/azimuth") applyParam(azimuth_, value);
  else if (address == "/object/elevation") applyParam(elevation_, value);
  else if (address == "/object/distance") applyParam(distance_, value);
  else if (address == "/object/gain") applyParam(gain_, value);
  else if (address == "/object/size") applyParam(size_, value);
  else if (address == "/object/width") applyParam(width_, value);
  else if (address == "/object/height") applyParam(height_, value);
  else if (address == "/object/depth") applyParam(depth_, value);
  else if (address == "/object/diffuse") applyParam(diffuse_, value);
}

void ObjectsAudioProcessor::handleControlGris2Message(
    const String& address, const OSCMessage& message) {
  // address is "/controlgris/<pluginId>/traj/<trajId>/<component>[/<n>]" for
  // position, or "/controlgris/<pluginId>/<component>" (no /traj/<trajId>/
  // segment - these are per-plugin-instance, not per-source) for the two
  // span controls. See the long comment on OscProtocol for where this came
  // from. fromTokens() on a string starting with the "/" break character
  // produces a leading empty token (JUCE's StringArray::addTokens scans
  // from position 0, so the token ending at that very first "/" is "") -
  // tokens[0] is "", tokens[1] is "controlgris", etc., not tokens[0] ==
  // "controlgris".
  auto tokens = StringArray::fromTokens(address, "/", "");
  if (tokens.size() < 4 || tokens[1] != "controlgris") return;

  int pluginId = tokens[2].getIntValue();
  if (pluginId != oscSourceIndex_) {
    setOscDebugText(describeOscMessage(message) +
                    " [ignored: plugin id " + String(pluginId) +
                    " != Src# " + String(oscSourceIndex_) + "]");
    return;  // a different object's message
  }

  // Extent: ControlGRIS2's Azimuth Span / Elevation Span -> EAR Object's
  // Width / Height, kept unlinked (linkSize_ off) so they stay independent
  // instead of collapsing onto a single Size knob. Addresses and the 0..1
  // range confirmed by a live OSC capture (/controlgris/<id>/azispan and
  // .../elespan - no /traj/<trajId>/ segment).
  if (tokens.size() == 4 && (tokens[3] == "azispan" || tokens[3] == "elespan")) {
    if (message.size() != 1 || !message[0].isFloat32()) return;
    setOscDebugText(describeOscMessage(message) + " [applied]");
    if (linkSize_->get()) linkSize_->setValueNotifyingHost(0.0f);
    applyParam(tokens[3] == "azispan" ? width_ : height_, message[0].getFloat32());
    return;
  }

  if (tokens.size() < 6 || tokens[3] != "traj") return;

  // Position: only the combined /xyz triplet is handled (ControlGRIS2 also
  // sends /x, /y, /z, /xy and /xyz/1, /xyz/2, /xyz/3 redundantly carrying
  // the same update, which would otherwise each re-trigger a partial, stale
  // move).
  if (tokens.size() == 6 && tokens[5] == "xyz") {
    if (message.size() != 3 || !message[0].isFloat32() || !message[1].isFloat32() ||
        !message[2].isFloat32())
      return;
    setOscDebugText(describeOscMessage(message) + " [applied]");
    applyCartesian(azimuth_, elevation_, distance_, message[0].getFloat32(),
                  message[1].getFloat32(), message[2].getFloat32());
    return;
  }
}

void ObjectsAudioProcessor::updateTrackProperties(
    const TrackProperties& properties) {
  connector_->trackPropertiesChanged(properties);
}

void ObjectsAudioProcessor::setIHostApplication(Steinberg::FUnknown * unknown)
{
  reaperHost = dynamic_cast<IReaperHostApplication*>(unknown);
  VST3ClientExtensions::setIHostApplication(unknown);
  if(reaperHost) {

    inputInstanceId_->internalSetIntAndNotifyHost(
        requestInstanceIdFromExtension(reaperHost));

    registerPluginLoadWithExtension(reaperHost,
                                    [this](std::string const& xmlState) {
                                      this->extensionSetState(xmlState);
                                    });

    numDawChannels_ = DetermineChannelCount(reaperHost);
  }
}

void ObjectsAudioProcessor::extensionSetState(std::string const & xmlStateStr)
{
  auto doc = XmlDocument(xmlStateStr);
  auto xmlState = doc.getDocumentElement();
  setStateInformation(xmlState.get(), false);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
  return new ObjectsAudioProcessor();
}
