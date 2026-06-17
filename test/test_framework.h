#pragma once

// A tiny, zero-dependency unit-test harness. In keeping with this project's
// "bundle only what you need" approach (it vendors ImGui rather than pulling a
// package manager), we use a ~70-line framework instead of GoogleTest/Catch2.
//
// Usage:
//   #include "test_framework.h"
//   TEST_CASE(my_thing_works) {
//       CHECK(1 + 1 == 2);
//       CHECK_EQ(answer(), 42);
//       CHECK_NEAR(pi(), 3.14159, 1e-4);
//       CHECK_STR_EQ(name(), "Ada");
//   }
// One translation unit (test_main.cpp) calls tf::runAll() from main().

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

namespace tf {

struct TestCase {
    const char* name;
    void (*fn)();
};

// Single shared registry across all translation units (inline => one instance).
inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

inline int& currentFails() { static int f = 0; return f; }
inline int& totalChecks() { static int c = 0; return c; }

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

inline void reportFail(const char* file, int line, const char* expr) {
    currentFails()++;
    std::printf("    FAIL %s:%d: %s\n", file, line, expr);
}

inline int runAll() {
    int passed = 0, failed = 0;
    for (const auto& tc : registry()) {
        currentFails() = 0;
        tc.fn();
        if (currentFails() == 0) {
            std::printf("[ PASS ] %s\n", tc.name);
            ++passed;
        } else {
            std::printf("[ FAIL ] %s (%d failed check(s))\n", tc.name, currentFails());
            ++failed;
        }
    }
    std::printf("\n%d passed, %d failed, %d checks total\n",
                passed, failed, totalChecks());
    return failed == 0 ? 0 : 1;
}

}  // namespace tf

#define TEST_CASE(testname)                                              \
    static void testname();                                              \
    static tf::Registrar tf_reg_##testname(#testname, &testname);        \
    static void testname()

#define CHECK(cond)                                                      \
    do {                                                                 \
        tf::totalChecks()++;                                             \
        if (!(cond)) tf::reportFail(__FILE__, __LINE__, "CHECK(" #cond ")"); \
    } while (0)

#define CHECK_FALSE(cond) CHECK(!(cond))

#define CHECK_EQ(a, b)                                                   \
    do {                                                                 \
        tf::totalChecks()++;                                             \
        if (!((a) == (b)))                                               \
            tf::reportFail(__FILE__, __LINE__, "CHECK_EQ(" #a ", " #b ")"); \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                            \
    do {                                                                 \
        tf::totalChecks()++;                                             \
        if (std::fabs((double)(a) - (double)(b)) > (eps))               \
            tf::reportFail(__FILE__, __LINE__, "CHECK_NEAR(" #a ", " #b ")"); \
    } while (0)

#define CHECK_STR_EQ(a, b)                                              \
    do {                                                                 \
        tf::totalChecks()++;                                             \
        if (std::string(a) != std::string(b))                           \
            tf::reportFail(__FILE__, __LINE__, "CHECK_STR_EQ(" #a ", " #b ")"); \
    } while (0)
