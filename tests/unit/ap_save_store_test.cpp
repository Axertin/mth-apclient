#include <catch2/catch_test_macros.hpp>

#include "mth/core/save/ap_save_store.hpp"

TEST_CASE("save key parts are sanitized to filesystem-safe text", "[savestore]")
{
    REQUIRE(mth::sanitize_save_key_part("Mina") == "Mina");
    REQUIRE(mth::sanitize_save_key_part("my slot") == "my_slot");
    REQUIRE(mth::sanitize_save_key_part("a/b\\c") == "a_b_c");
    REQUIRE(mth::sanitize_save_key_part("..") == "__");
    REQUIRE(mth::sanitize_save_key_part("") == "unnamed");
}

TEST_CASE("save filename combines seed and slot", "[savestore]")
{
    REQUIRE(mth::ap_save_filename("SEED123", "Mina") == "ap_SEED123_Mina_c28d5524018af663.ycsave");
    REQUIRE(mth::ap_save_filename("SEED 1", "my slot") == "ap_SEED_1_my_slot_7db9e5f646585915.ycsave");
}

TEST_CASE("save filename hash avoids sanitization collisions", "[savestore]")
{
    // "a b" and "a_b" sanitize identically, but the raw key parts differ, so the hash suffix
    // must keep the resulting filenames distinct.
    REQUIRE(mth::ap_save_filename("a b", "x") != mth::ap_save_filename("a_b", "x"));
}

TEST_CASE("blob validation requires the ycData header", "[savestore]")
{
    REQUIRE(mth::looks_like_save_blob("[YCD Version: 1]\nSaveSlot\n{}"));
    REQUIRE_FALSE(mth::looks_like_save_blob(""));
    REQUIRE_FALSE(mth::looks_like_save_blob("garbage"));
    REQUIRE_FALSE(mth::looks_like_save_blob("[YCD Version: 1]")); // header but no SaveSlot body
}
