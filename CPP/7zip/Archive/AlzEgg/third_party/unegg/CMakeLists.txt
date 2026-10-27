cmake_minimum_required(VERSION 3.16)
project(UnEggBackend LANGUAGES C CXX)

file(GLOB_RECURSE UNEGG_CPP_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/nest/*.cpp")
list(FILTER UNEGG_CPP_SOURCES EXCLUDE REGEX "/(FileInputStream|FileOutputStream|Workspace|ALZFormatFactory|EggFormatFactory)\\.cpp$")

file(GLOB UNEGG_AES_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/nest/third-party/encryption/aes/*.c")
file(GLOB UNEGG_BZIP2_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/nest/third-party/algorithm/bzip2/*.c")
set(UNEGG_ZLIB_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/nest/third-party/algorithm/zlib/adler32.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/nest/third-party/algorithm/zlib/crc32.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/nest/third-party/algorithm/zlib/inflate.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/nest/third-party/algorithm/zlib/inffast.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/nest/third-party/algorithm/zlib/inftrees.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/nest/third-party/algorithm/zlib/zutil.c")
set(UNEGG_LZMA_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/nest/third-party/algorithm/lzma/Alloc.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/nest/third-party/algorithm/lzma/LzmaDec.c")

add_library(UnEggBackend STATIC
  ${UNEGG_CPP_SOURCES}
  ${UNEGG_AES_SOURCES}
  ${UNEGG_BZIP2_SOURCES}
  ${UNEGG_ZLIB_SOURCES}
  ${UNEGG_LZMA_SOURCES})
target_include_directories(UnEggBackend PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/nest/third-party/algorithm/zlib"
  "${CMAKE_CURRENT_SOURCE_DIR}/nest/third-party/algorithm/bzip2")
target_compile_features(UnEggBackend PUBLIC cxx_std_11)
set_target_properties(UnEggBackend PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED YES
  CXX_STANDARD 11
  CXX_STANDARD_REQUIRED YES
  POSITION_INDEPENDENT_CODE ON)

if(MSVC)
  set_property(TARGET UnEggBackend PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
