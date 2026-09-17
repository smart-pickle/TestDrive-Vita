cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED VITASDK)
    if(DEFINED ENV{VITASDK})
        set(VITASDK $ENV{VITASDK})
    else()
        set(VITASDK "/usr/local/vitasdk")
    endif()
endif()

if(EXISTS "${VITASDK}/share/vita.cmake")
    include("${VITASDK}/share/vita.cmake")
    set(IS_VITA TRUE)
endif()

project(TestDriveVita C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

file(GLOB_RECURSE TD_SOURCES CONFIGURE_DEPENDS src/*.c)
file(GLOB_RECURSE TD_ENHANCED_SOURCES CONFIGURE_DEPENDS src/enhanced/*.c)

if(IS_VITA)
    set(VITA_APP_NAME "Test Drive")
    set(VITA_TITLEID "TDRV01987")
    set(VITA_VERSION "01.00")

    include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${VITASDK}/arm-vita-eabi/include/SDL2
    )

    add_executable(${PROJECT_NAME} ${TD_SOURCES})

    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing
        -O2 -ffast-math -ffunction-sections -fdata-sections
    )

    target_compile_definitions(${PROJECT_NAME} PRIVATE
        __psp2__
        __vita__
        VITA
    )

    target_link_options(${PROJECT_NAME} PRIVATE
        -Wl,-q
        -Wl,--gc-sections
    )

    target_link_libraries(${PROJECT_NAME}
        -Wl,--start-group
        SDL2
        mathneon
        SceMotion_stub
        SceDisplay_stub
        SceGxm_stub
        SceCtrl_stub
        SceTouch_stub
        SceHid_stub
        SceIme_stub
        ScePower_stub
        SceCommonDialog_stub
        SceSysmodule_stub
        SceAppMgr_stub
        SceAppUtil_stub
        SceAudio_stub
        SceAudioIn_stub
        SceKernelDmacMgr_stub
        m
        -Wl,--end-group
    )

    vita_create_self(eboot.bin ${PROJECT_NAME} STRIPPED)

    # Option to bundle original game data (disabled for official public releases)
    option(BUNDLE_GAME_DATA "Bundle DOS game data into VPK (set OFF for clean official release)" ON)

    # Collect all manual pages in vpk/manual/
    file(GLOB MANUAL_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/vpk/manual "vpk/manual/*.png")
    set(VPK_MANUAL_FILES)
    foreach(MF ${MANUAL_FILES})
        list(APPEND VPK_MANUAL_FILES FILE ${CMAKE_CURRENT_SOURCE_DIR}/vpk/manual/${MF} sce_sys/manual/${MF})
    endforeach()

    # Collect game assets if BUNDLE_GAME_DATA is enabled
    set(VPK_GAME_FILES)
    if(BUNDLE_GAME_DATA)
        file(GLOB_RECURSE GAME_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "Game/*")
        foreach(GF ${GAME_FILES})
            if(NOT GF MATCHES "\\.ba1$")
                list(APPEND VPK_GAME_FILES FILE ${CMAKE_CURRENT_SOURCE_DIR}/${GF} ${GF})
            endif()
        endforeach()
    endif()

    vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} eboot.bin
        VERSION ${VITA_VERSION}
        NAME "${VITA_APP_NAME}"
        FILE ${CMAKE_CURRENT_SOURCE_DIR}/vpk/icon0.png sce_sys/icon0.png
        FILE ${CMAKE_CURRENT_SOURCE_DIR}/vpk/bg.png sce_sys/livearea/contents/bg.png
        FILE ${CMAKE_CURRENT_SOURCE_DIR}/vpk/startup.png sce_sys/livearea/contents/startup.png
        FILE ${CMAKE_CURRENT_SOURCE_DIR}/vpk/template.xml sce_sys/livearea/contents/template.xml
        ${VPK_MANUAL_FILES}
        ${VPK_GAME_FILES}
    )

    if(TARGET ${PROJECT_NAME}.vpk-vpk)
        add_custom_command(TARGET ${PROJECT_NAME}.vpk-vpk POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.vpk
            ${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.vpk
        )
    endif()
else()
    find_package(SDL2 REQUIRED)
    add_executable(testdrive-enhanced ${TD_SOURCES})
    target_include_directories(testdrive-enhanced PRIVATE src ${SDL2_INCLUDE_DIRS})
    target_link_libraries(testdrive-enhanced PRIVATE ${SDL2_LIBRARIES})
    target_compile_options(testdrive-enhanced PRIVATE -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing)
    set_source_files_properties(${TD_ENHANCED_SOURCES} PROPERTIES COMPILE_OPTIONS -O2)
endif()
