#include <app/Schedule.h>

#include <catch2/catch_test_macros.hpp>

using tf::app::DailyWindow;
using tf::app::formatClock;
using tf::app::parseClock;

TEST_CASE("A daytime window plays between its times", "[installation]")
{
    const DailyWindow w { 9 * 60, 18 * 60 };
    CHECK_FALSE(w.contains(8 * 60 + 59));
    CHECK(w.contains(9 * 60));
    CHECK(w.contains(17 * 60 + 59));
    CHECK_FALSE(w.contains(18 * 60));
    CHECK(w.minutesUntilChange(8 * 60) == 60);
    CHECK(w.minutesUntilChange(17 * 60) == 60);
}

TEST_CASE("An overnight window wraps past midnight", "[installation]")
{
    const DailyWindow w { 22 * 60, 2 * 60 };
    CHECK(w.contains(23 * 60));
    CHECK(w.contains(0));
    CHECK(w.contains(60));
    CHECK_FALSE(w.contains(2 * 60));
    CHECK_FALSE(w.contains(12 * 60));
    CHECK(w.minutesUntilChange(23 * 60) == 3 * 60);
    CHECK(w.minutesUntilChange(12 * 60) == 10 * 60);
}

TEST_CASE("Equal start and stop times play all day", "[installation]")
{
    const DailyWindow w { 600, 600 };
    for (int m = 0; m < DailyWindow::kMinutesPerDay; m += 37)
        CHECK(w.contains(m));
    CHECK(w.minutesUntilChange(0) == -1);
}

TEST_CASE("Clock times parse strictly and print back", "[installation]")
{
    CHECK(parseClock("09:30") == 570);
    CHECK(parseClock(" 23:59 ") == 1439);
    CHECK(parseClock("0:05") == 5);
    CHECK_FALSE(parseClock("24:00").has_value());
    CHECK_FALSE(parseClock("12:60").has_value());
    CHECK_FALSE(parseClock("12:5").has_value());
    CHECK_FALSE(parseClock("noon").has_value());
    CHECK_FALSE(parseClock(":30").has_value());
    CHECK(formatClock(570) == "09:30");
    CHECK(formatClock(-1) == "23:59");
}
