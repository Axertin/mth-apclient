#pragma once

#include <functional>

namespace mth
{

// Owns the NPCBehavior_SewerCat::OnNPCEvent suppressor that makes the sewer-cat fetch vendor ("Panino")
// uninteractible while should_disable() is true. Installs on construction, removes on destruction.
class SewerCatHooks
{
  public:
    explicit SewerCatHooks(std::function<bool()> should_disable);
    ~SewerCatHooks();

    SewerCatHooks(const SewerCatHooks &) = delete;
    SewerCatHooks &operator=(const SewerCatHooks &) = delete;
};

} // namespace mth
