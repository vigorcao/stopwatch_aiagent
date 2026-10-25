# Browser simulator build of the StopWatch UserDemo.
#   emcmake cmake -S web -B web/build && cmake --build web/build -j
# Compiles the real main/apps + main/assets + main/main.cpp against the web HAL.
cmake_minimum_required(VERSION 3.16)
project(stopwatch_web C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(COMP ${ROOT}/components)
set(MAIN ${ROOT}/main)

file(GLOB_RECURSE LIB_SRCS
    ${COMP}/lvgl/src/*.c
    ${COMP}/mooncake/src/*.cpp
    ${COMP}/mooncake_log/src/*.cpp
    ${COMP}/mooncake_log/src/*.cc
    ${COMP}/smooth_ui_toolkit/src/*.cpp
)
file(GLOB_RECURSE APP_SRCS
    ${MAIN}/apps/*.c
    ${MAIN}/apps/*.cpp
    ${MAIN}/assets/*.c
    ${MAIN}/assets/*.cpp
)

# Embed the boot sound the way ESP-IDF EMBED_FILES does.
set(BOOT_SFX_C ${CMAKE_CURRENT_BINARY_DIR}/boot_sfx.c)
file(READ ${MAIN}/assets/sfx/boot_sfx.bin BOOT_SFX_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," BOOT_SFX_BYTES "${BOOT_SFX_HEX}")
file(WRITE ${BOOT_SFX_C}
    "#include <stddef.h>\n#include <stdint.h>\n"
    "const uint8_t web_boot_sfx[] __attribute__((aligned(2))) = {${BOOT_SFX_BYTES}};\n"
    "const size_t web_boot_sfx_size = sizeof(web_boot_sfx);\n")

add_executable(stopwatch
    ${LIB_SRCS}
    ${APP_SRCS}
    ${MAIN}/main.cpp
    ${MAIN}/hal/utils/button/Button_Class.cpp
    ${COMP}/M5GFX/src/lgfx/utility/lgfx_qrcode.c
    ${CMAKE_CURRENT_LIST_DIR}/src/main_web.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/hal_web.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/settings_web.cc
    ${BOOT_SFX_C}
)

# shim/ must come first so its M5GFX.h / i2c_bus.h / nvs_flash.h win.
target_include_directories(stopwatch PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/shim
    ${CMAKE_CURRENT_LIST_DIR}
    ${MAIN}
    ${COMP}/lvgl
    ${COMP}/lvgl/demos
    ${COMP}/mooncake/src
    ${COMP}/mooncake_log/src
    ${COMP}/smooth_ui_toolkit/src
    ${COMP}/smooth_ui_toolkit/src/lvgl
    ${COMP}/M5GFX/src
)

target_compile_definitions(stopwatch PRIVATE
    LV_CONF_INCLUDE_SIMPLE
    LV_LVGL_H_INCLUDE_SIMPLE
    SMOOTH_UI_TOOLKIT_ENABLE_DEFAULT_HAL=1
)

set(EM_FLAGS "-sUSE_SDL=2")
target_compile_options(stopwatch PRIVATE ${EM_FLAGS} -O2 -Wno-everything)
target_link_options(stopwatch PRIVATE
    ${EM_FLAGS}
    -O2
    -sASYNCIFY
    -sASYNCIFY_STACK_SIZE=65536
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=67108864
    -sSTACK_SIZE=1048576
    -sEXPORTED_RUNTIME_METHODS=HEAP16,UTF8ToString,stringToNewUTF8
    --shell-file ${CMAKE_CURRENT_LIST_DIR}/shell.html
    "SHELL:--preload-file ${ROOT}/fs/pets@/spiflash/pets"
    "SHELL:--preload-file ${ROOT}/fs/fonts@/spiflash/fonts"
)
set_target_properties(stopwatch PROPERTIES SUFFIX ".html")
set_property(TARGET stopwatch APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/shell.html)
