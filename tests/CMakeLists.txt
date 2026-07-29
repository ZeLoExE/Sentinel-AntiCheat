# ============================================================================
# Sentinel AntiCheat - Test Configuration
# ============================================================================

# Unit Tests
add_executable(sentinel_test_unit
    unit/TestTypes.cpp
    unit/TestRiskEngine.cpp
    unit/TestPlayerManager.cpp
    unit/TestAimbotDetector.cpp
    unit/TestMovementDetector.cpp
)

target_link_libraries(sentinel_test_unit PRIVATE
    sentinel_anticheat
    Threads::Threads
)

if(WIN32)
    target_link_libraries(sentinel_test_unit PRIVATE ws2_32)
else()
    target_link_libraries(sentinel_test_unit PRIVATE pthread dl)
endif()

add_test(NAME sentinel_unit_tests COMMAND sentinel_test_unit)

# Integration Tests
add_executable(sentinel_test_integration
    integration/TestSystemIntegration.cpp
)

target_link_libraries(sentinel_test_integration PRIVATE
    sentinel_anticheat
    Threads::Threads
)

if(WIN32)
    target_link_libraries(sentinel_test_integration PRIVATE ws2_32)
else()
    target_link_libraries(sentinel_test_integration PRIVATE pthread dl)
endif()

add_test(NAME sentinel_integration_tests COMMAND sentinel_test_integration)
