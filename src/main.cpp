#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "midicontrol/comm.hpp"
#include "midicontrol/config.hpp"
#include "midicontrol/console.hpp"
#include "midicontrol/console_exec.hpp"
#include "midicontrol/control_socket.hpp"
#include "midicontrol/diag_api.hpp"
#include "midicontrol/diag_store.hpp"
#include "midicontrol/instance.hpp"
#include "midicontrol/log.hpp"
#include "midicontrol/log_reader.hpp"
#include "midicontrol/midi_ports.hpp"
#include "midicontrol/sd_notify.hpp"
#include "midicontrol/serial_port.hpp"
#include "midicontrol/status.hpp"
#include "midicontrol/web_server.hpp"

using namespace mc;
using mc::status::Node;

namespace {

const char* kUsage =
    "Usage: midicontrol [--config PATH] [--headless] [--cmd \"COMMAND\"]\n"
    "\n"
    "  -c, --config PATH  config file (default: $MIDICONTROL_CONFIG, then\n"
    "                     $XDG_CONFIG_HOME/midicontrol/config.conf, then ./config.conf)\n"
    "      --headless     service mode: no prompt; run until SIGINT/SIGTERM\n"
    "                     (implied when stdin is not a terminal)\n"
    "      --cmd COMMAND  run one command (e.g. \"update base fw-update.bin\") in the\n"
    "                     running service, or standalone if none runs; exit status\n"
    "                     0 on success\n"
    "  -h, --help         this help\n"
    "\n"
    "In a terminal, midicontrol attaches to the running service if there is one\n"
    "(quit detaches), otherwise it runs standalone.\n";

bool fileExists(const std::string& p) {
    struct stat st {};
    return stat(p.c_str(), &st) == 0;
}

// Block until SIGINT/SIGTERM (headless mode). The signals must already be
// blocked in every thread (done before any thread is started).
void waitForTermination(const sigset_t& set) {
    int sig = 0;
    sigwait(&set, &sig);
    mc::log::info(Node::Software, "Received signal " + std::to_string(sig) + ", shutting down.");
}

void interactive(ConsoleExecutor& exec) {
    std::cout << "\nMIDIControl - interactive control\n" << console::kCommandsHelp << "\n";
    const console::Reply out = [](const std::string& text) { std::cout << text << std::flush; };
    std::string line;
    while (true) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        if (exec.execute(line, out, false) == console::ExecResult::Quit) break;
    }
}

// systemd watchdog: WATCHDOG=1 every 5 s, but only while every CommHandler
// thread shows progress (WatchdogSec=30 in the unit). A hung thread therefore
// leads to a restart; the web thread is deliberately not part of the check.
class Watchdog {
public:
    // `extraAlive`: more heartbeats that must show progress (the base log reader thread).
    explicit Watchdog(CommHandler& handler, std::function<bool()> extraAlive = {})
        : _handler(handler), _extraAlive(std::move(extraAlive)), _thread(&Watchdog::loop, this) {}
    ~Watchdog() {
        {
            std::lock_guard<std::mutex> lk(_mutex);
            _stop = true;
        }
        _cv.notify_all();
        _thread.join();
    }

private:
    void loop() {
        bool stalled = false;
        std::unique_lock<std::mutex> lk(_mutex);
        while (!_cv.wait_for(lk, std::chrono::seconds(5), [&] { return _stop; })) {
            if (_handler.threadsAlive(std::chrono::seconds(10)) && (!_extraAlive || _extraAlive())) {
                stalled = false;
                sdNotify("WATCHDOG=1");
            } else if (!stalled) {
                stalled = true;
                mc::log::warn(Node::Software, "a worker thread stalled; withholding the watchdog ping");
            }
        }
    }

    CommHandler& _handler;
    std::function<bool()> _extraAlive;
    std::mutex _mutex;
    std::condition_variable _cv;
    bool _stop = false;
    std::thread _thread;
};

int64_t wallMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

int main(int argc, char** argv) {
    std::string configArg;
    bool headless = false;
    std::optional<std::string> oneShot;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            configArg = argv[++i];
        } else if (arg.rfind("--config=", 0) == 0) {
            configArg = arg.substr(9);
        } else if (arg == "--headless") {
            headless = true;
        } else if (arg == "--cmd" && i + 1 < argc) {
            oneShot = argv[++i];
        } else if (arg.rfind("--cmd=", 0) == 0) {
            oneShot = arg.substr(6);
        } else if (arg == "-h" || arg == "--help") {
            std::cout << kUsage;
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n" << kUsage;
            return 2;
        }
    }
    if (!oneShot && !isatty(STDIN_FILENO)) headless = true;

    // Attach to the running service, if any.
    const std::string runDir = runtimeDir();
    const std::string socketPath = runDir + "/midicontrol.sock";
    if (oneShot || !headless) {
        if (auto rc = runClient(socketPath, oneShot)) return *rc;
    }

    // No service answered: this process is the one (single instance).
    InstanceLock lock;
    if (!lock.acquire(runDir + "/midicontrol.lock")) {
        std::cerr << "error: another midicontrol instance is running (" << runDir
                  << "/midicontrol.lock) but does not answer on " << socketPath << "\n";
        return 1;
    }

    const std::string configPath = resolveConfigPath(
        configArg, [](const char* name) { return std::getenv(name); }, fileExists);
    Config config;
    try {
        config = loadConfig(configPath);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    // In headless mode SIGINT/SIGTERM are taken synchronously by sigwait();
    // block them before any thread exists so every thread inherits the mask.
    sigset_t termSignals;
    sigemptyset(&termSignals);
    sigaddset(&termSignals, SIGINT);
    sigaddset(&termSignals, SIGTERM);
    if (headless) pthread_sigmask(SIG_BLOCK, &termSignals, nullptr);

    mc::status::StatusRegistry status(wallMs);
    mc::log::setRegistry(&status);
    mc::log::info(Node::Software, "Using config file " + configPath);
    status.set(Node::Software, "version", std::string(MC_GIT_HASH));
    status.set(Node::Software, "build_date", std::string(MC_BUILD_DATE));
    status.setNow(Node::Software, "started_ms");
    status.set(Node::Software, "config_path", configPath);
    status.set(Node::Software, "play_folder", config.playFolder);

    // Diagnostics: the store (also shows faults left unacknowledged by an earlier run), the remote's
    // DIAG polling inside CommHandler and the thread reading the base's log CDC.
    mc::diag::DiagStore diagStore(config.diagDir);
    mc::diag::publishFaultStatus(diagStore, status);
    status.set(Node::Software, "diag_dir", config.diagDir);

    CommHandler handler(config, [] { return findMidiControlPorts(); }, status, &diagStore);
    handler.start([] { return findMidiControlPort(); }, findMidiControlPort().value_or(""));
    ConsoleExecutor exec(handler, status, &diagStore);

    mc::diag::LogIngest logIngest(
        diagStore, wallMs,
        [&status](char level, const std::string& text) {
            status.event(Node::Base, level == 'E' ? mc::status::Level::Error : mc::status::Level::Warn, text);
        });
    logIngest.setOnFault([&] { mc::diag::publishFaultStatus(diagStore, status); });
    const std::string logPortOverride = config.diagLogPort;
    mc::diag::SerialLogSource logSource([logPortOverride]() -> std::optional<std::string> {
        if (!logPortOverride.empty()) return access(logPortOverride.c_str(), F_OK) == 0 ? std::optional<std::string>(logPortOverride) : std::nullopt;
        return findMidiControlLogPort();
    });
    mc::diag::LogReader logReader(logSource, logIngest);
    if (!oneShot) logReader.start();  // a one-shot command needs no log

    if (oneShot) {
        // Standalone one-shot: no page, no socket; just this command.
        const console::Reply out = [](const std::string& text) { std::cout << text << std::flush; };
        auto r = exec.execute(*oneShot, out, false);
        handler.shutdown();
        mc::log::setRegistry(nullptr);
        return r == console::ExecResult::Failed ? 1 : 0;
    }

    std::unique_ptr<WebServer> web;
    if (config.webEnabled) {
        web = std::make_unique<WebServer>(status, config.webBind, config.webPort, config.playFolder,
                                              config.organ, &diagStore);
        web->start();
    }
    ControlServer control(
        [&exec](const std::string& line, const console::Reply& out) {
            return exec.execute(line, out, true);
        },
        socketPath);
    control.start();
    // Ready once config is loaded and the page is up; the base may be unplugged.
    sdNotify("READY=1");
    auto watchdog = std::make_unique<Watchdog>(handler, [&logReader] { return logReader.alive(10000); });

    if (headless) {
        mc::log::info(Node::Software, "Running headless; send SIGTERM or SIGINT to stop.");
        waitForTermination(termSignals);
    } else {
        interactive(exec);
    }

    sdNotify("STOPPING=1");
    watchdog.reset();
    control.stop();
    if (web) web->stop();
    logReader.stop();
    handler.shutdown();
    mc::log::setRegistry(nullptr);
    std::cout << "Done.\n";
    return 0;
}
