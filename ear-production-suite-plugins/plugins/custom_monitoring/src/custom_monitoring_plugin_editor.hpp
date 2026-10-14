#pragma once

#include "JuceHeader.h"

#include "components/ear_button.hpp"
#include "components/ear_header.hpp"
#include "components/level_meter.hpp"
#include "components/onboarding.hpp"
#include "components/overlay.hpp"
#include "custom_monitoring_plugin_processor.hpp"
#include "speaker_meter_grid.hpp"

class EarCustomMonitoringAudioProcessorEditor
    : public AudioProcessorEditor,
      private ear::plugin::ui::Onboarding::Listener {
 public:
  EarCustomMonitoringAudioProcessorEditor(EarCustomMonitoringAudioProcessor*);
  ~EarCustomMonitoringAudioProcessorEditor();

  void paint(Graphics&) override;
  void resized() override;

 private:
  EarCustomMonitoringAudioProcessor* p_;

  std::unique_ptr<ear::plugin::ui::EarHeader> header_;
  std::unique_ptr<ear::plugin::ui::EarButton> onBoardingButton_;
  std::unique_ptr<ear::plugin::ui::Overlay> onBoardingOverlay_;
  std::unique_ptr<ear::plugin::ui::Onboarding> onBoardingContent_;

  std::unique_ptr<ear::plugin::ui::EarButton> loadLayoutButton_;
  Label layoutFileLabel_;

  std::unique_ptr<ear::plugin::ui::SpeakerMeterGrid> speakerMeterGrid_;
  std::unique_ptr<FileChooser> fileChooser_;

  std::unique_ptr<InterProcessLock> propertiesFileLock_;
  std::unique_ptr<PropertiesFile> propertiesFile_;

  Label versionLabel;
  // Unofficial-fork disclosure, not part of upstream EAR Production Suite:
  // paired with versionLabel in the same bottom bar (that one right-
  // justified, this one left-justified) so it's always visible without
  // competing with the main EarHeader title for space.
  Label byLabel_;

  void rebuildSpeakerMeters();
  void promptLoadLayout();

  // --- Onboarding::Listener
  void endButtonClicked(ear::plugin::ui::Onboarding* onboarding) override;
  void moreButtonClicked(ear::plugin::ui::Onboarding* onboarding) override;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
      EarCustomMonitoringAudioProcessorEditor)
};
