#include <engine/control/SpscQueue.h>

#include <catch2/catch_test_macros.hpp>

#include <thread>

using tf::engine::SpscQueue;

TEST_CASE("SpscQueue preserves order and reports full/empty")
{
    SpscQueue<int> q(3);
    int out = 0;
    REQUIRE_FALSE(q.pop(out));
    REQUIRE(q.push(1));
    REQUIRE(q.push(2));
    REQUIRE(q.push(3));
    REQUIRE_FALSE(q.push(4));
    REQUIRE(q.pop(out));
    REQUIRE(out == 1);
    REQUIRE(q.push(4));
    for (int expected : { 2, 3, 4 })
    {
        REQUIRE(q.pop(out));
        REQUIRE(out == expected);
    }
    REQUIRE_FALSE(q.pop(out));
}

TEST_CASE("SpscQueue delivers every item across threads in order")
{
    constexpr int kItems = 1'000'000;
    SpscQueue<int> q(1024);

    std::thread producer([&] {
        for (int i = 0; i < kItems; ++i)
            while (! q.push(i))
                std::this_thread::yield();
    });

    int expected = 0;
    bool inOrder = true;
    while (expected < kItems)
    {
        int v = 0;
        if (q.pop(v))
        {
            inOrder = inOrder && v == expected;
            ++expected;
        }
    }
    producer.join();
    REQUIRE(inOrder);
}
