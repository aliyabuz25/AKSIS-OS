cmake_minimum_required(VERSION 3.16)
project(shenlong_ultralight_bridge LANGUAGES CXX)

set(ULTRALIGHT_SDK_ROOT "" CACHE PATH "Ultralight SDK root")
if(NOT ULTRALIGHT_SDK_ROOT)
  message(FATAL_ERROR "Set ULTRALIGHT_SDK_ROOT to the official Ultralight SDK")
endif()

add_library(shenlong_ultralight SHARED ultralight_bridge.cpp)
target_compile_features(shenlong_ultralight PRIVATE cxx_std_11)
target_include_directories(shenlong_ultralight PRIVATE
  "${ULTRALIGHT_SDK_ROOT}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}")
if(APPLE)
  target_link_directories(shenlong_ultralight PRIVATE "${ULTRALIGHT_SDK_ROOT}/lib/mac")
elseif(WIN32)
  target_link_directories(shenlong_ultralight PRIVATE "${ULTRALIGHT_SDK_ROOT}/lib/win/x64")
else()
  target_link_directories(shenlong_ultralight PRIVATE "${ULTRALIGHT_SDK_ROOT}/lib/linux")
endif()
target_link_libraries(shenlong_ultralight PRIVATE Ultralight UltralightCore WebCore)
