set(SOURCES_IR_REVERB
    ${EPS_SHARED_DIR}/binary_data.cpp
    src/ir_reverb_dsp.cpp
    src/ir_reverb_plugin_editor.cpp
    src/ir_reverb_plugin_processor.cpp
    src/ir_reverb_point_controls_row.cpp
    src/ir_reverb_scene_view.cpp
    src/ir_reverb_add_items_popup.cpp
)

set(HEADERS_IR_REVERB
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

	src/ir_reverb_dsp.hpp
	src/ir_reverb_plugin_editor.hpp
	src/ir_reverb_plugin_processor.hpp
	src/ir_reverb_point_connector.hpp
	src/ir_reverb_point_controls_row.hpp
	src/ir_reverb_scene_view.hpp
	src/ir_reverb_add_items_popup.hpp
)

source_group("Header Files" FILES ${HEADERS_IR_REVERB})

# Skeleton of a convolution (IR) based counterpart to the "Reverb for EAR
# Suite" plugin - same overall structure (discovers Object/Object OSC
# tracks via Scene's own PUB broadcast, outputs 10 points in the same
# "4+6+0" layout, each independently registered with Scene as its own ADM
# Object), but the diffuse tail is a convolution against a loaded impulse
# response instead of an algorithmic FDN/MVerb.
#
# v1 scope (this skeleton): mono impulse responses only, one shared IR
# convolved per point against that point's own direction-weighted sum of
# the active sources (encode-then-sum-then-convolve, so CPU cost is
# independent of how many sources are active - see processBlock()).
# Deferred: stereo/AmbiX/discrete-array IR support, per-point position/
# gain overrides (points currently expose only name+routing, not the
# full Object-style parameter set the algorithmic Reverb has), and a
# saved-IR-library combo (currently a single "Load IR..." file picker).
add_juce_vst3_plugin(ir_reverb
  SOURCES ${SOURCES_IR_REVERB} ${HEADERS_IR_REVERB}
  IDE_FOLDER ${IDE_FOLDER_PLUGINS}
  CODE_SUFFIX "22"
  DISPLAY_NAME "LF IR Reverb"
  DESCRIPTION "Convolution-based spatial reverb: reverberates a selected EAR Object/Object OSC through a loaded impulse response and sends 10 points to EAR Scene as independent objects. Not affiliated with or endorsed by EBU."
  OUTPUT_NAME "LF IR Reverb"
  MANUFACTURER_NAME "Luca Frigo"
  MANUFACTURER_WEBSITE "https://github.com/frigol/ear-suite-extras"
  MANUFACTURER_CODE "4C462020" # Hex for 'LF  '
  BUNDLE_ID_DOMAIN "io.lucafrigo"
  BUNDLE_ID_PREFIX "io.lucafrigo"
  COPYRIGHT "(C) 2026 Luca Frigo"
  IS_EPS_PLUGIN ON)

target_include_directories(ir_reverb_VST3 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ir_reverb_VST3 PRIVATE ear-plugin-base ear-version ear)
install_juce_vst3_plugin(ir_reverb "${EPS_PLUGIN_INSTALL_PREFIX}ear-production-suite")
