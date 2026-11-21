set(_CZX_APPLEWIN_NO_ROMS_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(czx_override_applewin_external_roms)
  if(TARGET apple2roms)
    set_target_properties(apple2roms PROPERTIES
      SOURCES "${_CZX_APPLEWIN_NO_ROMS_DIR}/external_apple2roms.cpp"
    )
    set_property(TARGET apple2roms PROPERTY INCLUDE_DIRECTORIES
      "${_CZX_APPLEWIN_NO_ROMS_DIR}"
      "${CMAKE_SOURCE_DIR}/resource"
    )
    set_property(TARGET apple2roms PROPERTY INTERFACE_INCLUDE_DIRECTORIES
      "${_CZX_APPLEWIN_NO_ROMS_DIR}"
    )
  endif()

  file(REMOVE
    "${CMAKE_BINARY_DIR}/resource/apple2roms_data.h"
    "${CMAKE_BINARY_DIR}/resource/apple2roms_data.cpp"
  )

  if(EXISTS "${CMAKE_BINARY_DIR}/source/frontends/common2/common_config.h")
    file(WRITE "${CMAKE_BINARY_DIR}/source/frontends/common2/common_config.h"
      "// relative path from executable to resources\n#define ROOT_PATH \".\"\n"
    )
  endif()
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL czx_override_applewin_external_roms)
