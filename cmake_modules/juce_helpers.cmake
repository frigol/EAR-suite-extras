function(add_juce_vst3_plugin PLUGIN_NAME)
  set(options)
  set(oneValueArgs IDE_FOLDER DESCRIPTION DISPLAY_NAME OUTPUT_NAME CODE_PREFIX CODE_SUFFIX IS_EPS_PLUGIN
                    MANUFACTURER_NAME MANUFACTURER_WEBSITE MANUFACTURER_CODE BUNDLE_ID_DOMAIN
                    BUNDLE_ID_PREFIX COPYRIGHT)
  set(multiValueArgs SOURCES)
  cmake_parse_arguments(PLUGIN "${options}" "${oneValueArgs}"
                        "${multiValueArgs}" ${ARGN} )

  if(NOT PLUGIN_CODE_PREFIX)
    set(PLUGIN_CODE_PREFIX "455053") # Hex for "EPS"
  endif()

  if(NOT PLUGIN_CODE_SUFFIX)
    set(PLUGIN_CODE_SUFFIX "00")
  endif()

  if(NOT PLUGIN_DISPLAY_NAME)
    set(PLUGIN_DISPLAY_NAME "${PLUGIN_NAME}")
  endif()

  if(NOT PLUGIN_OUTPUT_NAME)
    set(PLUGIN_OUTPUT_NAME "${PLUGIN_NAME}")
  endif()

  # These all default to the EBU/EAR Production Suite identity, so existing
  # (EBU) plugins are unaffected; pass them explicitly for a plugin with no
  # EBU affiliation (e.g. MANUFACTURER_NAME "Your Name").
  if(NOT PLUGIN_MANUFACTURER_NAME)
    set(PLUGIN_MANUFACTURER_NAME "EBU")
  endif()
  if(NOT DEFINED PLUGIN_MANUFACTURER_WEBSITE)
    set(PLUGIN_MANUFACTURER_WEBSITE "https://ear-production-suite.ebu.io/")
  endif()
  if(NOT PLUGIN_MANUFACTURER_CODE)
    set(PLUGIN_MANUFACTURER_CODE "45425520") # Hex for 'EBU '
  endif()
  if(NOT PLUGIN_BUNDLE_ID_DOMAIN)
    set(PLUGIN_BUNDLE_ID_DOMAIN "com.ebu")
  endif()
  if(NOT PLUGIN_BUNDLE_ID_PREFIX)
    set(PLUGIN_BUNDLE_ID_PREFIX "ch.ebu.ear")
  endif()
  if(NOT DEFINED PLUGIN_COPYRIGHT)
    set(PLUGIN_COPYRIGHT "(C) 2019-2023 BBC, (C) 2020-2021 EBU, (C) 2019-2021 IRT")
  endif()

  string(REGEX REPLACE "[^0-9A-Za-z\\-\\.]" "-" PLUGIN_BUNDLE_ID ${PLUGIN_NAME})

  set(PRODUCT_BUNDLE_IDENTIFIER "${PLUGIN_BUNDLE_ID_PREFIX}.${PLUGIN_BUNDLE_ID}")
  set(_SUPPORT_PATH ${CMAKE_CURRENT_BINARY_DIR}/${PLUGIN_NAME}_resources/)
  configure_file(${JUCE_SUPPORT_RESOURCES}/juce/AppConfig.h.in ${_SUPPORT_PATH}/AppConfig.h)
  configure_file(${JUCE_SUPPORT_RESOURCES}/juce/JuceHeader.h.in ${_SUPPORT_PATH}/JuceHeader.h)
  configure_file(${JUCE_SUPPORT_RESOURCES}/osx/VST-Info.plist.in ${_SUPPORT_PATH}/Info.plist)
  file(COPY ${JUCE_SUPPORT_RESOURCES}/osx/PkgInfo DESTINATION ${_SUPPORT_PATH})
  if(CMAKE_GENERATOR STREQUAL Xcode)
    # Xcode doesn't like PkgInfo in library modules (Error "Multiple commands produce 'Contents/PkgInfo'")
    add_library(${PLUGIN_NAME}_VST3 MODULE ${PLUGIN_SOURCES} ${_SUPPORT_PATH}/AppConfig.h ${_SUPPORT_PATH}/JuceHeader.h ${_SUPPORT_PATH}/Info.plist)
  else()
    add_library(${PLUGIN_NAME}_VST3 MODULE ${PLUGIN_SOURCES} ${_SUPPORT_PATH}/AppConfig.h ${_SUPPORT_PATH}/JuceHeader.h ${_SUPPORT_PATH}/Info.plist ${_SUPPORT_PATH}/PkgInfo)
  endif()
  target_include_directories(${PLUGIN_NAME}_VST3 PRIVATE ${_SUPPORT_PATH}/ ${EPS_SHARED_DIR})
  target_link_libraries(${PLUGIN_NAME}_VST3 PRIVATE Juce::VST3)

  if(PLUGIN_IS_EPS_PLUGIN)
    list(APPEND EPS_PLUGIN_TARGETS ${PLUGIN_NAME}_VST3)
    set(EPS_PLUGIN_TARGETS ${EPS_PLUGIN_TARGETS} CACHE INTERNAL "")
  endif()

  set_target_properties(${PLUGIN_NAME}_VST3 PROPERTIES
    BUNDLE TRUE
    OUTPUT_NAME "${PLUGIN_OUTPUT_NAME}"
    BUNDLE_EXTENSION "vst3"
    SUFFIX ".vst3"
    XCODE_ATTRIBUTE_WRAPPER_EXTENSION "vst3"
    XCODE_ATTRIBUTE_GENERATE_PKGINFO_FILE "YES"
    MACOSX_BUNDLE_INFO_PLIST "${_SUPPORT_PATH}/Info.plist"
    LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/VST3"
    COMPILE_PDB_NAME "${PLUGIN_OUTPUT_NAME}"
    COMPILE_PDB_OUTPUT_DIR "${PROJECT_BINARY_DIR}/VST3"
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN TRUE
  )
  
  if(PLUGIN_IDE_FOLDER)
    set_target_properties(${PLUGIN_NAME}_VST3 PROPERTIES FOLDER ${PLUGIN_IDE_FOLDER})
  endif()
  
  set_source_files_properties( ${_SUPPORT_PATH}/PkgInfo PROPERTIES
    MACOSX_PACKAGE_LOCATION .
  )

endfunction()

function(install_juce_vst3_plugin PLUGIN_NAME DESTINATION)
install(TARGETS ${PLUGIN_NAME}_VST3 COMPONENT Plugins DESTINATION "${DESTINATION}")
if(WIN32)
    install(FILES $<TARGET_PDB_FILE:${PLUGIN_NAME}_VST3>
            DESTINATION "${DESTINATION}" OPTIONAL)
endif()
endfunction()

function (_generate_juce_module_stub MODULE_FILENAME DEST_PATH OUTVAR)
    get_filename_component(MODULE_NAME ${MODULE_FILENAME} NAME_WE)
    if(MODULE_NAME MATCHES "juce_audio_plugin_client_(utils|VST3|VST|AU|AAX)")
        set(MODULE_NAME "juce_audio_plugin_client")
    endif()
    set(FILENAME "${DEST_PATH}/include_${MODULE_FILENAME}")
    set(${OUTVAR} ${FILENAME} PARENT_SCOPE)
    if(NOT EXISTS "${FILENAME}")
      file(WRITE "${FILENAME}" "#include \"AppConfig.h\"\n#include <${MODULE_NAME}/${MODULE_FILENAME}>\n")
    endif()
endfunction()


function (_generate_juce_sources VAR)
    set(DESTINATION "${PROJECT_BINARY_DIR}/support/juce")
    # common modules
    set(MODULES
      juce_audio_basics
      juce_audio_processors
      juce_audio_devices
      juce_dsp
      juce_audio_formats
      juce_audio_utils
      juce_core
      juce_cryptography
      juce_data_structures
      juce_events
      juce_graphics
      juce_gui_basics
      juce_gui_extra
      juce_opengl
      juce_video
      juce_osc
    )
    foreach(MODULE  ${MODULES})
        _generate_juce_module_stub(${MODULE}.cpp ${DESTINATION} GENERATED_FILE)
        list(APPEND GENERATED_JUCE_FILES ${GENERATED_FILE})
    endforeach()

    # vst3 sources

    # Older JUCE split shared plugin-client helpers into a separate
    # juce_audio_plugin_client_utils.cpp translation unit; current JUCE folds
    # that into detail/juce_PluginUtilities.h, included directly by each
    # client .cpp (e.g. juce_audio_plugin_client_VST3.cpp), so there is no
    # longer a matching source file to generate a stub for here.
    #
    # On Apple, juce_audio_plugin_client_VST3.mm is not a companion to
    # juce_audio_plugin_client_VST3.cpp - it just "#include"s it, wholesale,
    # to compile it as Objective-C++ instead of C++. So on Apple only the
    # .mm stub is generated/compiled; compiling both, as this used to
    # (unconditionally generating the .cpp stub, then also adding a .mm
    # stub alongside it), duplicate-defines every VST3 client symbol.
    if(APPLE)
        _generate_juce_module_stub("juce_audio_plugin_client_VST3.mm" ${DESTINATION} GENERATED_FILE)
        list(APPEND GENERATED_JUCE_FILES_VST3 ${GENERATED_FILE})
    else()
        _generate_juce_module_stub("juce_audio_plugin_client_VST3.cpp" ${DESTINATION} GENERATED_FILE)
        list(APPEND GENERATED_JUCE_FILES_VST3 ${GENERATED_FILE})
    endif()

    # au sources
    if(APPLE)
        _generate_juce_module_stub("juce_audio_plugin_client_AU_1.mm" ${DESTINATION} GENERATED_FILE)
        list(APPEND GENERATED_JUCE_FILES_AU ${GENERATED_FILE})
        _generate_juce_module_stub("juce_audio_plugin_client_AU_2.mm" ${DESTINATION} GENERATED_FILE)
        list(APPEND GENERATED_JUCE_FILES_AU ${GENERATED_FILE})
    endif()

    # aax sources
    _generate_juce_module_stub("juce_audio_plugin_client_AAX.cpp" ${DESTINATION} GENERATED_FILE)
    list(APPEND GENERATED_JUCE_FILES_AAX ${GENERATED_FILE})

    set(${VAR} ${GENERATED_JUCE_FILES} PARENT_SCOPE)
    set(${VAR}_VST3 ${GENERATED_JUCE_FILES_VST3} PARENT_SCOPE)
    set(${VAR}_AU ${GENERATED_JUCE_FILES_AU} PARENT_SCOPE)
    set(${VAR}_AAX ${GENERATED_JUCE_FILES_AAX} PARENT_SCOPE)
endfunction()
