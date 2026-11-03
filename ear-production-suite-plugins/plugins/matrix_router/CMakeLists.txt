set(SOURCES_MATRIX_ROUTER
    src/matrix_router_plugin_editor.cpp
    src/matrix_router_plugin_processor.cpp
)

set(HEADERS_MATRIX_ROUTER
	src/matrix_grid_component.hpp
	src/matrix_router_plugin_editor.hpp
	src/matrix_router_plugin_processor.hpp
)

source_group("Header Files" FILES ${HEADERS_MATRIX_ROUTER})

# Not an EBU/EAR Production Suite plugin - it has its own identity rather
# than the family's EBU one used by add_juce_vst3_plugin()'s defaults.
add_juce_vst3_plugin(
  matrix_router
  SOURCES ${SOURCES_MATRIX_ROUTER} ${HEADERS_MATRIX_ROUTER}
  CODE_SUFFIX "20"
  DISPLAY_NAME "Matrix Router"
  DESCRIPTION "128x128 routing matrix (patchbay) with saveable crosspoint presets"
  OUTPUT_NAME "Matrix Router"
  IDE_FOLDER ${IDE_FOLDER_PLUGINS}
  IS_EPS_PLUGIN OFF
  MANUFACTURER_NAME "Luca Frigo"
  MANUFACTURER_WEBSITE ""
  MANUFACTURER_CODE "4C462020" # Hex for 'LF  '
  BUNDLE_ID_DOMAIN "io.lucafrigo"
  BUNDLE_ID_PREFIX "io.lucafrigo"
  COPYRIGHT "(C) 2026 Luca Frigo")

install_juce_vst3_plugin(matrix_router "${EPS_PLUGIN_INSTALL_PREFIX}ear-production-suite")
