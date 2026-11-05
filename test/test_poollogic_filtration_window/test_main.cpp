#include <unity.h>
#include <math.h>

#include "Modules/PoolLogicModule/FiltrationWindow.h"

void test_temp_below_low_uses_min_duration()
{
    FiltrationWindowInput in{};
    in.waterTemp = 5.0f;
    in.lowThreshold = 12.0f;
    in.setpoint = 24.0f;
    in.startMinHour = 8;
    in.stopMaxHour = 23;

    FiltrationWindowOutput out{};
    TEST_ASSERT_TRUE(computeFiltrationWindowDeterministic(in, out));
    TEST_ASSERT_EQUAL_UINT8(2, out.durationHours);
    TEST_ASSERT_EQUAL_UINT8(14, out.startHour);
    TEST_ASSERT_EQUAL_UINT8(16, out.stopHour);
}

void test_temp_equal_setpoint_switches_to_high_factor()
{
    FiltrationWindowInput in{};
    in.waterTemp = 24.0f;
    in.lowThreshold = 12.0f;
    in.setpoint = 24.0f;
    in.startMinHour = 8;
    in.stopMaxHour = 23;

    FiltrationWindowOutput out{};
    TEST_ASSERT_TRUE(computeFiltrationWindowDeterministic(in, out));
    // 24 * 0.5 = 12h
    TEST_ASSERT_EQUAL_UINT8(12, out.durationHours);
    TEST_ASSERT_EQUAL_UINT8(9, out.startHour);
    TEST_ASSERT_EQUAL_UINT8(21, out.stopHour);
}

void test_nan_temperature_uses_widest_available_window()
{
    FiltrationWindowInput in{};
    in.waterTemp = NAN;
    in.lowThreshold = 12.0f;
    in.setpoint = 24.0f;
    in.startMinHour = 8;
    in.stopMaxHour = 23;

    FiltrationWindowOutput out{};
    TEST_ASSERT_TRUE(computeFiltrationWindowDeterministic(in, out));
    TEST_ASSERT_EQUAL_UINT8(15, out.durationHours);
    TEST_ASSERT_EQUAL_UINT8(8, out.startHour);
    TEST_ASSERT_EQUAL_UINT8(23, out.stopHour);
}

void test_stop_le_start_uses_emergency_duration_when_possible()
{
    FiltrationWindowInput in{};
    in.waterTemp = 20.0f; // duration about 7h
    in.lowThreshold = 12.0f;
    in.setpoint = 24.0f;
    in.startMinHour = 22;
    in.stopMaxHour = 22; // force stop clamp to start

    FiltrationWindowOutput out{};
    TEST_ASSERT_TRUE(computeFiltrationWindowDeterministic(in, out));
    TEST_ASSERT_EQUAL_UINT8(22, out.startHour);
    TEST_ASSERT_EQUAL_UINT8(23, out.stopHour);
    TEST_ASSERT_EQUAL_UINT8(1, out.durationHours);
}

void test_stop_le_start_with_late_start_uses_fallback_window()
{
    FiltrationWindowInput in{};
    in.waterTemp = 20.0f;
    in.lowThreshold = 12.0f;
    in.setpoint = 24.0f;
    in.startMinHour = 23;
    in.stopMaxHour = 23; // start gets pinned to 23

    FiltrationWindowOutput out{};
    TEST_ASSERT_TRUE(computeFiltrationWindowDeterministic(in, out));
    TEST_ASSERT_EQUAL_UINT8(22, out.startHour);
    TEST_ASSERT_EQUAL_UINT8(23, out.stopHour);
    TEST_ASSERT_EQUAL_UINT8(1, out.durationHours);
}

void test_day_window_is_inactive_after_stop_hour()
{
    TEST_ASSERT_FALSE(isFiltrationWindowActiveAtMinute(8, 23, (uint16_t)((0 * 60) + 18)));
    TEST_ASSERT_TRUE(isFiltrationWindowActiveAtMinute(8, 23, (uint16_t)((22 * 60) + 59)));
    TEST_ASSERT_FALSE(isFiltrationWindowActiveAtMinute(8, 23, (uint16_t)(23 * 60)));
}

void test_overnight_window_wraps_across_midnight()
{
    TEST_ASSERT_FALSE(isFiltrationWindowActiveAtMinute(22, 6, (uint16_t)((21 * 60) + 59)));
    TEST_ASSERT_TRUE(isFiltrationWindowActiveAtMinute(22, 6, (uint16_t)((0 * 60) + 18)));
    TEST_ASSERT_FALSE(isFiltrationWindowActiveAtMinute(22, 6, (uint16_t)(6 * 60)));
}

void test_equal_start_stop_window_is_inactive()
{
    TEST_ASSERT_FALSE(isFiltrationWindowActiveAtMinute(23, 23, (uint16_t)((0 * 60) + 18)));
    TEST_ASSERT_FALSE(isFiltrationWindowActiveAtMinute(23, 23, (uint16_t)(23 * 60)));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_temp_below_low_uses_min_duration);
    RUN_TEST(test_temp_equal_setpoint_switches_to_high_factor);
    RUN_TEST(test_nan_temperature_uses_widest_available_window);
    RUN_TEST(test_stop_le_start_uses_emergency_duration_when_possible);
    RUN_TEST(test_stop_le_start_with_late_start_uses_fallback_window);
    RUN_TEST(test_day_window_is_inactive_after_stop_hour);
    RUN_TEST(test_overnight_window_wraps_across_midnight);
    RUN_TEST(test_equal_start_stop_window_is_inactive);
    return UNITY_END();
}
