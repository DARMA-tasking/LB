# `comm` links against VT's legacy target name: `vt::vt`.
# New VT export is `vt::runtime::vt`.

if(vt_backend_enabled)
  find_package(vt REQUIRED CONFIG)

  if(TARGET vt::runtime::vt AND NOT TARGET vt::vt)
    add_library(vt::vt ALIAS vt::runtime::vt)
  endif()

  if(NOT TARGET vt::vt)
    message(FATAL_ERROR "VT does not provide vt::vt or vt::runtime::vt")
  endif()
endif()
