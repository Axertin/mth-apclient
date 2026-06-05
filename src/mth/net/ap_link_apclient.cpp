#include "mth/net/ap_link_apclient.hpp"

#include <chrono>
#include <exception>
#include <list>
#include <utility>

#include <apclient.hpp>
#include <apuuid.hpp>

#include "pal/pal_cert.hpp"
#include "pal/pal_log.hpp"

namespace
{
// AP game name - placeholder until the Mina apworld defines its name.
constexpr const char *kGameName = "Mina the Hollower";
// items_handling bitmask: remote items + own-world items + starting inventory.
constexpr int kItemHandling = 0b111;

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
    do_disconnect(); // tear down the client on the net thread
}

void ApLink::do_connect(const std::string &server, const std::string &slot, const std::string &password)
{
    do_disconnect();

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
    // Only emit a disconnect event if we were actually connected. The
    // socket-disconnected handler already emits one on server-initiated drops
    // (and leaves client_ alive so apclientpp can reconnect on the next poll),
    // so guarding on connected_ here avoids a duplicate ApDisconnected.
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
            client_->ConnectSlot(slot, password, kItemHandling, tags);
        });

    client_->set_slot_connected_handler(
        [this](const nlohmann::json &data)
        {
            connected_.store(true);
            std::list<std::string> tags;
            // Mirror okami's proven connect flow: (re)assert items-handling/tags
            // after the slot connects. send_items_handling=false, tags empty -
            // effectively confirms the ConnectSlot negotiation.
            client_->ConnectUpdate(false, kItemHandling, true, tags);
            client_->StatusUpdate(APClient::ClientStatus::PLAYING);

            auto missing = client_->get_missing_locations();
            auto checked = client_->get_checked_locations();
            push_event(mth::ApConnected{client_->get_seed(), data.is_null() ? std::string{} : data.dump(), client_->get_player_number(),
                                        std::vector<std::int64_t>(checked.begin(), checked.end()), std::vector<std::int64_t>(missing.begin(), missing.end())});
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
}

} // namespace mth::net
