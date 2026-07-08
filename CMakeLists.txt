cmake_minimum_required(VERSION 3.10.0)
project(vst3arduinothing VERSION 0.1.0 LANGUAGES C CXX)

add_executable(vst3arduinothing
    src/cli.cpp
    src/hardware.cpp
)
target_compile_features(vst3arduinothing PRIVATE cxx_std_17)
