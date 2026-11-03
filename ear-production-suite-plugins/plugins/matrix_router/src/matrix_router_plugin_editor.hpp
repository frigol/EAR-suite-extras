#pragma once

#include "JuceHeader.h"
#include "matrix_grid_component.hpp"
#include "matrix_router_plugin_processor.hpp"

namespace matrix_router {

class MatrixRouterAudioProcessorEditor : public AudioProcessorEditor {
 public:
  explicit MatrixRouterAudioProcessorEditor(MatrixRouterAudioProcessor*);
  ~MatrixRouterAudioProcessorEditor() override;

  void paint(Graphics&) override;
  void resized() override;

 private:
  MatrixRouterAudioProcessor& processor_;

  Label titleLabel_;
  Label inputsLabel_;
  Slider inputsSlider_;
  Label outputsLabel_;
  Slider outputsSlider_;
  TextButton clearAllButton_;

  Viewport gridViewport_;
  std::unique_ptr<MatrixGridComponent> grid_;

  void refreshFromProcessor();

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MatrixRouterAudioProcessorEditor)
};

}  // namespace matrix_router
