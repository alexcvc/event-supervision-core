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

add_executable(event_descriptor_type_mismatch_death_test EventDescriptorTypeMismatchDeathTest.cpp)
target_link_libraries(event_descriptor_type_mismatch_death_test PRIVATE event_system)

if (UNIX)
    # CTest reports a subprocess it observes dying to a signal (e.g. SIGABRT from
    # std::terminate()) as an unconditional "Exception" failure that neither WILL_FAIL
    # nor PASS_REGULAR_EXPRESSION can invert. run_death_test.sh absorbs the signal itself
    # and reports success/failure via a normal exit code that CTest can evaluate.
    add_test(NAME EventDescriptorTypeMismatchTerminates
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/run_death_test.sh
                $<TARGET_FILE:event_descriptor_type_mismatch_death_test>)
endif ()
