
#pragma once
#include <cmath>
#include <cstdio>
#include <exception>
#include <vector>

namespace TestFramework {

    struct TestCase {
        const char* name;
        void (*fn)();
    };

    inline std::vector<TestCase>& registry() {
        static std::vector<TestCase> tests;
        return tests;
    }

    inline int& failure_count() {
        static int failures = 0;
        return failures;
    }

    struct Registrar {
        Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
    };

    inline int run_all() {
        int failed_tests = 0;
        for (const auto& test : registry()) {
            const int before = failure_count();
            try {
                test.fn();
            } catch (const std::exception& e) {
                ++failure_count();
                std::printf("  exception inattendue : %s\n", e.what());
            } catch (...) {
                ++failure_count();
                std::printf("  exception inattendue (type inconnu)\n");
            }
            const bool ok = (failure_count() == before);
            if (!ok) ++failed_tests;
            std::printf("[%s] %s\n", ok ? " OK  " : "ECHEC", test.name);
        }
        std::printf("\n%zu tests, %d en echec, %d verifications en echec\n",
                    registry().size(), failed_tests, failure_count());
        return failed_tests == 0 ? 0 : 1;
    }
}

#define TEST_CASE(name)                                                        \
    static void name();                                                        \
    static const TestFramework::Registrar name##_registrar(#name, &name);      \
    static void name()

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ++TestFramework::failure_count();                                  \
            std::printf("  %s:%d : CHECK(%s) a echoue\n", __FILE__, __LINE__, #cond); \
        }                                                                      \
    } while (0)

#define CHECK_CLOSE(actual, expected, tol)                                     \
    do {                                                                       \
        const double a_ = (actual), e_ = (expected);                           \
        if (!(std::abs(a_ - e_) <= (tol))) {                                   \
            ++TestFramework::failure_count();                                  \
            std::printf("  %s:%d : %s = %.12g, attendu %.12g (tol %.1e)\n",    \
                        __FILE__, __LINE__, #actual, a_, e_, (double)(tol));   \
        }                                                                      \
    } while (0)

#define CHECK_THROWS(expr, ExceptionType)                                      \
    do {                                                                       \
        bool thrown_ = false;                                                  \
        try { (void)(expr); } catch (const ExceptionType&) { thrown_ = true; } \
        if (!thrown_) {                                                        \
            ++TestFramework::failure_count();                                  \
            std::printf("  %s:%d : %s n'a pas leve %s\n", __FILE__, __LINE__, #expr, #ExceptionType); \
        }                                                                      \
    } while (0)
