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

#include "mth/core/ap/ap_ids.hpp" // kLocBase
#include "mth/core/broadcast.hpp"
#include "mth/core/fountain_lamps.hpp"
#include "mth/core/stat_cap_state.hpp" // clamp_max_stat_level
#include "mth/net/deathlink.hpp"
#include "pal/pal_cert.hpp"
#include "pal/pal_log.hpp"

namespace
{
constexpr const char *kGameName = "Mina The Hollower"; // placeholder until apworld is named
constexpr int kItemHandling = 0b111;                   // remote + own-world + starting inventory
constexpr std::chrono::seconds kConnectTimeout{30};

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

void ApLink::scout_locations(const std::vector<std::int64_t> &location_ids)
{
    enqueue(
        [this, location_ids]
        {
            if (!client_ || location_ids.empty())
                return;
            std::list<int64_t> ids(location_ids.begin(), location_ids.end());
            try
            {
                // create_as_hint=2: hint the scouted shop contents for free (no hint-point cost); the
                // server dedups so re-scouting an in-flight location does not create duplicate hints.
                client_->LocationScouts(ids, 2);
            }
            catch (const std::exception &e)
            {
                pal::logf(pal::LogLevel::Warn, "ApLink: LocationScouts failed: %s", e.what());
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
    // Console override is opt-out only: 'off' is a sticky client-side force-off; 'on' clears the force-off and
    // defers back to slot_data, so it cannot enable deathlink beyond what the seed permits.
    force_off_.store(!on);
    const bool effective = slot_deathlink_.load() && !force_off_.load();
    deathlink_.store(effective);
    enqueue(
        [this, effective]
        {
            if (!client_)
                return;
            try
            {
                std::list<std::string> tags;
                if (effective)
                    tags.push_back("DeathLink");
                client_->ConnectUpdate(false, kItemHandling, true, tags);
            }
            catch (const std::exception &e)
            {
                pal::logf(pal::LogLevel::Warn, "ApLink: ConnectUpdate failed: %s", e.what());
            }
        });
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
                connect_deadline_.reset();
            }
        }
        if (connect_deadline_ && std::chrono::steady_clock::now() > *connect_deadline_)
        {
            connect_deadline_.reset();
            pal::logf(pal::LogLevel::Warn, "ApLink: connect timed out after %llds", static_cast<long long>(kConnectTimeout.count()));
            do_disconnect(); // tears down the half-open client; was_connected is false so no ApDisconnected
            push_event(mth::ApConnectionRefused{{"connection timed out"}});
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
        push_event(mth::ApConnectionRefused{{"server and slot are required"}});
        return;
    }

    const std::string uri = build_uri(server);
    std::string cert;
    if (uri.starts_with("wss://"))
    {
        const auto ca = pal::ca_bundle_path();
        if (!ca)
        {
            push_event(mth::ApConnectionRefused{{"no CA bundle found for wss (set MTHAP_AP_CERT)"}});
            return;
        }
        cert = ca->string();
    }

    try
    {
        const std::string uuid = ap_get_uuid((pal::log_dir() / "ap_uuid").string(), server);
        client_ = std::make_unique<APClient>(uuid, kGameName, uri, cert);
        // A brand-new client means a new session: clear the re-delivery dedup cursor so the next server's
        // items (indices restarting at 0) are not filtered as already-seen. A transient socket drop reuses
        // the existing client instead of calling connect(), so its re-delivered items stay correctly deduped.
        last_item_index_ = -1;
        setup_handlers(slot, password);
        push_event(mth::ApConnecting{});
        connect_deadline_ = std::chrono::steady_clock::now() + kConnectTimeout;
        pal::logf(pal::LogLevel::Info, "ApLink: connecting to %s", uri.c_str());
    }
    catch (const std::exception &e)
    {
        push_event(mth::ApConnectionRefused{{std::string("connect failed: ") + e.what()}});
        client_.reset();
    }
}

void ApLink::do_disconnect()
{
    connect_deadline_.reset();
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
            connect_deadline_.reset();
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
            connect_deadline_.reset();
            // slot_data "death_link" sets the default; a sticky client-side force-off (console) still wins.
            slot_deathlink_.store(data.is_object() && data.value("death_link", 0) != 0);
            const bool deathlink = slot_deathlink_.load() && !force_off_.load();
            deathlink_.store(deathlink);
            std::list<std::string> tags;
            if (deathlink)
                tags.push_back("DeathLink");
            client_->ConnectUpdate(false, kItemHandling, true, tags);
            client_->StatusUpdate(APClient::ClientStatus::PLAYING);

            auto missing = client_->get_missing_locations();
            auto checked = client_->get_checked_locations();
            const bool ossex_start = data.is_object() && data.value("ossex_start", 0) != 0;
            // "kear_rando" is a mode, not a flag: 0 = vanilla (Universal Kear items grant real keys),
            // 1/2 = per-lock / per-area AP items. Absent (older seeds) -> the apworld's own default.
            const mth::KearMode kear_mode = mth::kear_mode_from_slot_data(data.is_object() ? data.value("kear_rando", 1) : 1);
            const bool burrow_rando = data.is_object() && data.value("burrow_rando", 0) != 0;
            const bool swim_rando = data.is_object() && data.value("swim_rando", 0) != 0;
            const bool rope_rando = data.is_object() && data.value("rope_rando", 0) != 0;
            const bool puff_rando = data.is_object() && data.value("puff_rando", 0) != 0;
            const bool spring_rando = data.is_object() && data.value("spring_rando", 0) != 0;
            const bool carry_rando = data.is_object() && data.value("carry_rando", 0) != 0;
            // Default true until the apworld ships a train-rando option: current seeds omit the key but
            // still shuffle the tickets/Train Pass, so gating must be on. Send train_rando=0 to opt out.
            const bool train_rando = data.is_object() && data.value("train_rando", 1) != 0;
            const int max_stat_level = mth::clamp_max_stat_level(data.is_object() ? data.value("max_stat_level", 99) : 99);
            const int goal_config = data.is_object() ? data.value("goal_config", 0) : 0;
            const int goal_generators = data.is_object() ? data.value("goal_generators", 99) : 99;
            const int goal_bosses = data.is_object() ? data.value("goal_bosses", 99) : 99;
            const bool wallet_cap = data.is_object() && data.value("wallet_cap", 0) != 0;
            std::vector<int> lit_gen_indices;
            if (data.is_object())
                if (auto lg = data.find("lit_generators"); lg != data.end() && lg->is_array())
                    for (const auto &v : *lg)
                        if (v.is_number_integer())
                            lit_gen_indices.push_back(v.get<int>());
            for (int i : lit_gen_indices)
                if (i < 0 || i >= mth::kGeneratorLampCount)
                    pal::logf(pal::LogLevel::Warn, "lit_generators: ignoring out-of-range lamp index %d (valid 0..%d)", i, mth::kGeneratorLampCount - 1);
            const std::uint32_t lit_generator_lamp_mask = mth::lit_mask_from_indices(lit_gen_indices);
            // Absent means every generator counts, so a missing key must not collapse to an empty array.
            std::uint64_t broken_generator_mask = mth::kAllGeneratorsMask;
            if (data.is_object())
                if (auto bg = data.find("broken_generators"); bg != data.end() && bg->is_array())
                {
                    std::vector<int> broken_indices;
                    for (const auto &v : *bg)
                        if (v.is_number_integer())
                            broken_indices.push_back(v.get<int>());
                    for (int i : broken_indices)
                        if (i < 0 || i >= mth::kGeneratorLampCount)
                            pal::logf(pal::LogLevel::Warn, "broken_generators: generator index %d is outside the expected 0..%d", i,
                                      mth::kGeneratorLampCount - 1);
                    broken_generator_mask = mth::broken_generator_mask(broken_indices);
                }
            push_event(mth::ApConnected{client_->get_seed(),
                                        data.is_null() ? std::string{} : data.dump(),
                                        client_->get_player_number(),
                                        std::vector<std::int64_t>(checked.begin(), checked.end()),
                                        std::vector<std::int64_t>(missing.begin(), missing.end()),
                                        ossex_start,
                                        kear_mode,
                                        burrow_rando,
                                        swim_rando,
                                        rope_rando,
                                        puff_rando,
                                        spring_rando,
                                        carry_rando,
                                        train_rando,
                                        deathlink,
                                        max_stat_level,
                                        goal_config,
                                        goal_generators,
                                        broken_generator_mask,
                                        goal_bosses,
                                        wallet_cap,
                                        lit_generator_lamp_mask});
        });

    client_->set_slot_refused_handler(
        [this](const std::list<std::string> &errors)
        {
            connected_.store(false);
            connect_deadline_.reset();
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

    // Server-reported checked locations (Connected full set + RoomUpdate deltas): Collect, same-slot coop,
    // connect-time self-heal. Reconciled locally (no re-send); see App::reconcile_server_checked.
    client_->set_location_checked_handler([this](const std::list<std::int64_t> &locations)
                                          { push_event(mth::ApLocationsChecked{std::vector<std::int64_t>(locations.begin(), locations.end())}); });

    // LocationScouts replies: resolve item/player names here (apclientpp resolution is net-thread-only),
    // then ship plain-data ScoutInfo over the event queue for the ScoutRegistry.
    client_->set_location_info_handler(
        [this](const std::list<APClient::NetworkItem> &items)
        {
            std::vector<mth::ScoutInfo> out;
            out.reserve(items.size());
            const int our_slot = client_->get_player_number();
            for (const auto &it : items)
            {
                mth::ScoutInfo si;
                si.collection_slot = static_cast<int>(it.location - mth::kLocBase);
                si.player_game = client_->get_player_game(it.player);
                si.item_name = client_->get_item_name(it.item, si.player_game);
                si.player_alias = client_->get_player_alias(it.player);
                si.item_flags = it.flags;
                si.is_self = (it.player == our_slot);
                out.push_back(std::move(si));
            }
            push_event(mth::ApScoutInfo{std::move(out)});
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

    // Relevant PrintJSON -> banner. Resolve names/colors here (apclientpp resolution is net-thread-only),
    // then ship the segments over the event queue.
    client_->set_print_json_handler(
        [this](const APClient::PrintJSONArgs &args)
        {
            const auto opt = [](const int *p) { return p ? std::optional<int>(*p) : std::nullopt; };
            // args.item->player is the finder: relevant when we sent the check (item destined for another slot).
            const std::optional<int> item_player = args.item ? std::optional<int>(args.item->player) : std::nullopt;
            if (!mth::broadcast_relevant(client_->get_team_number(), client_->get_player_number(), opt(args.team), opt(args.slot), opt(args.receiving),
                                         item_player))
                return;

            std::vector<mth::BannerSegment> segments;
            for (const auto &node : args.data)
            {
                std::string text = client_->render_json(std::list<APClient::TextNode>{node}, APClient::RenderFormat::TEXT);
                if (text.empty())
                    continue;
                bool is_self = false;
                if (node.type == "player_id")
                    try
                    {
                        is_self = std::stoi(node.text) == client_->get_player_number();
                    }
                    catch (const std::exception &)
                    {
                    }
                segments.push_back(mth::BannerSegment{std::move(text), mth::banner_color(node.type, node.color, node.flags, node.hintStatus, is_self)});
            }
            if (segments.empty())
                return;
            push_event(mth::ApPrintBroadcast{std::move(segments)});
        });
}

} // namespace mth::net
