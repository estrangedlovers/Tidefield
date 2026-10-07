include(Catch)

add_executable(tidefield_tests
    support/AllocationGuard.cpp
    dsp/SmootherTests.cpp
    dsp/FilterTests.cpp
    dsp/LimiterTests.cpp
    dsp/DroneTests.cpp
    engine/SpscQueueTests.cpp
    engine/ParamTests.cpp
    engine/MasterChainTests.cpp
    engine/EngineTests.cpp
    engine/TerrainTests.cpp)

target_include_directories(tidefield_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tidefield_tests PRIVATE tidefield_engine Catch2::Catch2WithMain)

catch_discover_tests(tidefield_tests)
