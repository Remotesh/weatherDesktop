#include "test_framework.h"
#include "weatherdesktop/ThreadSafeQueue.h"

#include <thread>
#include <chrono>

using namespace wd;

TEST_CASE(queue_push_then_trypop) {
    ThreadSafeQueue<int> q;
    int out = 0;
    CHECK_FALSE(q.tryPop(out));  // empty
    q.push(42);
    CHECK(q.tryPop(out));
    CHECK_EQ(out, 42);
    CHECK_FALSE(q.tryPop(out));  // drained
}

TEST_CASE(queue_fifo_order) {
    ThreadSafeQueue<int> q;
    for (int i = 0; i < 5; ++i) q.push(i);
    for (int i = 0; i < 5; ++i) {
        int out = -1;
        CHECK(q.tryPop(out));
        CHECK_EQ(out, i);
    }
}

TEST_CASE(queue_clear) {
    ThreadSafeQueue<int> q;
    q.push(1);
    q.push(2);
    q.clear();
    int out = 0;
    CHECK_FALSE(q.tryPop(out));
}

TEST_CASE(queue_waitpopfor_timeout_when_empty) {
    ThreadSafeQueue<int> q;
    int out = 0;
    auto start = std::chrono::steady_clock::now();
    bool got = q.waitPopFor(out, std::chrono::milliseconds(20));
    auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK_FALSE(got);
    CHECK(elapsed >= std::chrono::milliseconds(15));  // actually waited
}

TEST_CASE(queue_producer_consumer) {
    ThreadSafeQueue<int> q;
    const int N = 1000;
    std::thread producer([&] {
        for (int i = 0; i < N; ++i) q.push(i);
    });

    long long sum = 0;
    int received = 0;
    while (received < N) {
        int out = 0;
        if (q.waitPopFor(out, std::chrono::milliseconds(500))) {
            sum += out;
            ++received;
        } else {
            break;  // guard against a hang if something is wrong
        }
    }
    producer.join();

    CHECK_EQ(received, N);
    CHECK_EQ(sum, (long long)N * (N - 1) / 2);  // 0 + 1 + ... + (N-1)
}
