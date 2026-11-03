#include "matrix_router_plugin_editor.hpp"

namespace matrix_router {

MatrixRouterAudioProcessorEditor::MatrixRouterAudioProcessorEditor(
    MatrixRouterAudioProcessor* p)
    : AudioProcessorEditor(p), processor_(*p) {
  titleLabel_.setText("Matrix Router", dontSendNotification);
  titleLabel_.setFont(Font(18.0f, Font::bold));
  titleLabel_.setColour(Label::textColourId, Colours::white);
  addAndMakeVisible(titleLabel_);

  inputsLabel_.setText("Inputs", dontSendNotification);
  inputsLabel_.setColour(Label::textColourId, Colours::white);
  addAndMakeVisible(inputsLabel_);

  inputsSlider_.setSliderStyle(Slider::IncDecButtons);
  inputsSlider_.setRange(1, kMaxChannels, 1);
  inputsSlider_.setValue(processor_.numInputs(), dontSendNotification);
  inputsSlider_.setTextBoxStyle(Slider::TextBoxLeft, false, 50, 22);
  inputsSlider_.onValueChange = [this]() {
    processor_.setNumInputs(static_cast<int>(inputsSlider_.getValue()));
  };
  addAndMakeVisible(inputsSlider_);

  outputsLabel_.setText("Outputs", dontSendNotification);
  outputsLabel_.setColour(Label::textColourId, Colours::white);
  addAndMakeVisible(outputsLabel_);

  outputsSlider_.setSliderStyle(Slider::IncDecButtons);
  outputsSlider_.setRange(1, kMaxChannels, 1);
  outputsSlider_.setValue(processor_.numOutputs(), dontSendNotification);
  outputsSlider_.setTextBoxStyle(Slider::TextBoxLeft, false, 50, 22);
  outputsSlider_.onValueChange = [this]() {
    processor_.setNumOutputs(static_cast<int>(outputsSlider_.getValue()));
  };
  addAndMakeVisible(outputsSlider_);

  clearAllButton_.setButtonText("Clear all");
  clearAllButton_.onClick = [this]() { processor_.clearAllCrosspoints(); };
  addAndMakeVisible(clearAllButton_);

  grid_ = std::make_unique<MatrixGridComponent>(processor_);
  gridViewport_.setViewedComponent(grid_.get(), false);
  gridViewport_.setScrollBarsShown(true, true);
  addAndMakeVisible(gridViewport_);

  processor_.onMatrixChanged = [this]() { refreshFromProcessor(); };

  setSize(700, 500);
  setResizable(true, true);
  setResizeLimits(400, 300, 2000, 2000);
}

MatrixRouterAudioProcessorEditor::~MatrixRouterAudioProcessorEditor() {
  processor_.onMatrixChanged = nullptr;
}

void MatrixRouterAudioProcessorEditor::refreshFromProcessor() {
  inputsSlider_.setValue(processor_.numInputs(), dontSendNotification);
  outputsSlider_.setValue(processor_.numOutputs(), dontSendNotification);
  grid_->refreshSize();
}

void MatrixRouterAudioProcessorEditor::paint(Graphics& g) {
  g.fillAll(Colour(0xff222222));
}

void MatrixRouterAudioProcessorEditor::resized() {
  auto area = getLocalBounds().reduced(10);
  titleLabel_.setBounds(area.removeFromTop(28));
  area.removeFromTop(6);

  auto controlsArea = area.removeFromTop(30);
  inputsLabel_.setBounds(controlsArea.removeFromLeft(50));
  inputsSlider_.setBounds(controlsArea.removeFromLeft(110));
  controlsArea.removeFromLeft(20);
  outputsLabel_.setBounds(controlsArea.removeFromLeft(55));
  outputsSlider_.setBounds(controlsArea.removeFromLeft(110));
  controlsArea.removeFromLeft(20);
  clearAllButton_.setBounds(controlsArea.removeFromLeft(90));

  area.removeFromTop(10);
  gridViewport_.setBounds(area);
}

}  // namespace matrix_router
