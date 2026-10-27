#ifndef MIDICONTROL_TEST_FRAMEWORK_HPP
#define MIDICONTROL_TEST_FRAMEWORK_HPP

// Tiny self-contained test harness — no external dependencies.
//
// Usage:
//   TEST(name) { CHECK(cond); CHECK_EQ(a, b); }
//   int main() { return mc::test::runAll(); }
//
// Tests register themselves automatically at static-init time.

#include <cstdint>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace mc::test {

struct Case {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}

// Thrown by a failed assertion to abort the current test case.
struct Failure {
    std::string message;
};

struct Registrar {
    Registrar(const std::string& name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

inline std::string toHex(const std::vector<uint8_t>& v) {
    std::ostringstream os;
    os << std::hex;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) os << ' ';
        if (v[i] < 16) os << '0';
        os << static_cast<int>(v[i]);
    }
    return os.str();
}

inline int runAll() {
    int failed = 0;
    int passed = 0;
    for (auto& c : registry()) {
        try {
            c.fn();
            ++passed;
            std::cout << "[ PASS ] " << c.name << "\n";
        } catch (const Failure& f) {
            ++failed;
            std::cout << "[ FAIL ] " << c.name << "\n        " << f.message << "\n";
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "[ FAIL ] " << c.name << "\n        unexpected exception: " << e.what()
                      << "\n";
        }
    }
    std::cout << "\n" << passed << " passed, " << failed << " failed, "
              << registry().size() << " total\n";
    return failed == 0 ? 0 : 1;
}

}  // namespace mc::test

// --- macros ---

#define TEST(test_name)                                                                    \
    static void test_name();                                                               \
    static ::mc::test::Registrar registrar_##test_name(#test_name, test_name);             \
    static void test_name()

#define FAIL_AT(msg)                                                                       \
    do {                                                                                   \
        std::ostringstream _os;                                                            \
        _os << __FILE__ << ":" << __LINE__ << ": " << msg;                                 \
        throw ::mc::test::Failure{_os.str()};                                              \
    } while (0)

#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) FAIL_AT("CHECK failed: " #cond);                                      \
    } while (0)

#define CHECK_EQ(a, b)                                                                     \
    do {                                                                                   \
        auto _va = (a);                                                                    \
        auto _vb = (b);                                                                    \
        if (!(_va == _vb)) {                                                               \
            std::ostringstream _os;                                                        \
            _os << "CHECK_EQ failed: " #a " == " #b " (" << _va << " vs " << _vb << ")";   \
            FAIL_AT(_os.str());                                                            \
        }                                                                                  \
    } while (0)

// Passes when `expr` throws any std::exception.
#define CHECK_THROWS(expr)                                                                 \
    do {                                                                                   \
        bool _threw = false;                                                               \
        try {                                                                              \
            (void)(expr);                                                                  \
        } catch (const std::exception&) {                                                  \
            _threw = true;                                                                 \
        }                                                                                  \
        if (!_threw) FAIL_AT("CHECK_THROWS failed: " #expr " did not throw");              \
    } while (0)

#define CHECK_TRUE(cond) CHECK(cond)
#define CHECK_FALSE(cond) CHECK(!(cond))

#endif  // MIDICONTROL_TEST_FRAMEWORK_HPP
