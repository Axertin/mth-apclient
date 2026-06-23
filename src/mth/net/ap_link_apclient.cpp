#include "mth/net/ap_link_apclient.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <exception>
#include <list>
#include <string>
#include <utility>

#include <apclient.hpp>
#include <apuuid.hpp>

#include "mth/net/deathlink.hpp"
#include "pal/pal_cert.hpp"
#include "pal/pal_log.hpp"

namespace
{
constexpr const char *kGameName = "Mina The Hollower"; // placeholder until apworld is named
constexpr int kItemHandling = 0b111;                   // remote + own-world + starting inventory

std::string build_uri(const std::string &server)
{
    if (server.starts_with("ws://") || server.starts_with("wss://"))
        return server;
    if (server.starts_with("localhost") || server.starts_with("127.0.0.1"))
        return "ws://" + server;
    return "wss://" + server;
}
} // namespace

namespace mth::net
{

ApLink::ApLink() : thread_([this] { run(); })
{
}

ApLink::~ApLink()
{
    running_.store(false);
    if (thread_.joinable())
        thread_.join();
}

void ApLink::enqueue(std::function<void()> cmd)
{
    std::lock_guard<std::mutex> lock(cmd_mutex_);
    commands_.push(std::move(cmd));
}

void ApLink::push_event(mth::ApEvent ev)
{
    std::lock_guard<std::mutex> lock(event_mutex_);
    events_.push_back(std::move(ev));
}

std::vector<mth::ApEvent> ApLink::drain_events()
{
    std::lock_guard<std::mutex> lock(event_mutex_);
    return std::exchange(events_, {});
}

bool ApLink::is_connected() const
{
    return connected_.load();
}

void ApLink::connect(const std::string &server, const std::string &slot, const std::string &password)
{
    enqueue([this, server, slot, password] { do_connect(server, slot, password); });
}

void ApLink::disconnect()
{
    enqueue([this] { do_disconnect(); });
}

void ApLink::send_locations(const std::vector<std::int64_t> &location_ids)
{
    enqueue(
        [this, location_ids]
        {
            if (!client_ || location_ids.empty())
                return;
            std::list<int64_t> ids(location_ids.begin(), location_ids.end());
            try
            {
                client_->LocationChecks(ids);
            }
            catch (const std::exception &e)
            {
                pal::logf(pal::LogLevel::Warn, "ApLink: LocationChecks failed: %s", e.what());
            }
        });
}

void ApLink::set_goal()
{
    enqueue(
        [this]
        {
            if (!client_)
                return;
            try
            {
                client_->StatusUpdate(APClient::ClientStatus::GOAL);
            }
            catch (const std::exception &e)
            {
                pal::logf(pal::LogLevel::Warn, "ApLink: StatusUpdate(GOAL) failed: %s", e.what());
            }
        });
}

void ApLink::enable_deathlink(bool on)
{
    deathlink_.store(on);
}

void ApLink::send_death(const std::string &cause)
{
    enqueue(
        [this, cause]
        {
            if (!client_ || !deathlink_.load())
                return;
            const double now = static_cast<double>(std::time(nullptr));
            nlohmann::json data = nlohmann::json::parse(mth::net::make_deathlink_payload(slot_name_, cause, now));
            std::list<std::string> tags{"DeathLink"};
            try
            {
                client_->Bounce(data, {}, {}, tags);
                pal::logf(pal::LogLevel::Info, "deathlink: sent bounce (cause=%s)", cause.c_str());
            }
            catch (const std::exception &e)
            {
                pal::logf(pal::LogLevel::Warn, "deathlink: Bounce failed: %s", e.what());
            }
        });
}

void ApLink::report_area(int game_state)
{
    enqueue(
        [this, game_state]
        {
            if (!client_)
                return;
            try
            {
                APClient::DataStorageOperation op;
                op.operation = "replace";
                op.value = game_state;
                const std::string key = "MTH_level_" + std::to_string(client_->get_team_number()) + "_" + std::to_string(client_->get_player_number());
                client_->Set(key, 0, false, {op});
            }
            catch (const std::exception &e)
            {
                pal::logf(pal::LogLevel::Warn, "ApLink: area Set failed: %s", e.what());
            }
        });
}

void ApLink::run()
{
    using namespace std::chrono_literals;
    while (running_.load())
    {
        for (;;)
        {
            std::function<void()> cmd;
            {
                std::lock_guard<std::mutex> lock(cmd_mutex_);
                if (commands_.empty())
                    break;
                cmd = std::move(commands_.front());
                commands_.pop();
            }
            cmd();
        }

        if (client_)
        {
            try
            {
                client_->poll();
            }
            catch (const std::exception &e)
            {
                pal::logf(pal::LogLevel::Error, "ApLink: poll failed: %s", e.what());
                if (connected_.exchange(false))
                    push_event(mth::ApDisconnected{});
                client_.reset();
            }
        }
        std::this_thread::sleep_for(10ms);
    }
    do_disconnect();
}

void ApLink::do_connect(const std::string &server, const std::string &slot, const std::string &password)
{
    do_disconnect();
    slot_name_ = slot;

    if (server.empty() || slot.empty())
    {
        push_event(mth::ApStatusChanged{"AP: server and slot are required"});
        return;
    }

    const std::string uri = build_uri(server);
    std::string cert;
    if (uri.starts_with("wss://"))
    {
        const auto ca = pal::ca_bundle_path();
        if (!ca)
        {
            push_event(mth::ApStatusChanged{"AP: no CA bundle found for wss (set MTHAP_AP_CERT)"});
            return;
        }
        cert = ca->string();
    }

    try
    {
        const std::string uuid = ap_get_uuid((pal::log_dir() / "ap_uuid").string(), server);
        client_ = std::make_unique<APClient>(uuid, kGameName, uri, cert);
        setup_handlers(slot, password);
        push_event(mth::ApStatusChanged{"AP: connecting..."});
        pal::logf(pal::LogLevel::Info, "ApLink: connecting to %s", uri.c_str());
    }
    catch (const std::exception &e)
    {
        push_event(mth::ApStatusChanged{std::string("AP: connect failed: ") + e.what()});
        client_.reset();
    }
}

void ApLink::do_disconnect()
{
    if (!client_)
        return;
    // socket_disconnected handler already emits on server drops; guard avoids duplicate.
    const bool was_connected = connected_.exchange(false);
    client_.reset();
    if (was_connected)
        push_event(mth::ApDisconnected{});
}

void ApLink::setup_handlers(const std::string &slot, const std::string &password)
{
    client_->set_socket_disconnected_handler(
        [this]
        {
            connected_.store(false);
            push_event(mth::ApDisconnected{});
        });

    client_->set_room_info_handler(
        [this, slot, password]
        {
            std::list<std::string> tags;
            if (deathlink_.load())
                tags.push_back("DeathLink");
            client_->ConnectSlot(slot, password, kItemHandling, tags);
        });

    client_->set_slot_connected_handler(
        [this](const nlohmann::json &data)
        {
            connected_.store(true);
            std::list<std::string> tags;
            if (deathlink_.load())
                tags.push_back("DeathLink");
            client_->ConnectUpdate(false, kItemHandling, true, tags);
            client_->StatusUpdate(APClient::ClientStatus::PLAYING);

            auto missing = client_->get_missing_locations();
            auto checked = client_->get_checked_locations();
            const bool ossex_start = data.is_object() && data.value("ossex_start", 0) != 0;
            const bool kear_rando = data.is_object() && data.value("kear_rando", 0) != 0;
            push_event(mth::ApConnected{client_->get_seed(), data.is_null() ? std::string{} : data.dump(), client_->get_player_number(),
                                        std::vector<std::int64_t>(checked.begin(), checked.end()), std::vector<std::int64_t>(missing.begin(), missing.end()),
                                        ossex_start, kear_rando});
        });

    client_->set_slot_refused_handler(
        [this](const std::list<std::string> &errors)
        {
            connected_.store(false);
            push_event(mth::ApConnectionRefused{std::vector<std::string>(errors.begin(), errors.end())});
        });

    client_->set_items_received_handler(
        [this](const std::list<APClient::NetworkItem> &items)
        {
            for (const auto &item : items)
            {
                if (item.index <= last_item_index_)
                    continue;
                push_event(mth::ApItemReceived{mth::ReceivedItem{item.item, item.index, item.player, static_cast<unsigned>(item.flags)}});
                last_item_index_ = item.index;
            }
        });

    client_->set_bounced_handler(
        [this](const nlohmann::json &cmd)
        {
            if (!deathlink_.load())
                return;
            if (auto t = cmd.find("tags"); t == cmd.end() || std::find(t->begin(), t->end(), "DeathLink") == t->end())
                return;
            std::string payload = cmd.contains("data") ? cmd["data"].dump() : std::string{};
            auto dl = mth::net::parse_deathlink_payload(payload);
            if (dl && dl->source == slot_name_)
                return; // our own death echoed back by the server; ignore
            std::string cause = dl ? dl->cause : std::string{};
            push_event(mth::ApDeathReceived{cause});
            pal::logf(pal::LogLevel::Info, "deathlink: received bounce (cause=%s)", cause.c_str());
        });
}

} // namespace mth::net
