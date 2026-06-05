#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "pal/pal_log.hpp"

namespace
{

class CapturingLog final : public pal::ILog
{
  public:
    void write(pal::LogLevel level, std::string_view message) override
    {
        std::lock_guard<std::mutex> lock(mu_);
        entries_.emplace_back(level, std::string(message));
    }
    void flush() override
    {
    }

    std::vector<std::pair<pal::LogLevel, std::string>> entries()
    {
        std::lock_guard<std::mutex> lock(mu_);
        return entries_;
    }

  private:
    std::mutex mu_;
    std::vector<std::pair<pal::LogLevel, std::string>> entries_;
};

} // namespace

TEST_CASE("log: injected sink captures formatted output", "[pal][log]")
{
    CapturingLog sink;
    pal::set_default_log(&sink);

    pal::logf(pal::LogLevel::Info, "hello %s %d", "world", 42);
    pal::logf(pal::LogLevel::Warn, "count=%d", 7);

    auto entries = sink.entries();
    REQUIRE(entries.size() == 2);
    REQUIRE(entries[0].first == pal::LogLevel::Info);
    REQUIRE(entries[0].second == "hello world 42");
    REQUIRE(entries[1].first == pal::LogLevel::Warn);
    REQUIRE(entries[1].second == "count=7");

    pal::set_default_log(nullptr);
}

TEST_CASE("log: null sink swallows messages after reset", "[pal][log]")
{
    pal::set_default_log(nullptr);
    pal::logf(pal::LogLevel::Debug, "nobody home");
    SUCCEED("no crash when default log is reset");
}

TEST_CASE("log observer receives every formatted line", "[pal][log]")
{
    std::vector<std::pair<pal::LogLevel, std::string>> seen;
    pal::set_log_observer([&seen](pal::LogLevel lvl, std::string_view msg) { seen.emplace_back(lvl, std::string(msg)); });

    pal::logf(pal::LogLevel::Warn, "hello %d", 42);
    pal::logf(pal::LogLevel::Info, "world");

    REQUIRE(seen.size() == 2);
    REQUIRE(seen[0].first == pal::LogLevel::Warn);
    REQUIRE(seen[0].second == "hello 42");
    REQUIRE(seen[1].first == pal::LogLevel::Info);
    REQUIRE(seen[1].second == "world");

    pal::set_log_observer(nullptr);
    pal::logf(pal::LogLevel::Info, "ignored");
    REQUIRE(seen.size() == 2);
}
