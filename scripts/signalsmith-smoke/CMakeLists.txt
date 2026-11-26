cmake_minimum_required(VERSION 3.21)
project(SignalsmithSmoke LANGUAGES CXX)

set(stretch "${CMAKE_CURRENT_SOURCE_DIR}/../../signalsmithStretch/src/main/jni")
add_executable(signalsmith-fixture fixture.cpp)
target_compile_features(signalsmith-fixture PRIVATE cxx_std_17)
target_include_directories(signalsmith-fixture PRIVATE "${stretch}" "${stretch}/third_party")

enable_testing()
add_test(NAME signalsmith-fixture COMMAND signalsmith-fixture)
