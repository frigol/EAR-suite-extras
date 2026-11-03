set(SOURCES_MONITORING_CBA
    ${EPS_SHARED_DIR}/binary_data.cpp
    src/monitoring_cba_plugin_editor.cpp
    src/monitoring_cba_plugin_processor.cpp
    src/monitoring_cba_scene_view.cpp
    src/monitoring_cba_add_items_popup.cpp
)

set(HEADERS_MONITORING_CBA
	${EPS_PLUGIN_BASE_DIR}/include/communication/common_types.hpp
	${EPS_PLUGIN_BASE_DIR}/include/communication/monitoring_control_connection.hpp
	${EPS_PLUGIN_BASE_DIR}/include/communication/monitoring_metadata_receiver.hpp
	${EPS_PLUGIN_BASE_DIR}/include/detail/constants.hpp
	${EPS_PLUGIN_BASE_DIR}/include/helper/custom_layout_file.hpp

	${EPS_SHARED_DIR}/binary_data.hpp
	${EPS_SHARED_DIR}/components/ear_button.hpp
	${EPS_SHARED_DIR}/components/look_and_feel/colours.hpp
	${EPS_SHARED_DIR}/components/look_and_feel/fonts.hpp

	src/monitoring_cba_plugin_editor.hpp
	src/monitoring_cba_plugin_processor.hpp
	src/monitoring_cba_scene_view.hpp
	src/monitoring_cba_add_items_popup.hpp
)

source_group("Header Files" FILES ${HEADERS_MONITORING_CBA})

# A self-contained Object-to-channel-based-audio (CBA) decoder: lets the
# user pick specific EAR Objects (same "+ Item" picker as "Reverb for EAR
# Suite" - no need for EAR Scene's own render path to be in the session at
# all, only its metadata broadcast for live object position) and decodes
# them straight to a standard ITU/BS.2051 loudspeaker layout (or a Custom
# Monitoring-style loaded layout file), via libear's own public
# ear::GainCalculatorObjects - the same gain law EAR Scene/Monitoring use
# internally. Meant to sit before a traditional, EAR-unaware
# channel-based-audio reverb (even a 7.1.4 one); feed that reverb's own
# channel outputs into a "LF Multi-Encoder" instance afterwards
# to bring them back into the ADM/Scene pipeline for export.
add_juce_vst3_plugin(monitoring_cba
  SOURCES ${SOURCES_MONITORING_CBA} ${HEADERS_MONITORING_CBA}
  IDE_FOLDER ${IDE_FOLDER_PLUGINS}
  CODE_SUFFIX "24"
  DISPLAY_NAME "LF Decoder"
  DESCRIPTION "Decodes picked EAR Objects straight to channel-based audio (standard ITU/BS.2051 layouts or a loaded custom layout) - no EAR Scene render path needed. Meant to sit before a traditional CBA reverb. Not affiliated with or endorsed by EBU."
  OUTPUT_NAME "LF Decoder"
  MANUFACTURER_NAME "Luca Frigo"
  MANUFACTURER_WEBSITE "https://github.com/frigol/ear-suite-extras"
  MANUFACTURER_CODE "4C462020" # Hex for 'LF  '
  BUNDLE_ID_DOMAIN "io.lucafrigo"
  BUNDLE_ID_PREFIX "io.lucafrigo"
  COPYRIGHT "(C) 2026 Luca Frigo"
  IS_EPS_PLUGIN ON)

target_include_directories(monitoring_cba_VST3 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(monitoring_cba_VST3 PRIVATE ear-plugin-base ear-version ear)
install_juce_vst3_plugin(monitoring_cba "${EPS_PLUGIN_INSTALL_PREFIX}ear-production-suite")
