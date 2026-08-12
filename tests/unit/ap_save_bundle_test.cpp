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
    REQUIRE(store.store_state("S", "2", "c 1\ng 2\n"));

    auto reader = f.make();
    REQUIRE(reader.load_state("S", "2").value() == "c 1\ng 2\n");
}

TEST_CASE("bundle keeps both payloads in one container", "[bundle]")
{
    Fixture f;
    auto store = f.make();
    REQUIRE(store.store("S", "2", kBlob));
    REQUIRE(store.store_state("S", "2", "c 1\n"));

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

    REQUIRE(store.store_state("S", "2", "c 1\n"));
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
    REQUIRE(store.store_state("S", "2", "c 4\ng 9\n"));

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
    REQUIRE(store.store_state("S", "2", "c 1\n"));
    REQUIRE_FALSE(std::filesystem::exists(f.saves / "ap_S_2.zip.bak")); // nothing to back up yet
    REQUIRE(store.store_state("S", "2", "c 1\nc 2\n"));
    REQUIRE(std::filesystem::exists(f.saves / "ap_S_2.zip.bak"));

    auto reader = f.make();
    REQUIRE(reader.load_state("S", "2").value() == "c 1\nc 2\n");
}

TEST_CASE("bundle recovers from the backup when the container is missing", "[bundle]")
{
    Fixture f;
    auto store = f.make();
    REQUIRE(store.store_state("S", "2", "c 1\n"));
    REQUIRE(store.store_state("S", "2", "c 1\nc 2\n"));
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
    REQUIRE(store.store("S1", "2", kBlob));
    REQUIRE(store.store_state("S1", "2", "c 1\n"));

    REQUIRE_FALSE(store.load_state("S2", "3").has_value());
    REQUIRE(store.store_state("S2", "3", "c 9\n"));

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
    {
        auto store = f.make();
        REQUIRE(store.store("S", "2", kBlob));
        REQUIRE(store.store_state("S", "2", "c 1\n"));
    }

    // Rewrite the container by hand with a corrupt save entry but an intact state entry, the way a
    // user editing the zip could.
    const std::string image = mth::zip::write({
        mth::zip::compress("manifest.json", "{\"format\":1,\"seed\":\"S\",\"slot\":\"2\"}"),
        mth::zip::compress("save.ycsave", "not a save at all"),
        mth::zip::compress("ap.state", "c 1\n"),
    });
    f.write_file(f.saves / "ap_S_2.zip", image);

    auto reader = f.make();
    REQUIRE_FALSE(reader.load("S", "2").has_value()); // refused, not staged into the game
    REQUIRE(reader.load_state("S", "2").value() == "c 1\n");
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
