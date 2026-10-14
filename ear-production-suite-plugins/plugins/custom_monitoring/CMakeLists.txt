set(SOURCES_CUSTOM_MONITORING
    ${EPS_SHARED_DIR}/binary_data.cpp
    ${EPS_SHARED_DIR}/components/level_meter_calculator.cpp
    src/custom_monitoring_plugin_editor.cpp
    src/custom_monitoring_plugin_processor.cpp
)

set(HEADERS_CUSTOM_MONITORING
	${EPS_PLUGIN_BASE_DIR}/include/detail/constants.hpp
	${EPS_PLUGIN_BASE_DIR}/include/monitoring_audio_processor.hpp
	${EPS_PLUGIN_BASE_DIR}/include/monitoring_backend.hpp
	${EPS_PLUGIN_BASE_DIR}/include/variable_block_adapter.hpp
	${EPS_PLUGIN_BASE_DIR}/include/helper/custom_layout_file.hpp

	${EPS_SHARED_DIR}/binary_data.hpp

	${EPS_SHARED_DIR}/components/ear_button.hpp
	${EPS_SHARED_DIR}/components/ear_header.hpp
	${EPS_SHARED_DIR}/components/level_meter.hpp
	${EPS_SHARED_DIR}/components/level_meter_calculator.hpp
	${EPS_SHARED_DIR}/components/onboarding.hpp
	${EPS_SHARED_DIR}/components/onboarding_slide.hpp
	${EPS_SHARED_DIR}/components/overlay.hpp
	${EPS_SHARED_DIR}/components/segment_progress_bar.hpp
	${EPS_SHARED_DIR}/components/version_label.hpp

	${EPS_SHARED_DIR}/components/look_and_feel/colours.hpp
	${EPS_SHARED_DIR}/components/look_and_feel/fonts.hpp
	${EPS_SHARED_DIR}/components/look_and_feel/shadows.hpp
	${EPS_SHARED_DIR}/components/look_and_feel/slider.hpp

	${EPS_SHARED_DIR}/helper/graphics.hpp
	${EPS_SHARED_DIR}/helper/properties_file.hpp

	src/custom_monitoring_plugin_editor.hpp
	src/custom_monitoring_plugin_processor.hpp
	src/speaker_meter.hpp
	src/speaker_meter_grid.hpp
)

source_group("Header Files" FILES ${HEADERS_CUSTOM_MONITORING})

add_juce_vst3_plugin(
  ear_custom_monitoring
  SOURCES ${SOURCES_CUSTOM_MONITORING} ${HEADERS_CUSTOM_MONITORING}
  CODE_SUFFIX "AB" # Monitoring suffixes are A0-AA; binaural is F0; scene is FF
  DISPLAY_NAME "Custom Monitoring for EAR Suite"
  DESCRIPTION "Unofficial fork of EAR Monitoring that decodes to a loudspeaker layout loaded from a text file. Not affiliated with or endorsed by EBU."
  OUTPUT_NAME "Custom Monitoring for EAR Suite"
  MANUFACTURER_NAME "Luca Frigo"
  MANUFACTURER_WEBSITE "https://github.com/frigol/ear-suite-extras"
  MANUFACTURER_CODE "4C462020" # Hex for 'LF  '
  BUNDLE_ID_DOMAIN "io.lucafrigo"
  BUNDLE_ID_PREFIX "io.lucafrigo"
  COPYRIGHT "(C) 2019-2023 BBC, (C) 2020-2021 EBU, (C) 2019-2021 IRT. Modified portions (C) 2026 Luca Frigo."
  IDE_FOLDER ${IDE_FOLDER_PLUGINS}
  IS_EPS_PLUGIN ON)

target_link_libraries(ear_custom_monitoring_VST3 PRIVATE ear-plugin-base ear-version)
install_juce_vst3_plugin(ear_custom_monitoring "${EPS_PLUGIN_INSTALL_PREFIX}ear-production-suite")
