include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v2.13.10
)
FetchContent_MakeAvailable(Catch2)

add_executable(event_system_tests
    EventDescriptorTests.cpp
    EventSupervisorTests.cpp
)

target_link_libraries(event_system_tests PRIVATE
    event_system
    Catch2::Catch2
)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/contrib)
include(CTest)
include(Catch)
catch_discover_tests(event_system_tests)
