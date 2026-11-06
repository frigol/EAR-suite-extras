#include "object_stereo_frontend_connector.hpp"

#include "components/ear_combo_box.hpp"
#include "components/ear_name_text_editor.hpp"
#include "components/ear_inverted_slider.hpp"
#include "components/panner_top_view.hpp"
#include "components/panner_side_view.hpp"
#include "components/look_and_feel/colours.hpp"

#include "detail/weak_ptr_helpers.hpp"

namespace ear {
namespace plugin {
namespace ui {

using detail::lockIfSame;

ObjectStereoJuceFrontendConnector::ObjectStereoJuceFrontendConnector(
    ObjectStereoAudioProcessor* processor)
    : p_(processor) {}

ObjectStereoJuceFrontendConnector::~ObjectStereoJuceFrontendConnector() {
  if (auto comboBox = routingAComboBox_.lock()) comboBox->removeListener(this);
  if (auto comboBox = routingBComboBox_.lock()) comboBox->removeListener(this);
  if (auto comboBox = colourComboBox_.lock()) comboBox->removeListener(this);
  if (auto slider = gainSlider_.lock()) slider->removeListener(this);
  if (auto slider = azimuthSlider_.lock()) slider->removeListener(this);
  if (auto slider = elevationSlider_.lock()) slider->removeListener(this);
  if (auto slider = distanceSlider_.lock()) slider->removeListener(this);
  if (auto slider = pairDistanceSlider_.lock()) slider->removeListener(this);
  if (auto slider = pairRotationSlider_.lock()) slider->removeListener(this);
  if (auto slider = sizeSlider_.lock()) slider->removeListener(this);
  if (auto slider = widthSlider_.lock()) slider->removeListener(this);
  if (auto slider = heightSlider_.lock()) slider->removeListener(this);
  if (auto slider = depthSlider_.lock()) slider->removeListener(this);
  if (auto slider = diffuseSlider_.lock()) slider->removeListener(this);
  if (auto button = linkSizeButton_.lock()) button->removeListener(this);
  if (auto button = useTrackNameCheckbox_.lock()) button->removeListener(this);
  if (auto panner = pannerTopView_.lock()) panner->removeListener(this);
  if (auto panner = pannerSideView_.lock()) panner->removeListener(this);
}

void ObjectStereoJuceFrontendConnector::refreshFromProcessor() {
  if (auto locked = statusBar_.lock()) { /* nothing time-varying to show yet */ }
  if (auto locked = nameTextEditor_.lock()) locked->setText(p_->getActiveName());
  if (auto locked = useTrackNameCheckbox_.lock())
    locked->setToggleState(p_->getUseTrackName(), dontSendNotification);
  if (auto locked = nameTextEditor_.lock()) {
    locked->setEnabled(!p_->getUseTrackName());
    locked->setAlpha(p_->getUseTrackName() ? 0.38f : 1.f);
  }
  if (auto locked = colourComboBox_.lock()) {
    locked->clearEntries();
    locked->addColourEntry(p_->getActiveColour());
    locked->selectEntry(0, dontSendNotification);
  }
  if (auto locked = routingAComboBox_.lock())
    locked->selectEntry(p_->getRoutingA(), dontSendNotification);
  if (auto locked = routingBComboBox_.lock())
    locked->selectEntry(p_->getRoutingB(), dontSendNotification);
  if (auto locked = gainSlider_.lock()) locked->setValue(p_->getGainDb(), dontSendNotification);
  if (auto locked = azimuthSlider_.lock()) locked->setValue(p_->getAzimuth(), dontSendNotification);
  if (auto locked = elevationSlider_.lock()) locked->setValue(p_->getElevation(), dontSendNotification);
  if (auto locked = distanceSlider_.lock()) locked->setValue(p_->getDistance(), dontSendNotification);
  if (auto locked = pairDistanceSlider_.lock()) locked->setValue(p_->getPairDistance(), dontSendNotification);
  if (auto locked = pairRotationSlider_.lock()) locked->setValue(p_->getPairRotation(), dontSendNotification);
  if (auto locked = pannerTopView_.lock()) {
    locked->setColour(PannerTopView::highlightColourId, p_->getActiveColour());
    locked->setAzimuth(p_->getAzimuth(), dontSendNotification);
    locked->setDistance(p_->getDistance(), dontSendNotification);
    locked->repaint();
  }
  if (auto locked = pannerSideView_.lock()) {
    locked->setColour(PannerSideView::highlightColourId, p_->getActiveColour());
    locked->setElevation(p_->getElevation(), dontSendNotification);
    locked->repaint();
  }
  if (auto locked = linkSizeButton_.lock())
    locked->setToggleState(p_->getLinkSize(), dontSendNotification);
  if (auto locked = sizeSlider_.lock()) locked->setValue(p_->getSize(), dontSendNotification);
  if (auto locked = widthSlider_.lock()) locked->setValue(p_->getWidth(), dontSendNotification);
  if (auto locked = heightSlider_.lock()) locked->setValue(p_->getHeight(), dontSendNotification);
  if (auto locked = depthSlider_.lock()) locked->setValue(p_->getDepth(), dontSendNotification);
  if (auto locked = diffuseSlider_.lock()) locked->setValue(p_->getDiffuse(), dontSendNotification);
  updateLinkSizeEnablement();
}

void ObjectStereoJuceFrontendConnector::setStatusBarLabel(std::shared_ptr<Label> statusBarLabel) {
  statusBar_ = statusBarLabel;
}

void ObjectStereoJuceFrontendConnector::setNameTextEditor(
    std::shared_ptr<EarNameTextEditor> textEditor) {
  textEditor->addListener(this);
  nameTextEditor_ = textEditor;
  refreshFromProcessor();
}

void ObjectStereoJuceFrontendConnector::setUseTrackNameCheckbox(
    std::shared_ptr<ToggleButton> useTrackNameCheckbox) {
  useTrackNameCheckbox->addListener(this);
  useTrackNameCheckbox_ = useTrackNameCheckbox;
  refreshFromProcessor();
}

void ObjectStereoJuceFrontendConnector::setColourComboBox(std::shared_ptr<EarComboBox> comboBox) {
  comboBox->addListener(this);
  colourComboBox_ = comboBox;
  refreshFromProcessor();
}

void ObjectStereoJuceFrontendConnector::setPannerTopView(std::shared_ptr<PannerTopView> panner) {
  panner->addListener(this);
  pannerTopView_ = panner;
  refreshFromProcessor();
}

void ObjectStereoJuceFrontendConnector::setPannerSideView(std::shared_ptr<PannerSideView> panner) {
  panner->addListener(this);
  pannerSideView_ = panner;
  refreshFromProcessor();
}

void ObjectStereoJuceFrontendConnector::setRoutingAComboBox(std::shared_ptr<EarComboBox> comboBox) {
  comboBox->addListener(this);
  routingAComboBox_ = comboBox;
  refreshFromProcessor();
}

void ObjectStereoJuceFrontendConnector::setRoutingBComboBox(std::shared_ptr<EarComboBox> comboBox) {
  comboBox->addListener(this);
  routingBComboBox_ = comboBox;
  refreshFromProcessor();
}

void ObjectStereoJuceFrontendConnector::setGainSlider(std::shared_ptr<EarSlider> slider) {
  slider->addListener(this);
  gainSlider_ = slider;
  refreshFromProcessor();
}

void ObjectStereoJuceFrontendConnector::setAzimuthSlider(std::shared_ptr<EarInvertedSlider> slider) {
  slider->addListener(this);
  azimuthSlider_ = slider;
  refreshFromProcessor();
}

void ObjectStereoJuceFrontendConnector::setElevationSlider(std::shared_ptr<EarSlider> slider) {
  slider->addListener(this);
  elevationSlider_ = slider;
  refreshFromProcessor();
}

void ObjectStereoJuceFrontendConnector::setDistanceSlider(std::shared_ptr<EarSlider> slider) {
  slider->addListener(this);
  distanceSlider_ = slider;
  refreshFromProcessor();
}

void ObjectStereoJuceFrontendConnector::setPairDistanceSlider(std::shared_ptr<EarSlider> slider) {
  slider->addListener(this);
  pairDistanceSlider_ = slider;
  refreshFromProcessor();
}

void ObjectStereoJuceFrontendConnector::setPairRotationSlider(std::shared_ptr<EarSlider> slider) {
  slider->addListener(this);
  pairRotationSlider_ = slider;
  refreshFromProcessor();
}

void ObjectStereoJuceFrontendConnector::setLinkSizeButton(std::shared_ptr<EarButton> button) {
  button->addListener(this);
  linkSizeButton_ = button;
  refreshFromProcessor();
}
void ObjectStereoJuceFrontendConnector::setSizeLabel(std::shared_ptr<Label> label) {
  sizeLabel_ = label;
  updateLinkSizeEnablement();
}
void ObjectStereoJuceFrontendConnector::setSizeSlider(std::shared_ptr<EarSlider> slider) {
  slider->addListener(this);
  sizeSlider_ = slider;
  refreshFromProcessor();
}
void ObjectStereoJuceFrontendConnector::setWidthLabel(std::shared_ptr<Label> label) {
  widthLabel_ = label;
  updateLinkSizeEnablement();
}
void ObjectStereoJuceFrontendConnector::setWidthSlider(std::shared_ptr<EarSlider> slider) {
  slider->addListener(this);
  widthSlider_ = slider;
  refreshFromProcessor();
}
void ObjectStereoJuceFrontendConnector::setHeightLabel(std::shared_ptr<Label> label) {
  heightLabel_ = label;
  updateLinkSizeEnablement();
}
void ObjectStereoJuceFrontendConnector::setHeightSlider(std::shared_ptr<EarSlider> slider) {
  slider->addListener(this);
  heightSlider_ = slider;
  refreshFromProcessor();
}
void ObjectStereoJuceFrontendConnector::setDepthLabel(std::shared_ptr<Label> label) {
  depthLabel_ = label;
  updateLinkSizeEnablement();
}
void ObjectStereoJuceFrontendConnector::setDepthSlider(std::shared_ptr<EarSlider> slider) {
  slider->addListener(this);
  depthSlider_ = slider;
  refreshFromProcessor();
}
void ObjectStereoJuceFrontendConnector::setDiffuseSlider(std::shared_ptr<EarSlider> slider) {
  slider->addListener(this);
  diffuseSlider_ = slider;
  refreshFromProcessor();
}

void ObjectStereoJuceFrontendConnector::updateLinkSizeEnablement() {
  auto linkSizeButtonLocked = linkSizeButton_.lock();
  auto sizeLabelLocked = sizeLabel_.lock();
  auto sizeSliderLocked = sizeSlider_.lock();
  auto widthLabelLocked = widthLabel_.lock();
  auto widthSliderLocked = widthSlider_.lock();
  auto heightLabelLocked = heightLabel_.lock();
  auto heightSliderLocked = heightSlider_.lock();
  auto depthLabelLocked = depthLabel_.lock();
  auto depthSliderLocked = depthSlider_.lock();
  if (!(linkSizeButtonLocked && sizeLabelLocked && sizeSliderLocked && widthLabelLocked &&
       widthSliderLocked && heightLabelLocked && heightSliderLocked && depthLabelLocked &&
       depthSliderLocked))
    return;

  bool linkSize = p_->getLinkSize();
  linkSizeButtonLocked->setToggleState(linkSize, dontSendNotification);
  sizeLabelLocked->setAlpha(linkSize ? 1.f : Emphasis::disabled);
  sizeSliderLocked->setAlpha(linkSize ? 1.f : Emphasis::disabled);
  sizeSliderLocked->setEnabled(linkSize);
  widthLabelLocked->setAlpha(linkSize ? Emphasis::disabled : 1.f);
  widthSliderLocked->setAlpha(linkSize ? Emphasis::disabled : 1.f);
  widthSliderLocked->setEnabled(!linkSize);
  heightLabelLocked->setAlpha(linkSize ? Emphasis::disabled : 1.f);
  heightSliderLocked->setAlpha(linkSize ? Emphasis::disabled : 1.f);
  heightSliderLocked->setEnabled(!linkSize);
  depthLabelLocked->setAlpha(linkSize ? Emphasis::disabled : 1.f);
  depthSliderLocked->setAlpha(linkSize ? Emphasis::disabled : 1.f);
  depthSliderLocked->setEnabled(!linkSize);
}

void ObjectStereoJuceFrontendConnector::sliderValueChanged(Slider* slider) {
  if (auto s = lockIfSame(gainSlider_, slider)) { p_->setGainDb(static_cast<float>(s->getValue())); return; }
  if (auto s = lockIfSame(azimuthSlider_, slider)) { p_->setAzimuth(static_cast<float>(s->getValue())); return; }
  if (auto s = lockIfSame(elevationSlider_, slider)) { p_->setElevation(static_cast<float>(s->getValue())); return; }
  if (auto s = lockIfSame(distanceSlider_, slider)) { p_->setDistance(static_cast<float>(s->getValue())); return; }
  if (auto s = lockIfSame(pairDistanceSlider_, slider)) { p_->setPairDistance(static_cast<float>(s->getValue())); return; }
  if (auto s = lockIfSame(pairRotationSlider_, slider)) { p_->setPairRotation(static_cast<float>(s->getValue())); return; }
  if (auto s = lockIfSame(sizeSlider_, slider)) { p_->setSize(static_cast<float>(s->getValue())); updateLinkSizeEnablement(); return; }
  if (auto s = lockIfSame(widthSlider_, slider)) { p_->setWidth(static_cast<float>(s->getValue())); return; }
  if (auto s = lockIfSame(heightSlider_, slider)) { p_->setHeight(static_cast<float>(s->getValue())); return; }
  if (auto s = lockIfSame(depthSlider_, slider)) { p_->setDepth(static_cast<float>(s->getValue())); return; }
  if (auto s = lockIfSame(diffuseSlider_, slider)) { p_->setDiffuse(static_cast<float>(s->getValue())); return; }
}

void ObjectStereoJuceFrontendConnector::pannerValueChanged(PannerTopView* panner) {
  if (lockIfSame(pannerTopView_, panner)) {
    p_->setAzimuth(panner->getAzimuth());
    p_->setDistance(panner->getDistance());
    if (auto s = distanceSlider_.lock()) s->setValue(panner->getDistance(), dontSendNotification);
    if (auto s = azimuthSlider_.lock()) s->setValue(panner->getAzimuth(), dontSendNotification);
  }
}

void ObjectStereoJuceFrontendConnector::pannerValueChanged(PannerSideView* panner) {
  if (lockIfSame(pannerSideView_, panner)) {
    p_->setElevation(panner->getElevation());
    if (auto s = elevationSlider_.lock()) s->setValue(panner->getElevation(), dontSendNotification);
  }
}

void ObjectStereoJuceFrontendConnector::comboBoxChanged(EarComboBox* comboBox) {
  if (auto locked = lockIfSame(routingAComboBox_, comboBox)) {
    p_->setRoutingA(comboBox->getSelectedEntryIndex());
  } else if (auto lockedB = lockIfSame(routingBComboBox_, comboBox)) {
    p_->setRoutingB(comboBox->getSelectedEntryIndex());
  }
}

void ObjectStereoJuceFrontendConnector::buttonClicked(Button* button) {
  if (auto locked = lockIfSame(linkSizeButton_, button)) {
    p_->setLinkSize(!locked->getToggleState());
    updateLinkSizeEnablement();
  } else if (auto lockedU = lockIfSame(useTrackNameCheckbox_, button)) {
    p_->setUseTrackName(!lockedU->getToggleState());
    refreshFromProcessor();
  }
}

void ObjectStereoJuceFrontendConnector::textEditorTextChanged(TextEditor& textEditor) {
  if (auto locked = lockIfSame(nameTextEditor_, &textEditor)) {
    p_->setName(locked->getText().toStdString());
  }
}

}  // namespace ui
}  // namespace plugin
}  // namespace ear
