#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "mth/core/save/ap_save_bundle.hpp"
#include "mth/core/save/ap_save_store.hpp"

namespace
{
constexpr const char *kBlob = "[YCD Version: 1]\nSaveSlot\n{ m_iFoo: 1 }";

struct Fixture
{
    std::filesystem::path root{std::filesystem::temp_directory_path() / "mthap_bundle_test"};
    std::filesystem::path saves{root / "saves"};

    Fixture()
    {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(saves);
    }
    ~Fixture()
    {
        std::filesystem::remove_all(root);
    }

    [[nodiscard]] mth::ApSaveBundleStore make() const
    {
        return mth::ApSaveBundleStore(saves, {root, saves});
    }

    void write_file(const std::filesystem::path &p, std::string_view text) const
    {
        std::filesystem::create_directories(p.parent_path());
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
    }
};
} // namespace

TEST_CASE("bundle filename combines seed and slot without a hash", "[bundle]")
{
    REQUIRE(mth::ap_bundle_filename("SEED123", "2") == "ap_SEED123_2.zip");
    REQUIRE(mth::ap_bundle_filename("SEED 1", "2") == "ap_SEED_1_2.zip");
    REQUIRE(mth::ap_bundle_filename("", "2") == "ap_unnamed_2.zip");
}

TEST_CASE("bundle round-trips a game save", "[bundle]")
{
    Fixture f;
    auto store = f.make();
    REQUIRE_FALSE(store.load("S", "2").has_value());
    REQUIRE(store.store("S", "2", kBlob));
    REQUIRE(std::filesystem::exists(f.saves / "ap_S_2.zip"));

    auto reader = f.make();
    REQUIRE(reader.load("S", "2").value() == kBlob);
}

TEST_CASE("bundle round-trips ap state", "[bundle]")
{
    Fixture f;
    auto store = f.make();
    REQUIRE_FALSE(store.load_state("S", "2").has_value());
    REQUIRE(store.stage_state("S", "2", "c 1\ng 2\n"));
    // Read-back through the same store is immediate; the disk waits for the game to save.
    REQUIRE(store.load_state("S", "2").value() == "c 1\ng 2\n");
    REQUIRE(store.store("S", "2", kBlob));

    auto reader = f.make();
    REQUIRE(reader.load_state("S", "2").value() == "c 1\ng 2\n");
}

TEST_CASE("bundle keeps both payloads in one container", "[bundle]")
{
    Fixture f;
    auto store = f.make();
    REQUIRE(store.stage_state("S", "2", "c 1\n"));
    REQUIRE(store.store("S", "2", kBlob));

    auto reader = f.make();
    REQUIRE(reader.load("S", "2").value() == kBlob);
    REQUIRE(reader.load_state("S", "2").value() == "c 1\n");
    REQUIRE(std::filesystem::exists(f.saves / "ap_S_2.zip"));
}

TEST_CASE("bundle rejects a malformed game save blob", "[bundle]")
{
    Fixture f;
    auto store = f.make();
    REQUIRE_FALSE(store.store("S", "2", "garbage"));
    REQUIRE_FALSE(std::filesystem::exists(f.saves / "ap_S_2.zip"));
}

TEST_CASE("bundle upgrades from a legacy ycsave", "[bundle]")
{
    Fixture f;
    f.write_file(f.saves / mth::ap_save_filename("S", "2"), kBlob);

    auto store = f.make();
    REQUIRE(store.load("S", "2").value() == kBlob);
    REQUIRE_FALSE(std::filesystem::exists(f.saves / "ap_S_2.zip")); // lazy: a read alone writes nothing

    REQUIRE(store.stage_state("S", "2", "c 1\n"));
    REQUIRE(store.store("S", "2", kBlob));
    REQUIRE(std::filesystem::exists(f.saves / "ap_S_2.zip"));

    auto reader = f.make();
    REQUIRE(reader.load("S", "2").value() == kBlob); // carried into the container
    REQUIRE(reader.load_state("S", "2").value() == "c 1\n");
    REQUIRE(std::filesystem::exists(f.saves / mth::ap_save_filename("S", "2"))); // legacy left untouched
}

TEST_CASE("bundle upgrades from a legacy state file", "[bundle]")
{
    Fixture f;
    f.write_file(f.root / "ap_S_2.state", "c 4\ns 1\n");

    auto store = f.make();
    REQUIRE(store.load_state("S", "2").value() == "c 4\ns 1\n");
    REQUIRE(store.store("S", "2", kBlob));

    auto reader = f.make();
    REQUIRE(reader.load_state("S", "2").value() == "c 4\ns 1\n");
    REQUIRE(reader.load("S", "2").value() == kBlob);
    REQUIRE(std::filesystem::exists(f.root / "ap_S_2.state")); // legacy left untouched
}

TEST_CASE("bundle upgrades from both legacy files at once", "[bundle]")
{
    Fixture f;
    f.write_file(f.saves / mth::ap_save_filename("S", "2"), kBlob);
    f.write_file(f.root / "ap_S_2.state", "c 4\n");

    auto store = f.make();
    REQUIRE(store.stage_state("S", "2", "c 4\ng 9\n"));
    REQUIRE(store.store("S", "2", kBlob));

    auto reader = f.make();
    REQUIRE(reader.load("S", "2").value() == kBlob);
    REQUIRE(reader.load_state("S", "2").value() == "c 4\ng 9\n");
}

TEST_CASE("bundle prefers the container over legacy files", "[bundle]")
{
    Fixture f;
    auto store = f.make();
    REQUIRE(store.store("S", "2", kBlob));

    // A stale legacy file must not win once the container exists.
    f.write_file(f.saves / mth::ap_save_filename("S", "2"), "[YCD Version: 1]\nSaveSlot\n{ stale }");

    auto reader = f.make();
    REQUIRE(reader.load("S", "2").value() == kBlob);
}

TEST_CASE("bundle rotates a backup on rewrite", "[bundle]")
{
    Fixture f;
    auto store = f.make();
    REQUIRE(store.stage_state("S", "2", "c 1\n"));
    REQUIRE(store.store("S", "2", kBlob));
    REQUIRE_FALSE(std::filesystem::exists(f.saves / "ap_S_2.zip.bak")); // nothing to back up yet
    REQUIRE(store.stage_state("S", "2", "c 1\nc 2\n"));
    REQUIRE(store.store("S", "2", kBlob));
    REQUIRE(std::filesystem::exists(f.saves / "ap_S_2.zip.bak"));

    auto reader = f.make();
    REQUIRE(reader.load_state("S", "2").value() == "c 1\nc 2\n");
}

TEST_CASE("bundle backs up once per session, not once per write", "[bundle]")
{
    Fixture f;
    auto store = f.make();
    REQUIRE(store.stage_state("S", "2", "c 1\n"));
    REQUIRE(store.store("S", "2", kBlob));
    REQUIRE(store.stage_state("S", "2", "c 1\nc 2\n")); // rotates the first generation out
    REQUIRE(store.store("S", "2", kBlob));
    const auto backup = f.saves / "ap_S_2.zip.bak";
    REQUIRE(std::filesystem::exists(backup));

    std::ifstream in(backup, std::ios::binary);
    const std::string pinned((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>{});

    // Further writes in the same session must leave that generation alone, or the backup decays to
    // "one location check ago" and protects nothing.
    REQUIRE(store.stage_state("S", "2", "c 1\nc 2\nc 3\n"));
    REQUIRE(store.stage_state("S", "2", "c 1\nc 2\nc 3\nc 4\n"));
    REQUIRE(store.store("S", "2", kBlob));

    std::ifstream after(backup, std::ios::binary);
    const std::string still((std::istreambuf_iterator<char>(after)), std::istreambuf_iterator<char>{});
    REQUIRE(still == pinned);

    auto reader = f.make();
    REQUIRE(reader.load_state("S", "2").value() == "c 1\nc 2\nc 3\nc 4\n");
}

TEST_CASE("bundle recovers from the backup when the container is missing", "[bundle]")
{
    Fixture f;
    auto store = f.make();
    REQUIRE(store.stage_state("S", "2", "c 1\n"));
    REQUIRE(store.store("S", "2", kBlob));
    REQUIRE(store.stage_state("S", "2", "c 1\nc 2\n"));
    REQUIRE(store.store("S", "2", kBlob));
    std::filesystem::remove(f.saves / "ap_S_2.zip");

    auto reader = f.make();
    REQUIRE(reader.load_state("S", "2").value() == "c 1\n"); // the backup holds the previous generation
}

TEST_CASE("bundle falls back to legacy when the container is corrupt", "[bundle]")
{
    Fixture f;
    f.write_file(f.saves / mth::ap_save_filename("S", "2"), kBlob);
    f.write_file(f.saves / "ap_S_2.zip", "this is not a zip");

    auto store = f.make();
    REQUIRE(store.load("S", "2").value() == kBlob);
}

TEST_CASE("bundle treats a foreign manifest as not ours", "[bundle]")
{
    Fixture f;
    auto store = f.make();
    REQUIRE(store.store("a b", "2", kBlob)); // sanitizes to ap_a_b_2.zip

    // "a_b" sanitizes to the same filename but is a different raw seed.
    auto reader = f.make();
    REQUIRE(reader.path_for("a b", "2") == reader.path_for("a_b", "2"));
    REQUIRE_FALSE(reader.load("a_b", "2").has_value());

    auto other = f.make();
    REQUIRE(other.load("a b", "2").value() == kBlob);
}

TEST_CASE("bundle switches cleanly between sessions", "[bundle]")
{
    Fixture f;
    auto store = f.make();
    REQUIRE(store.stage_state("S1", "2", "c 1\n"));
    REQUIRE(store.store("S1", "2", kBlob));

    REQUIRE_FALSE(store.load_state("S2", "3").has_value());
    REQUIRE(store.stage_state("S2", "3", "c 9\n"));
    REQUIRE(store.store("S2", "3", kBlob));

    REQUIRE(store.load_state("S1", "2").value() == "c 1\n");
    REQUIRE(store.load("S1", "2").value() == kBlob);
    REQUIRE(store.load_state("S2", "3").value() == "c 9\n");
}

TEST_CASE("bundle ignores an unsafe legacy state name", "[bundle]")
{
    Fixture f;
    auto store = f.make();
    // A seed carrying a separator must not be turned into a path to read.
    REQUIRE_FALSE(store.load_state("../../etc/passwd", "2").has_value());
}

TEST_CASE("bundle ignores a malformed save entry inside a container", "[bundle]")
{
    Fixture f;
    // A container with a corrupt save entry but an intact state entry, the way a user editing the
    // zip could produce.
    f.write_file(f.saves / "ap_S_2.zip", mth::zip::write({
                                             mth::zip::compress("manifest.json", "{\"format\":1,\"seed\":\"S\",\"slot\":\"2\"}"),
                                             mth::zip::compress("save.ycsave", "not a save at all"),
                                             mth::zip::compress("ap.state", "c 1\n"),
                                         }));

    auto reader = f.make();
    REQUIRE_FALSE(reader.load("S", "2").has_value()); // refused, not staged into the game
    REQUIRE(reader.load_state("S", "2").value() == "c 1\n");
}

TEST_CASE("bundle never deletes a save entry it declined to use", "[bundle]")
{
    Fixture f;
    // A structurally valid container whose save blob the mod does not recognise: exactly what a game
    // update bumping the ycData header version would produce. Ordinary play must not delete it.
    const std::string future = "[YCD Version: 2]\nSaveSlot\n{ m_iFoo: 1 }";
    f.write_file(f.saves / "ap_S_2.zip", mth::zip::write({
                                             mth::zip::compress("manifest.json", "{\"format\":1,\"seed\":\"S\",\"slot\":\"2\"}"),
                                             mth::zip::compress("save.ycsave", future),
                                             mth::zip::compress("ap.state", "c 1\n"),
                                         }));

    auto store = f.make();
    REQUIRE_FALSE(store.load("S", "2").has_value()); // not usable
    // Checking locations stages without writing, so ordinary play cannot reach the container at all.
    REQUIRE(store.stage_state("S", "2", "c 1\nc 2\n"));
    REQUIRE(store.flush());
    REQUIRE(store.stage_state("S", "2", "c 1\nc 2\nc 3\n"));
    REQUIRE(store.flush());
    REQUIRE_FALSE(std::filesystem::exists(f.saves / "ap_S_2.zip.bak"));

    // The container must still carry the unrecognised blob verbatim.
    for (const char *file : {"ap_S_2.zip"})
    {
        std::ifstream in(f.saves / file, std::ios::binary);
        REQUIRE(in.good());
        const std::string image((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>{});
        const auto entries = mth::zip::read(image);
        REQUIRE(entries.has_value());
        bool found = false;
        for (const auto &e : *entries)
            if (e.name == "save.ycsave" && e.data == future)
                found = true;
        REQUIRE(found);
    }
}

TEST_CASE("bundle refuses to overwrite a container belonging to another run", "[bundle]")
{
    Fixture f;
    {
        auto store = f.make();
        REQUIRE(store.stage_state("a b", "2", "c 1\n"));
        REQUIRE(store.store("a b", "2", kBlob)); // sanitizes to ap_a_b_2.zip
        REQUIRE(store.store("a b", "2", kBlob)); // a second generation, so a backup exists to protect
    }

    const auto container = f.saves / "ap_a_b_2.zip";
    const auto backup = f.saves / "ap_a_b_2.zip.bak";
    const auto slurp = [](const std::filesystem::path &p)
    {
        std::ifstream in(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>{});
    };
    const std::string before = slurp(container);
    const std::string backup_before = slurp(backup);

    // "a_b" collides on the sanitized filename. It must not clobber the other run.
    auto other = f.make();
    REQUIRE_FALSE(other.load("a_b", "2").has_value());
    REQUIRE_FALSE(other.stage_state("a_b", "2", "c 9\n"));
    REQUIRE_FALSE(other.store("a_b", "2", kBlob));

    // Byte-identical: the refusal happens before any rotation or write.
    REQUIRE(slurp(container) == before);
    REQUIRE(slurp(backup) == backup_before);

    auto reader = f.make();
    REQUIRE(reader.load("a b", "2").value() == kBlob);
    REQUIRE(reader.load_state("a b", "2").value() == "c 1\n");
}

TEST_CASE("bundle survives a manifest with wrong field types", "[bundle]")
{
    Fixture f;
    // nlohmann throws on a type mismatch, and this runs under a native game hook.
    for (const char *manifest : {"{\"format\":\"1\",\"seed\":\"S\",\"slot\":\"2\"}", "{\"format\":1,\"seed\":7,\"slot\":\"2\"}",
                                 "{\"format\":1,\"seed\":\"S\",\"slot\":[]}", "{\"format\":null}", "not json at all"})
    {
        f.write_file(f.saves / "ap_S_2.zip", mth::zip::write({
                                                 mth::zip::compress("manifest.json", manifest),
                                                 mth::zip::compress("save.ycsave", kBlob),
                                             }));
        auto store = f.make();
        REQUIRE_NOTHROW(store.load("S", "2"));
        REQUIRE_FALSE(store.load("S", "2").has_value()); // unreadable, not adopted
    }
}

TEST_CASE("bundle state writes do not block on the disk", "[bundle]")
{
    Fixture f;
    auto store = f.make();
    // A grant drain acks a whole batch in one call, so this is the shape of a reconnect. It must not
    // turn into one container rewrite per item on the calling thread.
    std::string text;
    for (int i = 0; i < 500; ++i)
    {
        text += "c " + std::to_string(i) + "\n";
        REQUIRE(store.stage_state("S", "2", text));
    }
    REQUIRE(store.flush());
    REQUIRE_FALSE(std::filesystem::exists(f.saves / "ap_S_2.zip"));
    REQUIRE(store.store("S", "2", kBlob));

    // Whatever the writer coalesced away, the final state is what lands.
    auto reader = f.make();
    REQUIRE(reader.load_state("S", "2").value() == text);
    REQUIRE(reader.load("S", "2").value() == kBlob);
}

TEST_CASE("bundle does not drop a queued write when the session changes", "[bundle]")
{
    Fixture f;
    auto store = f.make();
    REQUIRE(store.stage_state("S1", "2", "c 1\n"));
    REQUIRE(store.store("S1", "2", kBlob));
    // Switching keys with a write still owed must not coalesce it away or read stale bytes.
    REQUIRE(store.stage_state("S2", "3", "c 9\n"));
    REQUIRE(store.store("S2", "3", kBlob));
    REQUIRE(store.stage_state("S1", "2", "c 1\nc 2\n"));
    REQUIRE(store.store("S1", "2", kBlob));

    auto reader = f.make();
    REQUIRE(reader.load_state("S1", "2").value() == "c 1\nc 2\n");
    REQUIRE(reader.load_state("S2", "3").value() == "c 9\n");
}

TEST_CASE("bundle game saves are on disk before store returns", "[bundle]")
{
    Fixture f;
    auto store = f.make();
    // The mod is leaked and never runs a destructor, so the game's save has to be the durability
    // point rather than something merely queued.
    REQUIRE(store.store("S", "2", kBlob));
    REQUIRE(std::filesystem::exists(f.saves / "ap_S_2.zip"));

    auto reader = f.make();
    REQUIRE(reader.load("S", "2").value() == kBlob);
}

TEST_CASE("bundle does not publish staged state when destroyed", "[bundle]")
{
    Fixture f;
    {
        auto store = f.make();
        REQUIRE(store.stage_state("S", "2", "c 1\n"));
        REQUIRE(store.store("S", "2", kBlob));
        // Staged after the last game save. Teardown must not hand it a durability point the run
        // itself never got.
        REQUIRE(store.stage_state("S", "2", "c 1\nc 2\n"));
    }

    auto reader = f.make();
    REQUIRE(reader.load_state("S", "2").value() == "c 1\n");
}

TEST_CASE("bundle rewrites a save blob that changed without changing length", "[bundle]")
{
    Fixture f;
    auto store = f.make();
    const std::string first = "[YCD Version: 1]\nSaveSlot\n{ m_iFoo: 1 }";
    const std::string second = "[YCD Version: 1]\nSaveSlot\n{ m_iFoo: 2 }";
    REQUIRE(first.size() == second.size()); // the save is a text format; this is the common case

    REQUIRE(store.store("S", "2", first));
    REQUIRE(store.store("S", "2", second));

    auto reader = f.make();
    REQUIRE(reader.load("S", "2").value() == second);
}

TEST_CASE("bundle rewrites ap state that changed without changing length", "[bundle]")
{
    Fixture f;
    auto store = f.make();
    // Exactly what capturing the AP game slot does: "s 0" becomes "s 1".
    REQUIRE(store.stage_state("S", "2", "c 1\ng 2\ns 0\n"));
    REQUIRE(store.store("S", "2", kBlob));
    REQUIRE(store.stage_state("S", "2", "c 1\ng 2\ns 1\n"));
    REQUIRE(store.store("S", "2", kBlob));

    auto reader = f.make();
    REQUIRE(reader.load_state("S", "2").value() == "c 1\ng 2\ns 1\n");
}

TEST_CASE("bundle keeps the save blob intact across same-length state writes", "[bundle]")
{
    Fixture f;
    auto store = f.make();
    REQUIRE(store.store("S", "2", kBlob));
    // Staged states coalesce into the single commit the game save triggers, and the blob rides
    // along without ever being confused for a changed one.
    for (int i = 0; i < 5; ++i)
        REQUIRE(store.stage_state("S", "2", "c " + std::to_string(i) + "\n"));
    REQUIRE(store.store("S", "2", kBlob));

    auto reader = f.make();
    REQUIRE(reader.load("S", "2").value() == kBlob);
    REQUIRE(reader.load_state("S", "2").value() == "c 4\n");
}

TEST_CASE("bundle rewrites the save blob when it changes", "[bundle]")
{
    Fixture f;
    auto store = f.make();
    REQUIRE(store.store("S", "2", kBlob));

    const std::string updated = "[YCD Version: 1]\nSaveSlot\n{ m_iFoo: 99 }";
    REQUIRE(store.store("S", "2", updated));

    auto reader = f.make();
    REQUIRE(reader.load("S", "2").value() == updated);
}

TEST_CASE("bundle writes no container for state alone", "[bundle]")
{
    Fixture f;
    auto store = f.make();
    REQUIRE(store.stage_state("S", "2", "c 1\n"));
    REQUIRE(store.flush());
    REQUIRE_FALSE(std::filesystem::exists(f.saves / "ap_S_2.zip"));
}

TEST_CASE("bundle commits the ap state that pairs with the stored game save", "[bundle]")
{
    Fixture f;
    {
        auto store = f.make();
        REQUIRE(store.stage_state("S", "2", "g 1\n"));
        REQUIRE(store.store("S", "2", kBlob));
        // Granted after the game saved, so it belongs to a run the container does not describe.
        REQUIRE(store.stage_state("S", "2", "g 1\ng 2\n"));
        REQUIRE(store.load_state("S", "2").value() == "g 1\ng 2\n");
    }

    auto reader = f.make();
    REQUIRE(reader.load_state("S", "2").value() == "g 1\n");
    REQUIRE(reader.load("S", "2").value() == kBlob);
}

TEST_CASE("bundle discards staged state when the session changes", "[bundle]")
{
    Fixture f;
    auto store = f.make();
    REQUIRE(store.store("S", "2", kBlob));
    REQUIRE(store.stage_state("S", "2", "g 5\n"));

    REQUIRE_FALSE(store.load_state("T", "3").has_value());
    REQUIRE_FALSE(store.load_state("S", "2").has_value());
}

TEST_CASE("bundle reports state waiting for a commit", "[bundle]")
{
    Fixture f;
    auto store = f.make();
    REQUIRE_FALSE(store.state_staged());
    REQUIRE(store.stage_state("S", "2", "c 1\n"));
    REQUIRE(store.state_staged());
    REQUIRE(store.store("S", "2", kBlob));
    REQUIRE_FALSE(store.state_staged());

    // A session change drops staged state, so nothing is owed for the run we left.
    REQUIRE(store.stage_state("S", "2", "c 1\nc 2\n"));
    REQUIRE_FALSE(store.load_state("T", "3").has_value());
    REQUIRE_FALSE(store.state_staged());
}
