#include "lfe_send_plugin_processor.hpp"

#include "components/non_automatable_parameter.hpp"
#include "lfe_send_plugin_editor.hpp"
#include "lfe_send_frontend_connector.hpp"
#include "reaper_integration.hpp"

using namespace ear::plugin;

LfeSendAudioProcessor::LfeSendAudioProcessor()
    : AudioProcessor(
      // Single discrete channel - the LFE feed itself. Unlike DirectSpeakers
      // (which has to support the largest bed layout) this plugin only ever
      // carries one channel, so it never claims more DAW channels/slots than
      // it needs.
      BusesProperties().withInput("Input", AudioChannelSet::discreteChannels(1), true)),
      samplerate_(48000),
      levelMeter_(std::make_shared<LevelMeterCalculator>(1, samplerate_)) {
  /* clang-format off */
  addParameter(routing_ =
    new ui::NonAutomatedParameter<AudioParameterInt>(
      "routing", "Routing",
      -1, MAX_DAW_CHANNELS-1, -1));
  // Range intentionally matches DirectSpeakers' (0x0-0xFFFF, not a zero-width
  // range - JUCE's NormalisableRange divides by (max-min) when normalising,
  // so a single-value range would produce NaN). Nothing in this plugin ever
  // writes a different value to it: there is no UI control bound to it, and
  // setStateInformation() never restores it from a saved value - see there.
  addParameter(packFormatIdValue_ =
    new ui::NonAutomatedParameter<AudioParameterInt>(
      "packFormatIdValue", "PackFormat ID Value",
      0x0, 0xFFFF, kLfeSendPackFormatIdValue));
  addParameter(bypass_ =
    new AudioParameterBool("byps", "Bypass", false));
  addParameter(useTrackName_ =
    new AudioParameterBool("useTrackName", "Use Track Name", true));
  addParameter(inputInstanceId_ = new ReadOnlyAudioParameterInt(
    "inputInstanceId",  // parameter ID
    "Auto-set ID to uniquely identify plugin",  // parameter name
    0, 65535, 0));  // range and default value
  /* clang-format on */

  static_cast<ui::NonAutomatedParameter<AudioParameterInt>*>(routing_)->markPluginStateAsDirty = [this]() {
    bypass_->setValueNotifyingHost(bypass_->get());
  };

  connector_ = std::make_unique<ui::LfeSendJuceFrontendConnector>(this);
  backend_ = std::make_unique<DirectSpeakersBackend>(connector_.get());

  connector_->parameterValueChanged(0, routing_->get());
  connector_->parameterValueChanged(1, packFormatIdValue_->get());
  connector_->parameterValueChanged(3, useTrackName_->get());
}

LfeSendAudioProcessor::~LfeSendAudioProcessor() {}

const String LfeSendAudioProcessor::getName() const {
  return JucePlugin_Name;
}

bool LfeSendAudioProcessor::acceptsMidi() const { return false; }
bool LfeSendAudioProcessor::producesMidi() const { return false; }
bool LfeSendAudioProcessor::isMidiEffect() const { return false; }

double LfeSendAudioProcessor::getTailLengthSeconds() const {
  return 0.0;
}

int LfeSendAudioProcessor::getNumPrograms() {
  return 1;  // NB: some hosts don't cope very well if you tell them there are 0
             // programs, so this should be at least 1, even if you're not
             // really implementing programs.
}

int LfeSendAudioProcessor::getCurrentProgram() { return 0; }
void LfeSendAudioProcessor::setCurrentProgram(int index) {}

const String LfeSendAudioProcessor::getProgramName(int index) {
  return {};
}
void LfeSendAudioProcessor::changeProgramName(int index,
                                               const String& newName) {}

void LfeSendAudioProcessor::prepareToPlay(double samplerate,
                                           int samplesPerBlock) {
  if (samplerate_ != static_cast<int>(samplerate)) {
    samplerate_ = static_cast<int>(samplerate);
    levelMeter_->setup(1, samplerate_);
  }
}

void LfeSendAudioProcessor::releaseResources() {}

bool LfeSendAudioProcessor::isBusesLayoutSupported(
    const BusesLayout& layouts) const {

  // Must accept default config specified in ctor

  if(layouts.inputBuses.size() != 1)
    return false;
  if(layouts.inputBuses[0] != AudioChannelSet::discreteChannels(1))
    return false;

  if(layouts.outputBuses.size() != 0)
    return false;

  return true;
}

void LfeSendAudioProcessor::processBlock(AudioBuffer<float>& buffer,
                                          MidiBuffer& midiMessages) {
  if(!bypass_->get()) {
    if(getActiveEditor()) {
      levelMeter_->process(buffer);
    }
    backend_->triggerMetadataSend();
  }
}

bool LfeSendAudioProcessor::hasEditor() const { return true; }

AudioProcessorEditor* LfeSendAudioProcessor::createEditor() {
  levelMeter_->resetLevels();
  return new LfeSendAudioProcessorEditor(this);
}

void LfeSendAudioProcessor::getStateInformation(MemoryBlock& destData) {
  std::unique_ptr<XmlElement> xml(new XmlElement("LfeSendPlugin"));
  connectionId_ = backend_->getConnectionId();
  xml->setAttribute("connection_id", connectionId_.string());
  xml->setAttribute("routing", (int)*routing_);
  xml->setAttribute("use_track_name", (bool)*useTrackName_);
  xml->setAttribute("name", connector_->getActiveName());
  copyXmlToBinary(*xml, destData);
}

void LfeSendAudioProcessor::setStateInformation(const void* data,
                                                 int sizeInBytes) {
  std::unique_ptr<XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
  if(xmlState) setStateInformation(xmlState.get());

}

void LfeSendAudioProcessor::setStateInformation(XmlElement * xmlState, bool useDefaultsIfUnspecified)
{
    if (xmlState->hasTagName("LfeSendPlugin")) {

      if(useDefaultsIfUnspecified || xmlState->hasAttribute("connection_id")) {
        connectionId_ = communication::ConnectionId{
            xmlState
                ->getStringAttribute("connection_id",
                                     "00000000-0000-0000-0000-000000000000")
                .toStdString() };
        backend_->setConnectionId(connectionId_);
      }

      if(useDefaultsIfUnspecified || xmlState->hasAttribute("routing")) {
        *routing_ = xmlState->getIntAttribute("routing", -1);
      }

      // packFormatIdValue_ is permanently fixed to kLfeSendPackFormatIdValue
      // (no UI control writes to it) - nothing to restore here.

      if(useDefaultsIfUnspecified || xmlState->hasAttribute("use_track_name")) {
        *useTrackName_ = xmlState->getBoolAttribute("use_track_name", true);
      }

      if(useDefaultsIfUnspecified || xmlState->hasAttribute("name")) {
        connector_->setName(xmlState->getStringAttribute("name", "No Name").toStdString());
      }
    }
}

void LfeSendAudioProcessor::updateTrackProperties(
    const TrackProperties& properties) {
  connector_->trackPropertiesChanged(properties);
}

void LfeSendAudioProcessor::setIHostApplication(Steinberg::FUnknown * unknown)
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

void LfeSendAudioProcessor::extensionSetState(std::string const & xmlStateStr)
{
  auto doc = XmlDocument(xmlStateStr);
  auto xmlState = doc.getDocumentElement();
  setStateInformation(xmlState.get(), false);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
  return new LfeSendAudioProcessor();
}
