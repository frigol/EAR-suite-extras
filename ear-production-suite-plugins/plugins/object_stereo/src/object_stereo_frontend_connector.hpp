#pragma once

#include "JuceHeader.h"

#include "components/ear_button.hpp"
#include "components/ear_slider.hpp"
#include "components/ear_combo_box.hpp"
#include "components/panner_top_view.hpp"
#include "components/panner_side_view.hpp"
#include "object_stereo_plugin_processor.hpp"
#include <memory>

namespace ear {
namespace plugin {
namespace ui {

class EarComboBox;
class EarNameTextEditor;
class PannerTopView;
class PannerSideView;
class EarInvertedSlider;

/** @brief Binds the shared/group UI widgets (one centre position, one pair
 * of routing selectors, Pair Distance/Rotation, extent/diffuse) directly to
 * ObjectStereoAudioProcessor's plain setter methods - there is no
 * AudioProcessorParameter::Listener here (unlike the stock Object plugin's
 * connector): ObjectStereoAudioProcessor's own parameters are reserved
 * entirely for reaper-adm-extension's export contract (see its class
 * comment), so every control here is plain state, read/written straight
 * through the processor's getters/setters. */
class ObjectStereoJuceFrontendConnector
    : private TextEditor::Listener,
      private Slider::Listener,
      private Button::Listener,
      private ear::plugin::ui::PannerTopView::Listener,
      private ear::plugin::ui::PannerSideView::Listener,
      private ear::plugin::ui::EarComboBox::Listener {
 public:
  ObjectStereoJuceFrontendConnector(ObjectStereoAudioProcessor* processor);
  ~ObjectStereoJuceFrontendConnector();

  /** @brief Re-syncs every bound widget from the processor's current state -
   * call after a state load or a track-properties change (name/colour). */
  void refreshFromProcessor();

  // Main Value Box
  void setStatusBarLabel(std::shared_ptr<Label> statusBarLabel);
  void setNameTextEditor(std::shared_ptr<EarNameTextEditor> nameTextEditor);
  void setUseTrackNameCheckbox(std::shared_ptr<ToggleButton> useTrackNameCheckbox);
  void setColourComboBox(std::shared_ptr<EarComboBox> colourComboBox);
  void setRoutingAComboBox(std::shared_ptr<EarComboBox> routingComboBox);
  void setRoutingBComboBox(std::shared_ptr<EarComboBox> routingComboBox);

  // Panning Value Box
  void setGainSlider(std::shared_ptr<EarSlider> gainSlider);
  void setAzimuthSlider(std::shared_ptr<EarInvertedSlider> azimuthSlider);
  void setElevationSlider(std::shared_ptr<EarSlider> elevationSlider);
  void setDistanceSlider(std::shared_ptr<EarSlider> distanceSlider);
  void setPairDistanceSlider(std::shared_ptr<EarSlider> slider);
  void setPairRotationSlider(std::shared_ptr<EarSlider> slider);

  // Extent Value Box
  void setLinkSizeButton(std::shared_ptr<EarButton> button);
  void setSizeLabel(std::shared_ptr<Label> label);
  void setSizeSlider(std::shared_ptr<EarSlider> slider);
  void setWidthLabel(std::shared_ptr<Label> label);
  void setWidthSlider(std::shared_ptr<EarSlider> slider);
  void setHeightLabel(std::shared_ptr<Label> label);
  void setHeightSlider(std::shared_ptr<EarSlider> slider);
  void setDepthLabel(std::shared_ptr<Label> label);
  void setDepthSlider(std::shared_ptr<EarSlider> slider);
  void setDiffuseSlider(std::shared_ptr<EarSlider> slider);

  // Panning View Value Box
  void setPannerTopView(std::shared_ptr<PannerTopView> panner);
  void setPannerSideView(std::shared_ptr<PannerSideView> panner);

 protected:
  void sliderValueChanged(Slider* slider) override;
  void sliderDragStarted(Slider*) override {}
  void sliderDragEnded(Slider*) override {}

  void pannerValueChanged(ear::plugin::ui::PannerTopView* panner) override;
  void pannerDragStarted(ear::plugin::ui::PannerTopView*) override {}
  void pannerDragEnded(ear::plugin::ui::PannerTopView*) override {}

  void pannerValueChanged(ear::plugin::ui::PannerSideView* panner) override;
  void pannerDragStarted(ear::plugin::ui::PannerSideView*) override {}
  void pannerDragEnded(ear::plugin::ui::PannerSideView*) override {}

  void comboBoxChanged(
      ear::plugin::ui::EarComboBox* comboBoxThatHasChanged) override;

  void buttonClicked(Button*) override;
  void textEditorTextChanged(TextEditor&) override;

 private:
  void updateLinkSizeEnablement();

  ObjectStereoAudioProcessor* p_;

  std::weak_ptr<EarComboBox> colourComboBox_;
  std::weak_ptr<EarComboBox> routingAComboBox_;
  std::weak_ptr<EarComboBox> routingBComboBox_;
  std::weak_ptr<EarNameTextEditor> nameTextEditor_;
  std::weak_ptr<ToggleButton> useTrackNameCheckbox_;

  std::weak_ptr<EarSlider> gainSlider_;
  std::weak_ptr<EarInvertedSlider> azimuthSlider_;
  std::weak_ptr<EarSlider> elevationSlider_;
  std::weak_ptr<EarSlider> distanceSlider_;
  std::weak_ptr<EarSlider> pairDistanceSlider_;
  std::weak_ptr<EarSlider> pairRotationSlider_;

  std::weak_ptr<EarButton> linkSizeButton_;
  std::weak_ptr<Label> sizeLabel_;
  std::weak_ptr<EarSlider> sizeSlider_;
  std::weak_ptr<Label> widthLabel_;
  std::weak_ptr<EarSlider> widthSlider_;
  std::weak_ptr<Label> heightLabel_;
  std::weak_ptr<EarSlider> heightSlider_;
  std::weak_ptr<Label> depthLabel_;
  std::weak_ptr<EarSlider> depthSlider_;
  std::weak_ptr<EarSlider> diffuseSlider_;

  std::weak_ptr<PannerTopView> pannerTopView_;
  std::weak_ptr<PannerSideView> pannerSideView_;

  std::weak_ptr<Label> statusBar_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ObjectStereoJuceFrontendConnector)
};

}  // namespace ui
}  // namespace plugin
}  // namespace ear
