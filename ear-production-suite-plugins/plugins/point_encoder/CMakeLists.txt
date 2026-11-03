set(SOURCES_POINT_ENCODER
    ${EPS_SHARED_DIR}/binary_data.cpp
    src/point_encoder_plugin_editor.cpp
    src/point_encoder_plugin_processor.cpp
    src/point_encoder_point_controls_row.cpp
    src/point_encoder_scene_view.cpp
)

set(HEADERS_POINT_ENCODER
	${EPS_PLUGIN_BASE_DIR}/include/communication/common_types.hpp
	${EPS_PLUGIN_BASE_DIR}/include/detail/constants.hpp
	${EPS_PLUGIN_BASE_DIR}/include/helper/custom_layout_file.hpp
	${EPS_PLUGIN_BASE_DIR}/include/object_backend.hpp
	${EPS_PLUGIN_BASE_DIR}/include/ui/object_frontend_backend_connector.hpp

	${EPS_SHARED_DIR}/binary_data.hpp
	${EPS_SHARED_DIR}/components/ear_button.hpp
	${EPS_SHARED_DIR}/components/look_and_feel/colours.hpp
	${EPS_SHARED_DIR}/components/look_and_feel/fonts.hpp
	${EPS_SHARED_DIR}/components/read_only_audio_parameter_int.hpp

	src/point_encoder_plugin_editor.hpp
	src/point_encoder_plugin_processor.hpp
	src/point_encoder_point_connector.hpp
	src/point_encoder_point_controls_row.hpp
	src/point_encoder_scene_view.hpp
)

source_group("Header Files" FILES ${HEADERS_POINT_ENCODER})

# A generic N-channel-in / N-Object-out encoder: each of up to kMaxPoints
# plain audio input channels can be independently activated as its own ADM
# Object sent to EAR Scene (full Object-style parameter set - position,
# gain, width/height/depth, diffuse), with a free choice of which input
# channel feeds it. Lets several "LF Reverb"/"IR Reverb for EAR
# Suite" instances (both pure DSP engines with no Scene registration of
# their own) share one set of Scene-registered Objects instead of each
# registering its own 10, avoiding REAPER's 128-channel routing cap when
# several reverb instances are active in one session - see
# point_encoder_plugin_processor.hpp's own class comment for the full
# reasoning.
add_juce_vst3_plugin(point_encoder
  SOURCES ${SOURCES_POINT_ENCODER} ${HEADERS_POINT_ENCODER}
  IDE_FOLDER ${IDE_FOLDER_PLUGINS}
  CODE_SUFFIX "23"
  DISPLAY_NAME "LF Multi-Encoder"
  DESCRIPTION "Encodes up to 24 plain audio channels as independent ADM Objects sent to EAR Scene - lets several LF Reverb/LF IR Reverb instances share one set of Scene-registered points instead of each registering their own. Not affiliated with or endorsed by EBU."
  OUTPUT_NAME "LF Multi-Encoder"
  MANUFACTURER_NAME "Luca Frigo"
  MANUFACTURER_WEBSITE "https://github.com/frigol/ear-suite-extras"
  MANUFACTURER_CODE "4C462020" # Hex for 'LF  '
  BUNDLE_ID_DOMAIN "io.lucafrigo"
  BUNDLE_ID_PREFIX "io.lucafrigo"
  COPYRIGHT "(C) 2026 Luca Frigo"
  IS_EPS_PLUGIN ON)

target_include_directories(point_encoder_VST3 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(point_encoder_VST3 PRIVATE ear-plugin-base ear-version ear)
install_juce_vst3_plugin(point_encoder "${EPS_PLUGIN_INSTALL_PREFIX}ear-production-suite")
