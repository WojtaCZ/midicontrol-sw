#include "midicontrol/process.hpp"

#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <thread>

extern char** environ;

namespace mc::proc {

int spawn(const std::vector<std::string>& args) {
    if (args.empty()) return -1;
    std::vector<char*> argv;
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    // Empty signal mask and default dispositions for the termination signals,
    // whatever the calling thread has blocked or ignored.
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    sigset_t none, defaults;
    sigemptyset(&none);
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGTERM);
    sigaddset(&defaults, SIGINT);
    sigaddset(&defaults, SIGPIPE);
    posix_spawnattr_setsigmask(&attr, &none);
    posix_spawnattr_setsigdefault(&attr, &defaults);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    pid_t pid = -1;
    int rc = posix_spawnp(&pid, argv[0], nullptr, &attr, argv.data(), environ);
    posix_spawnattr_destroy(&attr);
    return rc == 0 ? pid : -rc;
}

Ended terminate(int pid, int graceMs) {
    if (pid <= 0) return Ended::NotRunning;
    if (kill(pid, SIGTERM) != 0) {
        waitpid(pid, nullptr, WNOHANG);
        return Ended::NotRunning;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(graceMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (waitpid(pid, nullptr, WNOHANG) == pid) return Ended::Exited;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
    return Ended::Killed;
}

}  // namespace mc::proc
