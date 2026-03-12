cmake_minimum_required(VERSION 3.16)

project(Darwin VERSION 1.0.5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/Zc:__cplusplus)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Svg)

add_executable(Darwin
    resources.qrc
    Darwin.rc
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
    # Views - Plugin Editor
    src/views/plugineditor/PluginEditorWidget.cpp
    src/views/plugineditor/PluginEditorWidget.h
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
    src/audio/AudioInputCapture.cpp
    src/audio/AudioInputCapture.h
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
    src/common/ThemeManager.cpp
    src/common/ThemeManager.h
    src/common/WavWriter.cpp
    src/common/WavWriter.h
    src/common/ModelAccessLock.cpp
    src/common/ModelAccessLock.h
    # Plugins
    src/plugins/VST3Scanner.cpp
    src/plugins/VST3Scanner.h
    src/plugins/VST3PluginInstance.cpp
    src/plugins/VST3PluginInstance.h
)

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

# VST3 SDK ソースファイルをビルドターゲットに追加
target_sources(Darwin PRIVATE
    # Hosting utilities
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_win32.cpp
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
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/threadchecker_win32.cpp
    # Utility
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/utility/stringconvert.cpp
)

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
install(FILES darwin.ico DESTINATION .)
