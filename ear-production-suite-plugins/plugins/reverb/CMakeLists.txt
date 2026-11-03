set(SOURCES_REVERB
    ${EPS_SHARED_DIR}/binary_data.cpp
    src/reverb_add_items_popup.cpp
    src/reverb_dsp.cpp
    src/reverb_plugin_editor.cpp
    src/reverb_plugin_processor.cpp
    src/reverb_point_controls_row.cpp
    src/reverb_scene_view.cpp
    src/reverb_tail_engine.cpp
    src/reverb_time_structure_view.cpp
)

set(HEADERS_REVERB
	${EPS_PLUGIN_BASE_DIR}/include/communication/common_types.hpp
	${EPS_PLUGIN_BASE_DIR}/include/communication/monitoring_control_connection.hpp
	${EPS_PLUGIN_BASE_DIR}/include/communication/monitoring_metadata_receiver.hpp
	${EPS_PLUGIN_BASE_DIR}/include/detail/constants.hpp
	${EPS_PLUGIN_BASE_DIR}/include/object_backend.hpp
	${EPS_PLUGIN_BASE_DIR}/include/ui/object_frontend_backend_connector.hpp

	${EPS_SHARED_DIR}/binary_data.hpp
	${EPS_SHARED_DIR}/components/ear_button.hpp
	${EPS_SHARED_DIR}/components/look_and_feel/colours.hpp
	${EPS_SHARED_DIR}/components/look_and_feel/fonts.hpp

	src/reverb_add_items_popup.hpp
	src/reverb_dsp.hpp
	src/reverb_plugin_editor.hpp
	src/reverb_plugin_processor.hpp
	src/reverb_point_connector.hpp
	src/reverb_point_controls_row.hpp
	src/reverb_scene_view.hpp
	src/reverb_tail_engine.hpp
	src/reverb_time_structure_view.hpp
	third_party/MVerb.h
)

source_group("Header Files" FILES ${HEADERS_REVERB})

# New plugin (not a fork of an existing one): takes mono sends from existing
# Object/Object OSC tracks, discovers them by subscribing to Scene's own PUB
# broadcast (the same stream every EAR Monitoring instance already reads -
# see communication::MonitoringControlConnection/MonitoringMetadataReceiver,
# reused here unchanged, no changes needed to Object OSC or Scene), and
# outputs 9 channels - one per ITU-R BS.2051 "4+5+0" speaker position - each
# independently registered with Scene as its own ADM Object via its own
# ear::plugin::ObjectBackend connection with its own "Select Scene channel"
# routing. Phase 1: plumbing only, no reverb DSP yet (see reverb_plugin_
# processor.cpp's processBlock for the current placeholder).
add_juce_vst3_plugin(reverb
  SOURCES ${SOURCES_REVERB} ${HEADERS_REVERB}
  IDE_FOLDER ${IDE_FOLDER_PLUGINS}
  CODE_SUFFIX "21"
  DISPLAY_NAME "LF Reverb"
  DESCRIPTION "Position-following spatial reverb: reverberates a selected EAR Object/Object OSC and sends 10 points (4+6+0 layout) to EAR Scene as independent objects. Not affiliated with or endorsed by EBU."
  OUTPUT_NAME "LF Reverb"
  MANUFACTURER_NAME "Luca Frigo"
  MANUFACTURER_WEBSITE "https://github.com/frigol/ear-suite-extras"
  MANUFACTURER_CODE "4C462020" # Hex for 'LF  '
  BUNDLE_ID_DOMAIN "io.lucafrigo"
  BUNDLE_ID_PREFIX "io.lucafrigo"
  COPYRIGHT "(C) 2026 Luca Frigo"
  IS_EPS_PLUGIN ON)

target_include_directories(reverb_VST3 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(reverb_VST3 PRIVATE ear-plugin-base ear-version ear)
install_juce_vst3_plugin(reverb "${EPS_PLUGIN_INSTALL_PREFIX}ear-production-suite")
