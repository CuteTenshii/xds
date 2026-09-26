#pragma once

#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

/// A minimal test harness so the library needs no test framework to build.
namespace check {

struct TestCase {
    const char* name;
    std::function<void()> body;
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> body) { registry().push_back({name, std::move(body)}); }
};

class Failure : public std::exception {
public:
    explicit Failure(std::string message)
        : m_message(std::move(message))
    {
    }
    const char* what() const noexcept override { return m_message.c_str(); }

private:
    std::string m_message;
};

inline int runAll()
{
    int failed = 0;
    for (const TestCase& test : registry()) {
        try {
            test.body();
            std::cout << "PASS " << test.name << '\n';
        } catch (const std::exception& e) {
            std::cout << "FAIL " << test.name << ": " << e.what() << '\n';
            ++failed;
        }
    }
    std::cout << registry().size() - static_cast<std::size_t>(failed) << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}

} // namespace check

#define CHECK_CONCAT_INNER(a, b) a##b
#define CHECK_CONCAT(a, b) CHECK_CONCAT_INNER(a, b)

#define TEST_CASE(name)                                                                                              \
    static void CHECK_CONCAT(test_body_, __LINE__)();                                                                \
    static const check::Registrar CHECK_CONCAT(test_registrar_, __LINE__)(name, CHECK_CONCAT(test_body_, __LINE__)); \
    static void CHECK_CONCAT(test_body_, __LINE__)()

#define CHECK(condition)                                                                                             \
    do {                                                                                                             \
        if (!(condition)) {                                                                                          \
            std::ostringstream check_message;                                                                        \
            check_message << __FILE__ << ':' << __LINE__ << ": " << #condition;                                      \
            throw check::Failure(check_message.str());                                                               \
        }                                                                                                            \
    } while (false)

#define CHECK_EQ(actual, expected)                                                                                   \
    do {                                                                                                             \
        const auto check_actual = (actual);                                                                          \
        const auto check_expected = (expected);                                                                      \
        if (!(check_actual == check_expected)) {                                                                     \
            std::ostringstream check_message;                                                                        \
            check_message << __FILE__ << ':' << __LINE__ << ": " << #actual << " == " << #expected << " (got "       \
                          << +check_actual << ", expected " << +check_expected << ')';                               \
            throw check::Failure(check_message.str());                                                               \
        }                                                                                                            \
    } while (false)

#define CHECK_THROWS(expression)                                                                                     \
    do {                                                                                                             \
        bool check_threw = false;                                                                                    \
        try {                                                                                                        \
            (void)(expression);                                                                                      \
        } catch (const std::exception&) {                                                                            \
            check_threw = true;                                                                                      \
        }                                                                                                            \
        if (!check_threw) {                                                                                          \
            std::ostringstream check_message;                                                                        \
            check_message << __FILE__ << ':' << __LINE__ << ": expected " << #expression << " to throw";             \
            throw check::Failure(check_message.str());                                                               \
        }                                                                                                            \
    } while (false)
