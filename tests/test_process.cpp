#include <signal.h>

#include <chrono>
#include <thread>

#include "midicontrol/process.hpp"
#include "test_framework.hpp"

using namespace mc::proc;

namespace {
// The service blocks SIGINT/SIGTERM in every thread (sigwait in main).
struct BlockTerm {
    sigset_t old;
    BlockTerm() {
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGTERM);
        sigaddset(&set, SIGINT);
        pthread_sigmask(SIG_BLOCK, &set, &old);
    }
    ~BlockTerm() { pthread_sigmask(SIG_SETMASK, &old, nullptr); }
};
}  // namespace

TEST(process_child_gets_sigterm_despite_blocked_parent) {
    BlockTerm block;
    int pid = spawn({"sleep", "30"});
    CHECK(pid > 0);
    auto t0 = std::chrono::steady_clock::now();
    CHECK(terminate(pid, 2000) == Ended::Exited);
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(1500));
}

TEST(process_stubborn_child_is_killed) {
    int pid = spawn({"sh", "-c", "trap '' TERM; sleep 30"});
    CHECK(pid > 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));  // let the shell set its trap
    auto t0 = std::chrono::steady_clock::now();
    CHECK(terminate(pid, 300) == Ended::Killed);
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(2000));
}

TEST(process_spawn_missing_program) {
    CHECK(spawn({"/nonexistent/program"}) < 0);
    CHECK(terminate(-1, 10) == Ended::NotRunning);
}
