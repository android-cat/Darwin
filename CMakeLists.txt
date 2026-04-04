cmake_minimum_required(VERSION 3.16)

project(Darwin VERSION 1.0.5 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    enable_language(OBJCXX)
endif()

if(MSVC)
    add_compile_options(/Zc:__cplusplus)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Svg)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
endif()

set(DARWIN_SOURCES
    resources.qrc
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    # Views - Source
    src/views/source/SourceView.cpp
    src/views/source/SourceView.h
    src/views/source/ScanSpinnerWidget.cpp
    src/views/source/PluginLoadOverlay.cpp
    # Views - Compose
    src/views/compose/ComposeView.cpp
    src/views/compose/ComposeView.h
    # Views - Mix
    src/views/mix/MixView.cpp
    src/views/mix/MixView.h
    # Views - Arrangement
    src/views/arrangement/ArrangementView.cpp
    src/views/arrangement/ArrangementView.h
    src/views/arrangement/ArrangementView_Headers.cpp
    src/views/arrangement/ArrangementView_DragDrop.cpp
    src/views/arrangement/ArrangementGridWidget.cpp
    src/views/arrangement/ArrangementGridWidget.h
    src/views/arrangement/ArrangementGridWidget_Painting.cpp
    src/views/arrangement/ArrangementGridWidget_Input.cpp
    src/views/arrangement/ArrangementGridWidget_DragDrop.cpp
    src/views/arrangement/ArrangementGridWidget_Animation.cpp
    src/views/arrangement/TimelineWidget.cpp
    src/views/arrangement/TimelineWidget.h
    # Views - Piano Roll
    src/views/pianoroll/PianoRollView.cpp
    src/views/pianoroll/PianoRollView.h
    src/views/pianoroll/PianoRollGridWidget.cpp
    src/views/pianoroll/PianoRollGridWidget.h
    src/views/pianoroll/PianoRollGridWidget_Painting.cpp
    src/views/pianoroll/PianoRollGridWidget_Input.cpp
    src/views/pianoroll/VelocityLaneWidget.cpp
    src/views/pianoroll/VelocityLaneWidget.h
    src/views/pianoroll/ExpressionLaneWidget.cpp
    src/views/pianoroll/ExpressionLaneWidget.h
    # Views - Plugin Editor
    src/views/plugineditor/PluginEditorWidget.cpp
    src/views/plugineditor/PluginEditorWidget.h
    src/views/plugineditor/MacNativeViewUtils.h
    # Widgets
    src/widgets/CustomTooltip.cpp
    src/widgets/CustomTooltip.h
    src/widgets/RecordModeButton.cpp
    src/widgets/RecordModeButton.h
    src/widgets/SplashWidget.cpp
    src/widgets/SplashWidget.h
    src/widgets/IconButton.h
    src/widgets/MixerChannelWidget.cpp
    src/widgets/MixerChannelWidget.h
    src/widgets/ProjectLoadDialog.cpp
    src/widgets/ProjectLoadDialog.h
    src/widgets/KnobWidget.cpp
    src/widgets/KnobWidget.h
    src/widgets/FaderWidget.cpp
    src/widgets/FaderWidget.h
    src/widgets/LevelMeterWidget.cpp
    src/widgets/LevelMeterWidget.h
    src/widgets/BulbRaysOverlay.cpp
    src/widgets/BulbRaysOverlay.h
    src/widgets/ExportProgressDialog.cpp
    src/widgets/ExportProgressDialog.h
    # Models
    src/models/Note.cpp
    src/models/Note.h
    src/models/CCEvent.cpp
    src/models/CCEvent.h
    src/models/Clip.cpp
    src/models/Clip.h
    src/models/Track.cpp
    src/models/Track.h
    src/models/Project.cpp
    src/models/Project.h
    # Controllers
    src/controllers/MidiInputDevice.cpp
    src/controllers/MidiInputDevice.h
    src/controllers/PlaybackController.cpp
    src/controllers/PlaybackController.h
    # Audio
    src/audio/AudioEngine.cpp
    src/audio/AudioEngine.h
    src/audio/AudioEngineBackend.cpp
    src/audio/AudioEngineBackend.h
    src/audio/AudioInputCapture.cpp
    src/audio/AudioInputCapture.h
    src/audio/AudioInputCaptureBackend.cpp
    src/audio/AudioInputCaptureBackend.h
    src/audio/AudioExporter.cpp
    src/audio/AudioExporter.h
    # Commands
    src/commands/UndoCommands.cpp
    src/commands/UndoCommands.h
    # Common
    src/common/Constants.h
    src/common/BurstAnimationHelper.h
    src/common/FadeHelper.h
    src/common/MidiFileParser.h
    src/common/AudioFileReader.h
    src/common/AudioFileReader.cpp
    src/common/FontManager.h
    src/common/FontManager.cpp
    src/common/ThemeManager.cpp
    src/common/ThemeManager.h
    src/common/WavWriter.cpp
    src/common/WavWriter.h
    src/common/ModelAccessLock.cpp
    src/common/ModelAccessLock.h
    # Plugins
    src/plugins/VST3Scanner.cpp
    src/plugins/VST3Scanner.h
    src/plugins/VST3MetadataProbe.cpp
    src/plugins/VST3MetadataProbe.h
    src/plugins/VST3PluginInstance.cpp
    src/plugins/VST3PluginInstance.h
)

if(WIN32)
    list(APPEND DARWIN_SOURCES Darwin.rc)
endif()

if(APPLE)
    list(APPEND DARWIN_SOURCES
        src/views/plugineditor/MacNativeViewUtils.mm
    )
endif()

add_executable(Darwin ${DARWIN_SOURCES})

# Include directories so we can use #include "header.h" directly
target_include_directories(Darwin PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/views/arrangement
    ${CMAKE_CURRENT_SOURCE_DIR}/src/views/pianoroll
    ${CMAKE_CURRENT_SOURCE_DIR}/src/views/compose
    ${CMAKE_CURRENT_SOURCE_DIR}/src/views/mix
    ${CMAKE_CURRENT_SOURCE_DIR}/src/views/source
    ${CMAKE_CURRENT_SOURCE_DIR}/src/views/plugineditor
    ${CMAKE_CURRENT_SOURCE_DIR}/src/widgets
    ${CMAKE_CURRENT_SOURCE_DIR}/src/models
    ${CMAKE_CURRENT_SOURCE_DIR}/src/controllers
    ${CMAKE_CURRENT_SOURCE_DIR}/src/plugins
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/src/commands
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

include(FetchContent)

# VST3 SDK Configuration
set(SMTG_ADD_VST3_HOSTING_SAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ADD_VST3_PLUGIN_SAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
set(SMTG_BUILD_VST3_SDK_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ADD_VSTGUI OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)



FetchContent_Declare(
    vst3sdk
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
    GIT_TAG v3.7.10_build_14
    GIT_SHALLOW TRUE
    GIT_SUBMODULES base pluginterfaces public.sdk
    GIT_SUBMODULES_RECURSE FALSE
)
FetchContent_GetProperties(vst3sdk)
if(NOT vst3sdk_POPULATED)
    FetchContent_Populate(vst3sdk)
endif()

target_include_directories(Darwin PRIVATE 
    ${vst3sdk_SOURCE_DIR}
    ${vst3sdk_SOURCE_DIR}/pluginterfaces
    ${vst3sdk_SOURCE_DIR}/public.sdk
    ${vst3sdk_SOURCE_DIR}/public.sdk/source
    ${vst3sdk_SOURCE_DIR}/base
)

# VST3 SDK に必要なプリプロセッサ定義
target_compile_definitions(Darwin PRIVATE RELEASE=1)

set(VST3_HOSTING_SOURCES
    # Hosting utilities
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/plugprovider.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/connectionproxy.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
    # Base/common
    ${vst3sdk_SOURCE_DIR}/base/source/fobject.cpp
    ${vst3sdk_SOURCE_DIR}/base/source/baseiids.cpp
    ${vst3sdk_SOURCE_DIR}/base/source/fbuffer.cpp
    ${vst3sdk_SOURCE_DIR}/base/source/fdebug.cpp
    ${vst3sdk_SOURCE_DIR}/base/source/fstreamer.cpp
    ${vst3sdk_SOURCE_DIR}/base/source/fstring.cpp
    ${vst3sdk_SOURCE_DIR}/base/source/updatehandler.cpp
    ${vst3sdk_SOURCE_DIR}/base/thread/source/fcondition.cpp
    ${vst3sdk_SOURCE_DIR}/base/thread/source/flock.cpp
    # pluginterfaces
    ${vst3sdk_SOURCE_DIR}/pluginterfaces/base/coreiids.cpp
    ${vst3sdk_SOURCE_DIR}/pluginterfaces/base/funknown.cpp
    # SDK common
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/pluginview.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/commoniids.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstinitiids.cpp
    # Utility
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/utility/stringconvert.cpp
)

if(WIN32)
    list(APPEND VST3_HOSTING_SOURCES
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_win32.cpp
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/threadchecker_win32.cpp
    )
elseif(APPLE)
    list(APPEND VST3_HOSTING_SOURCES
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_mac.mm
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/threadchecker_mac.mm
    )
endif()

target_sources(Darwin PRIVATE ${VST3_HOSTING_SOURCES})

target_link_libraries(Darwin PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent Qt6::Svg)

# Windows必須ライブラリ
if(WIN32)
    target_link_libraries(Darwin PRIVATE ole32 uuid avrt mfplat mfreadwrite mfuuid propsys shlwapi winmm)
    # _WIN32_WINNT / NTDDI_VERSION: FOLDERID_* 等 Vista 以降の API を有効化
    #   0x0A00 = Windows 10, NTDDI_WIN10 = 0x0A000000
    # WASAPI GUID は AudioEngine.cpp 内で __uuidof() を使用しているため INITGUID 不要。
    target_compile_definitions(Darwin PRIVATE
        _WIN32_WINNT=0x0A00
        NTDDI_VERSION=0x0A000000
        WINVER=0x0A00
    )
endif()

if(APPLE)
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    find_library(COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
    find_library(COREMIDI_FRAMEWORK CoreMIDI REQUIRED)
    find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
    find_library(AUDIOUNIT_FRAMEWORK AudioUnit REQUIRED)

    target_link_libraries(Darwin PRIVATE
        ${COCOA_FRAMEWORK}
        ${COREFOUNDATION_FRAMEWORK}
        ${COREAUDIO_FRAMEWORK}
        ${COREMIDI_FRAMEWORK}
        ${AUDIOTOOLBOX_FRAMEWORK}
        ${AUDIOUNIT_FRAMEWORK}
    )

    target_compile_options(Darwin PRIVATE
        "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>"
    )
endif()



if(WIN32)
    set_target_properties(Darwin PROPERTIES WIN32_EXECUTABLE ON)
    
    # Find windeployqt
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS ${Qt6_DIR}/../../../bin)
    
    if(WINDEPLOYQT_EXECUTABLE)
        message(STATUS "Found windeployqt: ${WINDEPLOYQT_EXECUTABLE}")
        add_custom_command(TARGET Darwin POST_BUILD
            COMMAND ${WINDEPLOYQT_EXECUTABLE} 
                --dir $<TARGET_FILE_DIR:Darwin>
                --libdir $<TARGET_FILE_DIR:Darwin>
                --plugindir $<TARGET_FILE_DIR:Darwin>/plugins
                --no-translations
                --compiler-runtime
                $<TARGET_FILE:Darwin>
            COMMENT "Running windeployqt..."
        )
    else()
        message(WARNING "windeployqt not found. DLLs will not be copied automatically.")
    endif()
endif()

# Installation Targets (Used as reference for Inno Setup)
install(TARGETS Darwin RUNTIME DESTINATION .)
install(FILES LICENSE DESTINATION .)
if(WIN32)
    install(FILES darwin.ico DESTINATION .)
endif()

if(BUILD_TESTING)
    set(DARWIN_TEST_SOURCES
        tests/TestMain.cpp
        tests/TestModels.cpp
        tests/TestUndoCommands.cpp
        tests/TestCommon.cpp
        src/models/Note.cpp
        src/models/CCEvent.cpp
        src/models/Clip.cpp
        src/models/Track.cpp
        src/models/Project.cpp
        src/commands/UndoCommands.cpp
        src/common/AudioFileReader.cpp
        src/common/WavWriter.cpp
        src/common/ThemeManager.cpp
        src/common/FontManager.cpp
        src/common/ModelAccessLock.cpp
        src/plugins/VST3PluginInstance.cpp
    )

    add_executable(DarwinTests ${DARWIN_TEST_SOURCES})
    target_sources(DarwinTests PRIVATE ${VST3_HOSTING_SOURCES})

    target_include_directories(DarwinTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/views/arrangement
        ${CMAKE_CURRENT_SOURCE_DIR}/src/views/pianoroll
        ${CMAKE_CURRENT_SOURCE_DIR}/src/views/compose
        ${CMAKE_CURRENT_SOURCE_DIR}/src/views/mix
        ${CMAKE_CURRENT_SOURCE_DIR}/src/views/source
        ${CMAKE_CURRENT_SOURCE_DIR}/src/views/plugineditor
        ${CMAKE_CURRENT_SOURCE_DIR}/src/widgets
        ${CMAKE_CURRENT_SOURCE_DIR}/src/models
        ${CMAKE_CURRENT_SOURCE_DIR}/src/controllers
        ${CMAKE_CURRENT_SOURCE_DIR}/src/plugins
        ${CMAKE_CURRENT_SOURCE_DIR}/src/audio
        ${CMAKE_CURRENT_SOURCE_DIR}/src/commands
        ${CMAKE_CURRENT_SOURCE_DIR}/src/common
        ${vst3sdk_SOURCE_DIR}
        ${vst3sdk_SOURCE_DIR}/pluginterfaces
        ${vst3sdk_SOURCE_DIR}/public.sdk
        ${vst3sdk_SOURCE_DIR}/public.sdk/source
        ${vst3sdk_SOURCE_DIR}/base
    )

    target_compile_definitions(DarwinTests PRIVATE RELEASE=1)
    target_link_libraries(DarwinTests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Concurrent
        Qt6::Svg
        Qt6::Test
    )

    if(WIN32)
        target_link_libraries(DarwinTests PRIVATE ole32 uuid avrt mfplat mfreadwrite mfuuid propsys shlwapi winmm)
        target_compile_definitions(DarwinTests PRIVATE
            _WIN32_WINNT=0x0A00
            NTDDI_VERSION=0x0A000000
            WINVER=0x0A00
        )
    endif()

    if(APPLE)
        target_link_libraries(DarwinTests PRIVATE
            ${COCOA_FRAMEWORK}
            ${COREFOUNDATION_FRAMEWORK}
            ${COREAUDIO_FRAMEWORK}
            ${COREMIDI_FRAMEWORK}
            ${AUDIOTOOLBOX_FRAMEWORK}
            ${AUDIOUNIT_FRAMEWORK}
        )

        target_compile_options(DarwinTests PRIVATE
            "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>"
        )
    endif()

    add_test(NAME DarwinTests COMMAND DarwinTests)
endif()
