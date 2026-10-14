set(SOURCES_OBJECT_OSC_INPUT
    ${EPS_SHARED_DIR}/binary_data.cpp
    ${EPS_SHARED_DIR}/components/look_and_feel/slider.cpp
    ${EPS_SHARED_DIR}/components/look_and_feel/name_text_editor.cpp
    ${EPS_SHARED_DIR}/components/ear_combo_box.cpp
    ${EPS_SHARED_DIR}/components/ear_header.cpp
    ${EPS_SHARED_DIR}/components/ear_slider_label.cpp
    ${EPS_SHARED_DIR}/components/level_meter_calculator.cpp
    src/object_frontend_connector.cpp
    src/object_plugin_editor.cpp
    src/object_plugin_processor.cpp
)

set(HEADERS_OBJECT_OSC_INPUT
	${EPS_PLUGIN_BASE_DIR}/include/communication/common_types.hpp
	${EPS_PLUGIN_BASE_DIR}/include/detail/constants.hpp
	${EPS_PLUGIN_BASE_DIR}/include/object_backend.hpp
	${EPS_PLUGIN_BASE_DIR}/include/ui/object_frontend_backend_connector.hpp

	${EPS_SHARED_DIR}/binary_data.hpp

	${EPS_SHARED_DIR}/components/ear_button.hpp
	${EPS_SHARED_DIR}/components/ear_combo_box.hpp
	${EPS_SHARED_DIR}/components/ear_header.hpp
	${EPS_SHARED_DIR}/components/ear_inverted_slider.hpp
	${EPS_SHARED_DIR}/components/ear_name_text_editor.hpp
	${EPS_SHARED_DIR}/components/ear_slider.hpp
	${EPS_SHARED_DIR}/components/ear_slider_label.hpp
	${EPS_SHARED_DIR}/components/level_meter.hpp
	${EPS_SHARED_DIR}/components/level_meter_calculator.hpp
	${EPS_SHARED_DIR}/components/non_automatable_parameter.hpp
	${EPS_SHARED_DIR}/components/onboarding.hpp
	${EPS_SHARED_DIR}/components/onboarding_slide.hpp
	${EPS_SHARED_DIR}/components/overlay.hpp
	${EPS_SHARED_DIR}/components/panner_side_view.hpp
	${EPS_SHARED_DIR}/components/panner_top_view.hpp
	${EPS_SHARED_DIR}/components/routing_info_icon.hpp
	${EPS_SHARED_DIR}/components/segment_progress_bar.hpp
	${EPS_SHARED_DIR}/components/version_label.hpp

	${EPS_SHARED_DIR}/components/look_and_feel/colours.hpp
	${EPS_SHARED_DIR}/components/look_and_feel/fonts.hpp
	${EPS_SHARED_DIR}/components/look_and_feel/name_text_editor.hpp
	${EPS_SHARED_DIR}/components/look_and_feel/roboto.hpp
	${EPS_SHARED_DIR}/components/look_and_feel/shadows.hpp
	${EPS_SHARED_DIR}/components/look_and_feel/slider.hpp
	${EPS_SHARED_DIR}/components/look_and_feel/tooltips.hpp

	${EPS_SHARED_DIR}/helper/graphics.hpp
	${EPS_SHARED_DIR}/helper/iso_lang_codes.hpp
	${EPS_SHARED_DIR}/helper/multi_async_updater.h
	${EPS_SHARED_DIR}/helper/properties_file.hpp

	src/object_component.hpp
	src/object_frontend_connector.hpp
	src/object_plugin_editor.hpp
	src/object_plugin_processor.hpp
	src/osc_port_registry.hpp
	src/value_box_extent.hpp
	src/value_box_gain.hpp
	src/value_box_main.hpp
	src/value_box_metadata.hpp
	src/value_box_panning.hpp
	src/value_box_panning_view.hpp
)

source_group("Header Files" FILES ${HEADERS_OBJECT_OSC_INPUT})

# Fork of plugins/object with OSC remote control added (see the long
# comment on ObjectsAudioProcessor::oscSourceIndex_) - kept as a separate
# plugin, rather than added to plugins/object directly, so the stock EAR
# Object plugin stays byte-for-byte what ships with upstream EAR
# Production Suite, installable side by side with this one. Controls
# Azimuth/Elevation (ControlGRIS2's xyz) and Width/Height (ControlGRIS2's
# azimuth/elevation span), calibrated against live OSC captures.
add_juce_vst3_plugin(object_osc
  SOURCES ${SOURCES_OBJECT_OSC_INPUT} ${HEADERS_OBJECT_OSC_INPUT}
  IDE_FOLDER ${IDE_FOLDER_PLUGINS}
  CODE_SUFFIX "13"
  DISPLAY_NAME "Object OSC for EAR Suite"
  DESCRIPTION "Unofficial fork (based on EAR Production Suite 1.2.2) of EAR Object with OSC remote control (own addresses + ControlGRIS2's xyz/span). Not affiliated with or endorsed by EBU."
  OUTPUT_NAME "Object OSC for EAR Suite"
  MANUFACTURER_NAME "Luca Frigo"
  MANUFACTURER_WEBSITE "https://github.com/frigol/ear-suite-extras"
  MANUFACTURER_CODE "4C462020" # Hex for 'LF  '
  BUNDLE_ID_DOMAIN "io.lucafrigo"
  BUNDLE_ID_PREFIX "io.lucafrigo"
  COPYRIGHT "(C) 2019-2023 BBC, (C) 2020-2021 EBU, (C) 2019-2021 IRT. Modified portions (C) 2026 Luca Frigo."
  IS_EPS_PLUGIN ON)

target_include_directories(object_osc_VST3 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(object_osc_VST3 PRIVATE ear-plugin-base ear-version)
install_juce_vst3_plugin(object_osc "${EPS_PLUGIN_INSTALL_PREFIX}ear-production-suite")
