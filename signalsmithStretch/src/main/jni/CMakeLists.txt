cmake_minimum_required(VERSION 3.21.0 FATAL_ERROR)

project(signalsmithStretchJNI CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(stretch_common_flags "-O3 -fno-math-errno -ffp-contract=fast -ffunction-sections -fdata-sections -ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${stretch_common_flags}")

add_library(signalsmithStretchJNI SHARED signalsmith_jni.cc)

set_target_properties(signalsmithStretchJNI PROPERTIES
                      CXX_VISIBILITY_PRESET hidden
                      VISIBILITY_INLINES_HIDDEN ON)

target_include_directories(signalsmithStretchJNI PRIVATE
                           "${CMAKE_CURRENT_SOURCE_DIR}"
                           "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

target_link_options(signalsmithStretchJNI PRIVATE
                    -Wl,--gc-sections
                    -Wl,--icf=all
                    -Wl,--exclude-libs,ALL
                    -Wl,-z,max-page-size=16384
                    -Wl,--build-id=sha1
                    -s)

target_link_libraries(signalsmithStretchJNI PRIVATE log)
