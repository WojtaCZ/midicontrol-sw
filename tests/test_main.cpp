#include "test_framework.hpp"

// Single entry point for the test binary. All TEST(...) cases across the other
// translation units register themselves automatically.
int main() {
    return mc::test::runAll();
}
