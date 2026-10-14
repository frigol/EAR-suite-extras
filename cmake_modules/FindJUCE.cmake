if(NOT JUCE_FOUND)

find_path(JUCE_SDK_DIR
  NAMES modules/juce_audio_basics
  PATHS ${JUCE_ROOT_DIR}
        ENV JUCE_ROOT_DIR
  HINTS
        "${CMAKE_SOURCE_DIR}/submodules/JUCE"
  )


include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(JUCE
  FOUND_VAR JUCE_FOUND
  REQUIRED_VARS JUCE_SDK_DIR
  FAIL_MESSAGE "Could NOT find JUCE. Consider setting JUCE_ROOT_DIR to the JUCE SDK root directory"
)

  if(JUCE_FOUND)
    include(juce_helpers)


    _generate_juce_sources(JUCE_GENERATED_SOURCES)
    # Main/core/shared JUCE library target
    ############
    add_library(Juce::core INTERFACE IMPORTED)
    target_sources(Juce::core INTERFACE ${JUCE_GENERATED_SOURCES})
    # set(_SUPPORT_PATH ${CMAKE_CURRENT_BINARY_DIR}/juce_core_resources/)
    # configure_file(${_JUCE_SUPPORT_RESOURCES}/juce/AppConfig.h.in ${_SUPPORT_PATH}/AppConfig.h)
    # configure_file(${_JUCE_SUPPORT_RESOURCES}/juce/JuceHeader.h.in ${_SUPPORT_PATH}/JuceHeader.h)
    # target_include_directories(Juce_core PRIVATE ${_SUPPORT_PATH})
    target_include_directories(Juce::core SYSTEM INTERFACE ${JUCE_SDK_DIR}/modules)
    target_compile_features(Juce::core INTERFACE cxx_std_14)

    # JUCE 8 renders SVGs (used for some icons/looks) via the bundled
    # lunasvg + plutovg libraries, and bundles a much bigger HarfBuzz for
    # text shaping. lunasvg's C++ and most of HarfBuzz are pulled in by the
    # juce_graphics.cpp amalgamation as normal, but plutovg is a set of
    # plain C translation units that amalgamation doesn't reach, and
    # HarfBuzz's own single-TU amalgamation entry point (harfbuzz.cc) is
    # never referenced from juce_graphics.cpp either - this DIY build
    # (predating both additions) never compiled either, leaving
    # juce_plutovg_*/hb_* symbols undefined at link time. Only present from
    # JUCE 8 onwards, hence the existence checks.
    set(_JUCE_PLUTOVG_DIR
      "${JUCE_SDK_DIR}/modules/juce_graphics/drawables/lunasvg/plutovg")
    if(EXISTS "${_JUCE_PLUTOVG_DIR}/source")
      # This is its own OBJECT library, rather than sources added directly
      # to Juce::core, because Juce::core forces "-x objective-c++" on
      # Apple (below) for JUCE's own .cpp sources, which would break
      # plutovg's plain C (it relies on implicit void* conversions that are
      # only valid in C) - a per-source COMPILE_OPTIONS override does not
      # reliably win against that INTERFACE-propagated flag.
      # find_package(JUCE) is called from several independent
      # subdirectories in this project (e.g. reaper-adm-extension as well
      # as the plugins), each getting its own directory-scoped JUCE_FOUND
      # and so re-entering this whole if(JUCE_FOUND) block; that's harmless
      # for the IMPORTED targets above (redefining those is tolerated), but
      # a real target like this one is not, so guard it explicitly.
      if(NOT TARGET juce_plutovg)
        file(GLOB _JUCE_PLUTOVG_SOURCES "${_JUCE_PLUTOVG_DIR}/source/*.c")
        add_library(juce_plutovg OBJECT ${_JUCE_PLUTOVG_SOURCES})
        target_include_directories(juce_plutovg SYSTEM PUBLIC
          ${_JUCE_PLUTOVG_DIR}/include)
        set_target_properties(juce_plutovg PROPERTIES
          POSITION_INDEPENDENT_CODE ON
          C_VISIBILITY_PRESET hidden)
      endif()
      # target_link_libraries(... INTERFACE juce_plutovg) does not reliably
      # forward an OBJECT library's object files through an INTERFACE
      # IMPORTED target chain (Juce::core -> Juce::VST3 -> the plugin) in
      # all CMake versions; referencing its $<TARGET_OBJECTS:...> as an
      # interface source is the documented, reliable way to do that.
      target_sources(Juce::core INTERFACE $<TARGET_OBJECTS:juce_plutovg>)
      target_include_directories(Juce::core SYSTEM INTERFACE ${_JUCE_PLUTOVG_DIR}/include)
    endif()

    set(_JUCE_HARFBUZZ_AMALGAMATION
      "${JUCE_SDK_DIR}/modules/juce_graphics/fonts/harfbuzz/harfbuzz.cc")
    if(EXISTS "${_JUCE_HARFBUZZ_AMALGAMATION}")
      # Plain C++, so - unlike plutovg - it's fine to compile as part of
      # Juce::core directly (no "-x objective-c++" clash).
      target_sources(Juce::core INTERFACE ${_JUCE_HARFBUZZ_AMALGAMATION})
    endif()
    target_compile_definitions(Juce::core INTERFACE
                    $<$<CONFIG:DEBUG>:DEBUG>
                    $<$<NOT:$<CONFIG:DEBUG>>:NDEBUG>)

    if(APPLE)
      find_package( OpenGL REQUIRED )
      find_library(AUDIOUNIT AudioUnit)
      find_library(AUDIOTOOLBOX AudioToolbox)
      find_library(AVFOUNDATION AVFoundation)
      find_library(AVKIT AVKit)
      find_library(CARBON Carbon)
      find_library(COCOA Cocoa)
      find_library(COREAUDIO CoreAudio)
      find_library(COREFOUNDATION CoreFoundation)
      find_library(COREMEDIA CoreMedia)
      find_library(COREMIDI CoreMidi)
      find_library(QUARTZCORE QuartzCore)
      find_library(IOKIT IOKit)
      find_library(ACCELERATE Accelerate)
      find_library(WEBKIT WebKit)
      find_library(OBJC objc)
      find_library(COREAUDIOKIT CoreAudioKit)
      find_library(SECURITY Security)

      # juce_graphics normally compiles its own bundled libpng/zlib sources
      # (JUCE_INCLUDE_PNGLIB_CODE/JUCE_INCLUDE_ZLIB_CODE default to 1), but it
      # skips that when Homebrew's libpng happens to be installed (even
      # though its headers are never on our include path) and expects a
      # system png to be linked instead - otherwise the PNG encoder symbols
      # are left undefined at link time. Link the library only: adding
      # find_package(PNG)'s /opt/homebrew/include as a -isystem path would
      # shadow vcpkg's protobuf (and other) headers with Homebrew's, and
      # JUCE only needs the symbols, not Homebrew's png.h, since it declares
      # what it needs itself.
      find_library(HOMEBREW_PNG_LIBRARY NAMES png16 png
        PATHS /opt/homebrew/opt/libpng/lib /opt/homebrew/lib /usr/local/opt/libpng/lib)
      find_library(HOMEBREW_ZLIB_LIBRARY NAMES z)
      # Same story as libpng above, for JUCE's bundled jpglib
      # (JUCE_INCLUDE_JPEGLIB_CODE).
      find_library(HOMEBREW_JPEG_LIBRARY NAMES jpeg
        PATHS /opt/homebrew/opt/jpeg-turbo/lib /opt/homebrew/opt/jpeg/lib
              /opt/homebrew/lib /usr/local/opt/jpeg-turbo/lib)

      target_link_libraries(Juce::core INTERFACE
          ${OPENGL_gl_LIBRARY}
          ${AUDIOUNIT}
          ${AUDIOTOOLBOX}
          ${AVFOUNDATION}
          ${AVKIT}
          ${CARBON}
          ${COCOA}
          ${COREAUDIO}
          ${COREFOUNDATION}
          ${COREMEDIA}
          ${COREMIDI}
          ${QUARTZCORE}
          ${IOKIT}
          ${ACCELERATE}
          ${WEBKIT}
          ${OBJC}
          ${COREAUDIOKIT}
          ${SECURITY}
          ${HOMEBREW_PNG_LIBRARY}
          ${HOMEBREW_ZLIB_LIBRARY}
          ${HOMEBREW_JPEG_LIBRARY})
        target_compile_options(Juce::core INTERFACE -x objective-c++)
    elseif(WIN32)
      target_link_libraries(Juce::core INTERFACE
        advapi32.lib
        comdlg32.lib
        gdi32.lib
        GlU32.Lib
        kernel32.lib
        ole32.lib
        OpenGL32.Lib
        rpcrt4.lib
        shell32.lib
        user32.lib
        vfw32.lib
        wininet.lib
        winmm.lib
        ws2_32.lib
      )
    elseif("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
      find_package(OpenGL REQUIRED)
      find_package(Freetype REQUIRED)
      find_package(Threads REQUIRED)
      target_link_libraries(Juce::core INTERFACE
        OpenGL::GL
        Freetype::Freetype
        Threads::Threads
        ${CMAKE_DL_LIBS}
      )
    endif(APPLE)

    # VST3
    ############
    add_library(Juce::VST3 INTERFACE IMPORTED)
    target_compile_definitions(Juce::VST3 INTERFACE JucePlugin_Build_VST3=1)
    target_link_libraries(Juce::VST3 INTERFACE pluginterfaces sdk Juce::core)
    target_sources(Juce::VST3 INTERFACE ${JUCE_GENERATED_SOURCES_VST3})
  endif()

endif()
