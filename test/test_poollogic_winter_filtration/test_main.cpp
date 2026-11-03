#include <unity.h>
#include <stdint.h>
#include "Modules/PoolLogicModule/WinterFiltration.h"

void setUp() {}
void tearDown() {}
constexpr uint32_t minute = 60000U;

void test_default_cycle_boundaries()
{
    WinterFiltrationCycle cycle;
    TEST_ASSERT_TRUE(cycle.update(100, true, 15, 1));
    TEST_ASSERT_TRUE(cycle.update(100 + 15 * minute - 1, true, 15, 1));
    TEST_ASSERT_FALSE(cycle.update(100 + 15 * minute, true, 15, 1));
    TEST_ASSERT_FALSE(cycle.update(100 + 60 * minute - 1, true, 15, 1));
    TEST_ASSERT_TRUE(cycle.update(100 + 60 * minute, true, 15, 1));
}
void test_adjustable_duration_and_start_to_start_interval()
{
    WinterFiltrationCycle cycle;
    TEST_ASSERT_TRUE(cycle.update(0, true, 20, 3));
    TEST_ASSERT_FALSE(cycle.update(20 * minute, true, 20, 3));
    TEST_ASSERT_FALSE(cycle.update(179 * minute, true, 20, 3));
    TEST_ASSERT_TRUE(cycle.update(180 * minute, true, 20, 3));
}
void test_millis_rollover_does_not_restart_cycle()
{
    WinterFiltrationCycle cycle;
    const uint32_t start = UINT32_MAX - 10 * minute;
    TEST_ASSERT_TRUE(cycle.update(start, true, 15, 1));
    TEST_ASSERT_TRUE(cycle.update(start + 14 * minute, true, 15, 1));
    TEST_ASSERT_FALSE(cycle.update(start + 15 * minute, true, 15, 1));
    TEST_ASSERT_TRUE(cycle.update(start + 60 * minute, true, 15, 1));
}
void test_reenable_starts_immediately()
{
    WinterFiltrationCycle cycle;
    cycle.update(0, true, 15, 1);
    TEST_ASSERT_FALSE(cycle.update(30 * minute, true, 15, 1));
    TEST_ASSERT_FALSE(cycle.update(31 * minute, false, 15, 1));
    TEST_ASSERT_TRUE(cycle.update(32 * minute, true, 15, 1));
}
void test_invalid_config_is_bounded()
{
    WinterFiltrationCycle cycle;
    TEST_ASSERT_TRUE(cycle.update(0, true, 0, 0));
    TEST_ASSERT_FALSE(cycle.update(minute, true, 0, 0));
    TEST_ASSERT_TRUE(cycle.update(2 * minute, true, UINT16_MAX, 1));
    WinterFiltrationCycle longCycle;
    longCycle.update(0, true, 15, UINT8_MAX);
    TEST_ASSERT_TRUE(longCycle.update(24 * 60 * minute, true, 15, UINT8_MAX));
}
void test_interval_change_starts_new_cycle()
{
    WinterFiltrationCycle cycle;
    cycle.update(0, true, 15, 1);
    TEST_ASSERT_FALSE(cycle.update(30 * minute, true, 15, 1));
    TEST_ASSERT_TRUE(cycle.update(31 * minute, true, 15, 2));
}
void test_winter_replaces_daily_window_even_without_air_sensor()
{
    TEST_ASSERT_FALSE(poolFiltrationDemand(false, true, true, true, true, false, false, 0, -2, 2));
    TEST_ASSERT_TRUE(poolFiltrationDemand(false, true, false, false, true, true, false, 0, -2, 2));
    TEST_ASSERT_TRUE(poolFiltrationDemand(false, true, false, true, false, false, false, 0, -2, 2));
}
void test_freeze_start_and_hold_override_cycle_end()
{
    TEST_ASSERT_TRUE(poolFiltrationDemand(false, true, false, false, true, false, true, -3, -2, 2));
    TEST_ASSERT_TRUE(poolFiltrationDemand(false, true, true, false, true, false, true, 2, -2, 2));
    TEST_ASSERT_FALSE(poolFiltrationDemand(false, true, true, false, true, false, true, 2.1f, -2, 2));
}
void test_full_cycle_through_freeze_and_thaw()
{
    WinterFiltrationCycle cycle;
    bool pump = poolFiltrationDemand(false, true, false, false, true, cycle.update(0, true, 15, 1), true, 5, -2, 2);
    TEST_ASSERT_TRUE(pump);
    pump = poolFiltrationDemand(false, true, pump, false, true, cycle.update(15 * minute, true, 15, 1), true, 0, -2, 2);
    TEST_ASSERT_TRUE(pump);
    pump = poolFiltrationDemand(false, true, pump, false, true, cycle.update(30 * minute, true, 15, 1), true, 3, -2, 2);
    TEST_ASSERT_FALSE(pump);
    TEST_ASSERT_TRUE(poolFiltrationDemand(false, true, pump, false, true, cycle.update(60 * minute, true, 15, 1), true, 3, -2, 2));
}
void test_pressure_safety_and_manual_priority()
{
    TEST_ASSERT_FALSE(poolFiltrationDemand(true, true, true, true, true, true, true, -10, -2, 2));
    TEST_ASSERT_FALSE(poolFiltrationDemand(false, false, false, true, true, true, true, -10, -2, 2));
    TEST_ASSERT_TRUE(poolFiltrationDemand(false, false, true, false, true, false, true, 10, -2, 2));
    TEST_ASSERT_FALSE(poolFiltrationDemand(true, false, true, true, true, true, true, -10, -2, 2));
}
int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_default_cycle_boundaries);
    RUN_TEST(test_adjustable_duration_and_start_to_start_interval);
    RUN_TEST(test_millis_rollover_does_not_restart_cycle);
    RUN_TEST(test_reenable_starts_immediately);
    RUN_TEST(test_invalid_config_is_bounded);
    RUN_TEST(test_interval_change_starts_new_cycle);
    RUN_TEST(test_winter_replaces_daily_window_even_without_air_sensor);
    RUN_TEST(test_freeze_start_and_hold_override_cycle_end);
    RUN_TEST(test_full_cycle_through_freeze_and_thaw);
    RUN_TEST(test_pressure_safety_and_manual_priority);
    return UNITY_END();
}
