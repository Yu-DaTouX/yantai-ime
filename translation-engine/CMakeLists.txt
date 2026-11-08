cmake_minimum_required(VERSION 3.25)
project(YantaiTranslationEngine LANGUAGES C CXX)

# Deliberately NOT add_subdirectory'd by Server/installer: these are downloadable components.
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The downloadable engine currently targets Windows x64 only")
endif()
if(NOT EXISTS "${CTRANSLATE2_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Set CTRANSLATE2_SOURCE_DIR to CTranslate2 v4.8.1 (0d8bcd362ac75ef860ef161d6f0efad0ae439ff0)")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(WITH_MKL OFF CACHE BOOL "MKL must not be distributed" FORCE)
set(WITH_DNNL ON CACHE BOOL "" FORCE)
set(WITH_OPENBLAS OFF CACHE BOOL "" FORCE)
set(WITH_CUDA OFF CACHE BOOL "" FORCE)
set(WITH_CUDNN OFF CACHE BOOL "" FORCE)
set(WITH_TENSOR_PARALLEL OFF CACHE BOOL "" FORCE)
set(WITH_FLASH_ATTN OFF CACHE BOOL "" FORCE)
set(OPENMP_RUNTIME COMP CACHE STRING "MSVC OpenMP, not Intel runtime" FORCE)
set(BUILD_CLI OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_CPU_DISPATCH ON CACHE BOOL "" FORCE)
add_subdirectory("${CTRANSLATE2_SOURCE_DIR}" ct2 EXCLUDE_FROM_ALL)
# Keep all C++ implementation/allocations behind the bridge C ABI, and use the
# same dynamic CRT as the Windows vcpkg libraries (upstream static CT2 defaults to /MT).
foreach(target ctranslate2 cpu_features utils)
    set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endforeach()

find_path(SENTENCEPIECE_INCLUDE_DIR sentencepiece_processor.h REQUIRED)
find_library(SENTENCEPIECE_LIBRARY NAMES sentencepiece REQUIRED)
# The Windows vcpkg SentencePiece port is static even with x64-windows; retain its
# external protobuf/Abseil dependencies instead of treating the .lib as an import library.
find_package(absl CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
add_library(yantai_translate SHARED engine.cpp)
target_compile_definitions(yantai_translate PRIVATE YT_TRANSLATE_ENGINE_EXPORTS NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(yantai_translate PRIVATE /utf-8 /W4)
target_include_directories(yantai_translate PRIVATE
    ../server/src "${CTRANSLATE2_SOURCE_DIR}/include" "${SENTENCEPIECE_INCLUDE_DIR}")
target_link_libraries(yantai_translate PRIVATE ctranslate2 "${SENTENCEPIECE_LIBRARY}"
    protobuf::libprotobuf-lite absl::flags absl::flags_parse absl::strings)
set_target_properties(yantai_translate PROPERTIES CXX_VISIBILITY_PRESET hidden)
add_executable(translation_engine_smoke smoke.cpp ../server/src/translation/local_engine.cpp)
target_compile_definitions(translation_engine_smoke PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(translation_engine_smoke PRIVATE /utf-8 /W4)
target_include_directories(translation_engine_smoke PRIVATE ../server/src)
add_executable(translation_worker_smoke worker_smoke.cpp
    ../server/src/translation/translation_worker.cpp ../server/src/translation/local_inference.cpp
    ../server/src/translation/local_engine.cpp)
target_include_directories(translation_worker_smoke PRIVATE ../server/src)
target_compile_definitions(translation_worker_smoke PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(translation_worker_smoke PRIVATE /utf-8 /W4)
