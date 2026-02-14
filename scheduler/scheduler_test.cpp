#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "scheduler.cpp"

int main() {
    using namespace std::chrono;
    std::cout << "[Test] starting scheduler tests\n";

    // Test 1: delayed run
    {
        std::cout << "\n[Test1] delayed run\n";
        Scheduler s(2, 10);
        std::atomic<bool> ran{false};
        auto t0 = Clock::now();
        auto id = s.schedule([&] {
            std::cout << "[Test1] job body executing\n";
            ran = true;
        }, t0 + 100ms, Priority::Normal);
        assert(id.has_value());
        std::this_thread::sleep_for(50ms);
        std::cout << "[Test1] after 50ms ran=" << ran.load() << "\n";
        assert(!ran.load());
        std::this_thread::sleep_for(80ms);
        std::cout << "[Test1] after 130ms ran=" << ran.load() << "\n";
        assert(ran.load());
        s.shutdown(ShutdownMode::Graceful);
    }

    // Test 2: cancel prevents run
    {
        std::cout << "\n[Test2] cancel prevents run\n";
        Scheduler s(1, 10);
        std::atomic<int> count{0};
        auto id = s.schedule([&] {
            ++count;
            std::cout << "[Test2] job body executing count=" << count.load() << "\n";
        }, Clock::now() + 100ms, Priority::Normal);
        assert(id.has_value());
        assert(s.cancel(*id));
        std::this_thread::sleep_for(150ms);
        std::cout << "[Test2] after 150ms count=" << count.load() << "\n";
        assert(count.load() == 0);
        s.shutdown(ShutdownMode::Graceful);
    }

    // Test 3: graceful drains queue
    {
        std::cout << "\n[Test3] graceful drains queue\n";
        Scheduler s(1, 10);
        std::atomic<int> count{0};
        s.schedule([&] {
            ++count;
            std::cout << "[Test3] job1 count=" << count.load() << "\n";
        }, Clock::now(), Priority::Normal);
        s.schedule([&] {
            ++count;
            std::cout << "[Test3] job2 count=" << count.load() << "\n";
        }, Clock::now(), Priority::Normal);
        s.shutdown(ShutdownMode::Graceful);
        std::cout << "[Test3] final count=" << count.load() << "\n";
        assert(count.load() == 2);
    }

    // Test 4: immediate drops pending jobs
    {
        std::cout << "\n[Test4] immediate drops pending jobs\n";
        Scheduler s(1, 10);
        std::atomic<int> count{0};
        s.schedule([&] {
            ++count;
            std::cout << "[Test4] job body executing count=" << count.load() << "\n";
        }, Clock::now() + 300ms, Priority::Normal);
        s.shutdown(ShutdownMode::Immediate);
        std::this_thread::sleep_for(350ms);
        std::cout << "[Test4] after 350ms count=" << count.load() << "\n";
        assert(count.load() == 0);
    }

    std::cout << "\n[Test] all scheduler tests passed\n";
    return 0;
}
