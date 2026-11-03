#include "lfe_send_frontend_connector.hpp"

#include "components/ear_combo_box.hpp"
#include "components/ear_name_text_editor.hpp"
#include "detail/weak_ptr_helpers.hpp"
#include <daw_channel_count.h>

namespace ear {
namespace plugin {
namespace ui {

using detail::lockIfSame;

inline bool clipToBool(float value) { return value < 0.5 ? false : true; }

LfeSendJuceFrontendConnector::LfeSendJuceFrontendConnector(
    LfeSendAudioProcessor* processor)
    : DirectSpeakersFrontendBackendConnector(), p_(processor) {
  if (p_) {
    auto& parameters = p_->getParameters();
    for (int i = 0; i < parameters.size(); ++i) {
      if (parameters[i]) {
        auto rangedParameter =
            dynamic_cast<RangedAudioParameter*>(parameters[i]);
        if (rangedParameter) {
          parameters_[i] = rangedParameter;
          rangedParameter->addListener(this);
        }
      }
    }
  }
}

LfeSendJuceFrontendConnector::~LfeSendJuceFrontendConnector() {
  for (auto parameter : parameters_) {
    if (parameter.second) {
      parameter.second->removeListener(this);
    }
  }
  if (auto comboBox = colourComboBox_.lock()) {
    comboBox->removeListener(this);
  }
  if (auto comboBox = routingComboBox_.lock()) {
    comboBox->removeListener(this);
  }
  if (auto button = useTrackNameCheckbox_.lock()) {
    button->removeListener(this);
  }
}

void LfeSendJuceFrontendConnector::setStatusBarLabel(
    std::shared_ptr<Label> statusBarLabel) {
  statusBarLabel_ = statusBarLabel;
  setStatusBarText(cachedStatusBarText_);
}

void LfeSendJuceFrontendConnector::doSetStatusBarText(
    const std::string& text) {
  if (!statusBarLabel_.expired()) {
    auto statusBar = statusBarLabel_;

    updater_.callOnMessageThread([text, statusBar]() {
      if (auto statusBarLocked = statusBar.lock()) {
        statusBarLocked->setText(text, dontSendNotification);
      }
    });
  }
  cachedStatusBarText_ = text;
}

void LfeSendJuceFrontendConnector::setNameTextEditor(
    std::shared_ptr<EarNameTextEditor> textEditor) {
  textEditor->addListener(this);
  nameTextEditor_ = textEditor;
  updateNameState();
}

void LfeSendJuceFrontendConnector::setUseTrackNameCheckbox(
  std::shared_ptr<ToggleButton> useTrackNameCheckbox){
  useTrackNameCheckbox->addListener(this);
  useTrackNameCheckbox_ = useTrackNameCheckbox;
  setUseTrackName(cachedUseTrackName_);
}

void LfeSendJuceFrontendConnector::setName(const std::string& name) {
  if (auto nameTextEditorLocked = nameTextEditor_.lock()) {
    nameTextEditorLocked->setText(name);
  }
  if(!cachedUseTrackName_ || lastKnownCustomName_.empty()) {
    lastKnownCustomName_ = name;
  }
  cachedName_ = name;
  // Normally we'd set a processor parameter which in turn calls
  //   parameterValueChanged as a listener, which in turn fires
  //   notifyParameterChanged. Instead, we must do that directly (name state
  //   not held by a parameter)
  notifyParameterChanged(ParameterId::NAME, cachedName_);
}

void LfeSendJuceFrontendConnector::setUseTrackName(bool useTrackName)
{
  auto useTrackNameCheckboxLocked = useTrackNameCheckbox_.lock();
  auto nameTextEditorLocked = nameTextEditor_.lock();

  if (useTrackNameCheckboxLocked && nameTextEditorLocked) {
    auto oldState = useTrackNameCheckboxLocked->getToggleState();
    if(oldState != useTrackName) {
      useTrackNameCheckboxLocked->setToggleState(useTrackName, dontSendNotification);
    }
  }
  cachedUseTrackName_ = useTrackName;
  updateNameState();
}

void LfeSendJuceFrontendConnector::setColourComboBox(
    std::shared_ptr<EarComboBox> comboBox) {
  comboBox->addListener(this);
  colourComboBox_ = comboBox;
  setColour(cachedColour_);
}

void LfeSendJuceFrontendConnector::setColour(Colour colour) {
  if (auto colourComboBoxLocked = colourComboBox_.lock()) {
    colourComboBoxLocked->clearEntries();
    colourComboBoxLocked->addColourEntry(colour);
    colourComboBoxLocked->selectEntry(0, dontSendNotification);
  }
  if (auto lfeLayerLocked = lfeLayer_.lock()) {
    lfeLayerLocked->setHighlightColour(colour);
    lfeLayerLocked->repaint();
  }
  cachedColour_ = colour;
}

void LfeSendJuceFrontendConnector::setRoutingComboBox(
    std::shared_ptr<EarComboBox> comboBox) {
  comboBox->addListener(this);
  routingComboBox_ = comboBox;

  // Single-channel pack format: one DAW channel per entry, no range suffix.
  comboBox->clearEntries();
  for (int i = 1; i <= p_->getNumDawChannels(); ++i) {
    auto entry = comboBox->addTextEntry(String(i), i);
    entry->setSelectable(true);
  }
  setRouting(cachedRouting_);
}

void LfeSendJuceFrontendConnector::setRouting(int routing) {
  if (auto routingComboBoxLocked = routingComboBox_.lock()) {
    routingComboBoxLocked->selectEntry(routing, dontSendNotification);
  }
  cachedRouting_ = routing;
}

void LfeSendJuceFrontendConnector::setLfeLayerValueBox(
    std::shared_ptr<ValueBoxSpeakerLayer> layer) {
  layer->setHighlightColour(cachedColour_);
  layer->setSpeakerSetupPackFormat(kLfeSendPackFormatIdValue);
  lfeLayer_ = layer;
}

void LfeSendJuceFrontendConnector::setChannelGainsValueBox(
    std::shared_ptr<ChannelMeterLayout> gains) {
  gains->setSpeakerSetupPackFormat(kLfeSendPackFormatIdValue);
  channelGains_ = gains;
}

void LfeSendJuceFrontendConnector::parameterValueChanged(
    int parameterIndex, float newValue) {
  using ParameterId = ui::DirectSpeakersFrontendBackendConnector::ParameterId;
  switch (parameterIndex) {
    case 0:
      notifyParameterChanged(ParameterId::ROUTING, p_->getRouting()->get());
      updater_.callOnMessageThread([this]() {
        setRouting(p_->getRouting()->get());
      });
      break;
    case 1:
      // packFormatIdValue_ is permanently fixed - still forward it once so
      // the backend has the LFE-only metadata from the start.
      notifyParameterChanged(ParameterId::PACKFORMAT_ID_VALUE,
                             p_->getPackFormatIdValue()->get());
      break;
    case 3:
      setUseTrackName(p_->getUseTrackName()->get());
      break;
    case 4:
      notifyParameterChanged(ParameterId::INPUT_INSTANCE_ID, p_->getInputInstanceId()->get());
      break;
  }
}

void LfeSendJuceFrontendConnector::trackPropertiesChanged(
    const juce::AudioProcessor::TrackProperties& properties) {
  // It is unclear if this will be called from the message thread so to be sure
  // we call the following async using the MessageManager
  updater_.callOnMessageThread([properties, this]() {
    this->lastKnownTrackName_ = properties.name.toStdString();
    if(this->cachedUseTrackName_) {
      this->setName(this->lastKnownTrackName_);
      notifyParameterChanged(ParameterId::NAME, this->lastKnownTrackName_);
    }
    if (properties.colour.isTransparent()) {
      this->setColour(EarColours::PrimaryVariant);
      notifyParameterChanged(ParameterId::COLOUR,
                             EarColours::PrimaryVariant.getARGB());
    } else {
      this->setColour(properties.colour);
      notifyParameterChanged(ParameterId::COLOUR, properties.colour.getARGB());
    }
  });
}

void LfeSendJuceFrontendConnector::comboBoxChanged(
    EarComboBox* comboBox) {
  if (auto routingComboBox = lockIfSame(routingComboBox_, comboBox)) {
    *(p_->getRouting()) = routingComboBox->getSelectedEntryIndex();
  }
}

void LfeSendJuceFrontendConnector::buttonClicked(Button* button) {
  if (auto useTrackNameCheckbox = lockIfSame(useTrackNameCheckbox_, button)) {
    *(p_->getUseTrackName()) = !useTrackNameCheckbox->getToggleState();
  }
}

void LfeSendJuceFrontendConnector::textEditorTextChanged(TextEditor& textEditor)
{
  if (auto nameTextEditor = lockIfSame(nameTextEditor_, &textEditor)) {
    setName(nameTextEditor->getText().toStdString());
  }
}

void LfeSendJuceFrontendConnector::updateNameState()
{
  auto nameTextEditorLocked = nameTextEditor_.lock();
  if(nameTextEditorLocked) {
    if(cachedUseTrackName_) {
      nameTextEditorLocked->setEnabled(false);
      nameTextEditorLocked->setAlpha(0.38f);
    } else {
      nameTextEditorLocked->setEnabled(true);
      nameTextEditorLocked->setAlpha(1.f);
    }
  }
  if(cachedUseTrackName_ || lastKnownCustomName_.empty()) {
    setName(lastKnownTrackName_);
  } else {
    setName(lastKnownCustomName_);
  }
}

std::string LfeSendJuceFrontendConnector::getActiveName()
{
  return cachedName_;
}

}  // namespace ui
}  // namespace plugin
}  // namespace ear
