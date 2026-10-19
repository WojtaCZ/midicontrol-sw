#include <iostream>
#include <sstream>
#include <string>

#include "midicontrol/log.hpp"
#include "midicontrol/status.hpp"
#include "test_framework.hpp"

using namespace mc::status;

TEST(log_routes_levels) {
    StatusRegistry reg([] { return int64_t(42); });
    std::ostringstream out, err;
    auto* oldOut = std::cout.rdbuf(out.rdbuf());
    auto* oldErr = std::cerr.rdbuf(err.rdbuf());
    mc::log::setRegistry(&reg);
    mc::log::debug(Node::Base, "d");
    mc::log::info(Node::Software, "i");
    mc::log::warn(Node::Base, "w");
    mc::log::error(Node::Controller, "e");
    mc::log::setRegistry(nullptr);
    mc::log::info(Node::Software, "console only");
    std::cout.rdbuf(oldOut);
    std::cerr.rdbuf(oldErr);

    CHECK_EQ(out.str(), "i\nconsole only\n");
    CHECK_EQ(err.str(), "warning: w\nerror: e\n");
    auto s = reg.snapshot();
    const auto& base = s.node(Node::Base).events;
    CHECK_EQ(base.size(), size_t(2));
    CHECK_EQ(base[0].text, "d");
    CHECK(base[0].level == Level::Debug);
    CHECK_EQ(base[1].text, "w");
    CHECK_EQ(s.node(Node::Software).events.size(), size_t(1));
    CHECK_EQ(s.problems.size(), size_t(2));
}
