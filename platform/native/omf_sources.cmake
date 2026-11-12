set(OMF_SRC "${PORT_ROOT}/deps/openomf/src")
get_filename_component(NATIVE_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
file(GLOB_RECURSE OMF_FILES CONFIGURE_DEPENDS "${OMF_SRC}/*.c")
list(FILTER OMF_FILES EXCLUDE REGEX "/main.c$|/audio/backends/|/video/renderers/|/vendored/argtable/")
list(FILTER OMF_FILES EXCLUDE REGEX "/(sound|music)_sources/(null_|opus_)")
list(FILTER OMF_FILES EXCLUDE REGEX "/controller/(net_controller|spec_controller|game_controller_db).c$")
list(FILTER OMF_FILES EXCLUDE REGEX "/game/scenes/lobby.c$|/mainmenu/menu_(connect|listen|net).c$")
list(FILTER OMF_FILES EXCLUDE REGEX "/game/utils/nat.c$")
add_library(omf_core STATIC ${OMF_FILES}
  "${OMF_SRC}/audio/backends/native/native_backend.c"
  "${OMF_SRC}/video/renderers/software/software_renderer.c"
  "${NATIVE_DIR}/omf_sdl.c" "${NATIVE_DIR}/omf_pcm.c"
  "${NATIVE_DIR}/omf_offline.c" "${NATIVE_DIR}/omf_main.c")
target_include_directories(omf_core PUBLIC "${OMF_SRC}" "${NATIVE_DIR}")
target_compile_options(omf_core PRIVATE -include "${NATIVE_DIR}/omf_sdl_names.h" -ffunction-sections -fdata-sections -fsigned-char)
target_compile_definitions(omf_core PRIVATE OMF_NATIVE=1 ENABLE_SOFTWARE_RENDERER ENABLE_NATIVE_AUDIO_BACKEND PNG_FOUND
  V_MAJOR=0 V_MINOR=8 V_PATCH=6 V_LABEL="native-dev")
