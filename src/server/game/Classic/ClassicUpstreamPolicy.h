/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TRINITYCORE_CLASSIC_UPSTREAM_POLICY_H
#define TRINITYCORE_CLASSIC_UPSTREAM_POLICY_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Numeric ids are the Classic 1.60 values of ItemModType, SpellSchoolMask and SMART_EVENT.
// The call sites static_assert them against those enums so a renamed or renumbered constant fails the build.

namespace ClassicUpstream
{
inline constexpr int32_t ItemModSpellHealingDone = 41;
inline constexpr int32_t ItemModSpellDamageDone = 42;
inline constexpr int32_t ItemModClassicPhysicalDamageDone = 83;
inline constexpr int32_t ItemModClassicArcanePenetration = 123;
inline constexpr int32_t ItemModClassicResistanceAllSchools = 124;
inline constexpr uint32_t SpellSchoolMaskNormal = 1u;
inline constexpr uint32_t SpellSchoolMaskMagic = 126u;
inline constexpr uint32_t PlainArrowDisplayId = 5996u;
inline constexpr uint32_t MonsterFireArrowDisplayId = 23211u;
inline constexpr uint32_t GenericAmmoDisplayThreshold = 500000u;

inline constexpr uint32_t SmartEventUpdateIc = 0;
inline constexpr uint32_t SmartEventUpdateOoc = 1;
inline constexpr uint32_t SmartEventUpdate = 60;
inline constexpr uint32_t SmartActionCallTimedActionList = 80;
inline constexpr uint32_t MaxInlineTimedActionListDepth = 4;

inline bool IsSpellHealingOrDamageMod(int32_t statType)
{
    return statType == ItemModSpellHealingDone || statType == ItemModSpellDamageDone;
}

inline int32_t StackClassicTotal(int32_t total, int32_t value, bool apply)
{
    return total + (apply ? value : -value);
}

// ITEM_MOD_SPELL_DAMAGE_DONE adds to magical schools only. A physical-only mask does not qualify.
inline bool SpellDamageBonusApplies(uint32_t schoolMask)
{
    return (schoolMask & SpellSchoolMaskMagic) != 0;
}

// Enchant stat 124 is excluded: enchantment 8203 stores it under the name "Spirit +$k1".
inline bool EnchantStatIsClassicSchoolSkillOrPenetration(int32_t statType)
{
    return statType >= ItemModClassicPhysicalDamageDone && statType <= ItemModClassicArcanePenetration;
}

enum class TemporaryEnchantLevelResult : uint8_t
{
    Allowed = 0,
    TooHighLevel
};

// Poisons, oils, stones and shaman imbues have no minimum weapon level. A cast-item MaxLevel still refuses a weapon above it.
inline TemporaryEnchantLevelResult EvaluateTemporaryEnchantLevel(bool hasCastItem, uint32_t spellMaxLevel, uint32_t itemLevel)
{
    if (hasCastItem && spellMaxLevel > 0 && itemLevel > spellMaxLevel)
        return TemporaryEnchantLevelResult::TooHighLevel;
    return TemporaryEnchantLevelResult::Allowed;
}

inline uint32_t ThrownWeaponAmmoDisplay(uint32_t displayId)
{
    if (!displayId || displayId >= GenericAmmoDisplayThreshold)
        return PlainArrowDisplayId;
    return displayId;
}

inline uint32_t AmmoDisplayOrPlainArrow(uint32_t displayId, bool drawable)
{
    if (!drawable)
        return PlainArrowDisplayId;
    return displayId;
}

struct TimedListEvent
{
    uint32_t eventType = 0;
    uint32_t min = 0;
    uint32_t max = 0;
    uint32_t link = 0;
};

inline bool TimedActionListIsInstant(std::span<TimedListEvent const> entries)
{
    if (entries.empty())
        return false;

    for (TimedListEvent const& entry : entries)
    {
        bool const update = entry.eventType == SmartEventUpdate
            || entry.eventType == SmartEventUpdateIc
            || entry.eventType == SmartEventUpdateOoc;
        if (!update || entry.min || entry.max || entry.link)
            return false;
    }
    return true;
}

inline bool TimedActionListInlineDepthAllowed(uint32_t depth)
{
    return depth < MaxInlineTimedActionListDepth;
}

struct TransmogStoredSlot
{
    uint32_t slot = 0;
    uint32_t option = 0;
    uint32_t appearance = 0;
    uint32_t illusion = 0;
};

struct TransmogStoredSlotMatch
{
    bool found = false;
    uint32_t appearance = 0;
    uint32_t illusion = 0;
};

// Each requested slot is matched from the start of the stored list. A missing slot is an empty old appearance, not the next row.
inline TransmogStoredSlotMatch FindStoredTransmogSlot(std::span<TransmogStoredSlot const> stored, uint32_t slot, uint32_t option)
{
    for (TransmogStoredSlot const& row : stored)
        if (row.slot == slot && row.option == option)
            return { true, row.appearance, row.illusion };
    return {};
}

// Ready behavior: the save proceeds only when the client quote equals the server total. The amount charged is that total.
inline bool TransmogQuotedPriceMatches(uint64_t serverCost, uint64_t clientCost)
{
    return serverCost == clientCost;
}

inline void Expect(std::vector<std::string>& failures, bool condition, char const* message)
{
    if (!condition)
        failures.emplace_back(message);
}

inline std::vector<std::string> RunSelfTest()
{
    std::vector<std::string> failures;

    Expect(failures, IsSpellHealingOrDamageMod(ItemModSpellHealingDone), "healing stat is a classic spell bonus");
    Expect(failures, IsSpellHealingOrDamageMod(ItemModSpellDamageDone), "damage stat is a classic spell bonus");
    Expect(failures, !IsSpellHealingOrDamageMod(45), "plain spell power is not this pair");
    Expect(failures, StackClassicTotal(0, 30, true) == 30, "apply adds the stat");
    Expect(failures, StackClassicTotal(30, 30, false) == 0, "remove subtracts the stat");
    Expect(failures, StackClassicTotal(10, 4, true) == 14, "bonuses stack");
    Expect(failures, !SpellDamageBonusApplies(SpellSchoolMaskNormal), "physical spells do not gain spell-damage-done");
    Expect(failures, SpellDamageBonusApplies(2u), "holy gains spell-damage-done");
    Expect(failures, SpellDamageBonusApplies(SpellSchoolMaskMagic), "every magic school gains spell-damage-done");
    Expect(failures, SpellDamageBonusApplies(SpellSchoolMaskNormal | 4u), "a mixed physical and fire mask still gains it");
    Expect(failures, EnchantStatIsClassicSchoolSkillOrPenetration(ItemModClassicPhysicalDamageDone), "school damage enchant is applied");
    Expect(failures, EnchantStatIsClassicSchoolSkillOrPenetration(ItemModClassicArcanePenetration), "arcane penetration enchant is applied");
    Expect(failures, !EnchantStatIsClassicSchoolSkillOrPenetration(ItemModClassicResistanceAllSchools), "stat 124 stays out of enchant application");
    Expect(failures, !EnchantStatIsClassicSchoolSkillOrPenetration(82), "the stat below the classic range stays out");
    Expect(failures, !EnchantStatIsClassicSchoolSkillOrPenetration(125), "creature-type enchants stay out of this range");

    Expect(failures, EvaluateTemporaryEnchantLevel(false, 0, 1) == TemporaryEnchantLevelResult::Allowed, "a level 1 weapon accepts a poison");
    Expect(failures, EvaluateTemporaryEnchantLevel(true, 0, 1) == TemporaryEnchantLevelResult::Allowed, "no max level does not refuse a low weapon");
    Expect(failures, EvaluateTemporaryEnchantLevel(true, 20, 1) == TemporaryEnchantLevelResult::Allowed, "spell level 20 does not refuse a level 1 weapon");
    Expect(failures, EvaluateTemporaryEnchantLevel(true, 60, 60) == TemporaryEnchantLevelResult::Allowed, "item level equal to the max is allowed");
    Expect(failures, EvaluateTemporaryEnchantLevel(true, 60, 61) == TemporaryEnchantLevelResult::TooHighLevel, "a cast item above MaxLevel is refused");
    Expect(failures, EvaluateTemporaryEnchantLevel(false, 60, 61) == TemporaryEnchantLevelResult::Allowed, "the high-level refusal requires a cast item");

    Expect(failures, ThrownWeaponAmmoDisplay(0) == PlainArrowDisplayId, "a missing thrown display uses the plain arrow");
    Expect(failures, ThrownWeaponAmmoDisplay(GenericAmmoDisplayThreshold) == PlainArrowDisplayId, "a generic thrown display uses the plain arrow");
    Expect(failures, ThrownWeaponAmmoDisplay(1234) == 1234u, "a real thrown display is kept");
    Expect(failures, AmmoDisplayOrPlainArrow(737993u, false) == PlainArrowDisplayId, "ammo without a drawable display uses the plain arrow");
    Expect(failures, AmmoDisplayOrPlainArrow(MonsterFireArrowDisplayId, true) == MonsterFireArrowDisplayId, "a drawable display is not replaced");
    Expect(failures, PlainArrowDisplayId != MonsterFireArrowDisplayId, "the fallback is not the fire arrow");

    Expect(failures, !TimedActionListIsInstant({}), "an empty called list is not run inline");
    TimedListEvent const instantUpdate{ SmartEventUpdate, 0, 0, 0 };
    TimedListEvent const instantOoc{ SmartEventUpdateOoc, 0, 0, 0 };
    Expect(failures, TimedActionListIsInstant(std::span<TimedListEvent const>(&instantUpdate, 1)), "a zero-delay update list is instant");
    Expect(failures, TimedActionListIsInstant(std::array<TimedListEvent, 2>{ instantUpdate, instantOoc }), "every zero-delay update entry keeps the list instant");
    TimedListEvent delayed = instantUpdate;
    delayed.min = 1;
    Expect(failures, !TimedActionListIsInstant(std::span<TimedListEvent const>(&delayed, 1)), "a minimum delay is not instant");
    delayed = instantUpdate;
    delayed.max = 1;
    Expect(failures, !TimedActionListIsInstant(std::span<TimedListEvent const>(&delayed, 1)), "a maximum delay is not instant");
    delayed = instantUpdate;
    delayed.link = 9;
    Expect(failures, !TimedActionListIsInstant(std::span<TimedListEvent const>(&delayed, 1)), "a linked entry is not instant");
    delayed = instantUpdate;
    delayed.eventType = 2;
    Expect(failures, !TimedActionListIsInstant(std::span<TimedListEvent const>(&delayed, 1)), "a non-update event is not instant");
    Expect(failures, TimedActionListInlineDepthAllowed(0) && TimedActionListInlineDepthAllowed(3), "nesting below 4 is allowed");
    Expect(failures, !TimedActionListInlineDepthAllowed(MaxInlineTimedActionListDepth), "nesting of 4 stops");

    TransmogStoredSlot const stored[] = {
        { 1, 0, 11, 21 },
        { 3, 1, 33, 43 },
        { 2, 0, 22, 32 }
    };
    TransmogStoredSlotMatch const head = FindStoredTransmogSlot(stored, 1, 0);
    TransmogStoredSlotMatch const later = FindStoredTransmogSlot(stored, 2, 0);
    TransmogStoredSlotMatch const option = FindStoredTransmogSlot(stored, 3, 1);
    TransmogStoredSlotMatch const missing = FindStoredTransmogSlot(stored, 9, 0);
    Expect(failures, head.found && head.appearance == 11 && head.illusion == 21, "the first stored slot matches");
    Expect(failures, later.found && later.appearance == 22, "a slot stored out of order still matches");
    Expect(failures, option.found && option.illusion == 43, "slot option is part of the match");
    Expect(failures, !missing.found && missing.appearance == 0 && missing.illusion == 0, "a new slot has no previous appearance");
    Expect(failures, TransmogQuotedPriceMatches(210, 210), "a matching client quote is accepted");
    Expect(failures, !TransmogQuotedPriceMatches(210, 0), "a zero client quote does not match a positive server price");
    Expect(failures, !TransmogQuotedPriceMatches(210, 209), "an off-by-one client quote is refused");

    return failures;
}
}

#endif
