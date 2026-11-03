#include "lfe_send_plugin_editor.hpp"

#include "components/look_and_feel/colours.hpp"
#include "components/look_and_feel/fonts.hpp"
#include "components/look_and_feel/shadows.hpp"
#include "lfe_send_plugin_processor.hpp"
#include "lfe_send_frontend_connector.hpp"

namespace{
  const int desiredWidth{ 750 };
  const int desiredHeight{ 480 };
}

using namespace ear::plugin::ui;

LfeSendAudioProcessorEditor::LfeSendAudioProcessorEditor(
    LfeSendAudioProcessor* p)
    : AudioProcessorEditor(p),
      p_(p),
      content_(std::make_unique<LfeSendComponent>(p)),
      viewport_(std::make_unique<Viewport>()) {
  p->getFrontendConnector()->setStatusBarLabel(content_->statusBarLabel);
  p->getFrontendConnector()->setNameTextEditor(
      content_->mainValueBox->getNameTextEditor());
  p->getFrontendConnector()->setUseTrackNameCheckbox(
      content_->mainValueBox->getUseTrackNameCheckbox());
  p->getFrontendConnector()->setColourComboBox(
      content_->mainValueBox->getColourComboBox());
  p->getFrontendConnector()->setRoutingComboBox(
      content_->mainValueBox->getRoutingComboBox());
  p->getFrontendConnector()->setLfeLayerValueBox(
      content_->lfeLayerValueBox);
  p->getFrontendConnector()->setChannelGainsValueBox(
      content_->channelMetersBox);

  viewport_->setViewedComponent(content_.get(), false);
  viewport_->setScrollBarsShown(true, true);
  viewport_->getHorizontalScrollBar().setColour(ScrollBar::thumbColourId,
                                                EarColours::Primary);
  viewport_->getVerticalScrollBar().setColour(ScrollBar::thumbColourId,
                                              EarColours::Primary);
  addAndMakeVisible(viewport_.get());

  setResizable(true, false);
  setResizeLimits(0, 0, desiredWidth, desiredHeight);
  setSize(desiredWidth, desiredHeight);
}

LfeSendAudioProcessorEditor::~LfeSendAudioProcessorEditor() {}

void LfeSendAudioProcessorEditor::paint(Graphics& g) {}

void LfeSendAudioProcessorEditor::resized() {
  viewport_->setBounds(getLocalBounds());
  content_->setBounds(0, 0, desiredWidth, desiredHeight);
}
