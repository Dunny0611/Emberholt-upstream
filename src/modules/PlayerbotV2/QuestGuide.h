/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * Server-side quest guide. The live loop fills these facts from quest_template,
 * the starter and ender tables, and spawn rows. Nothing here grants credit.
 */

#ifndef TRINITY_PLAYERBOT_V2_QUEST_GUIDE_H
#define TRINITY_PLAYERBOT_V2_QUEST_GUIDE_H

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Playerbot::V2
{
inline constexpr uint32_t GuideStallSeconds = 60;
inline constexpr uint32_t PlanPathFailLimit = 3;
inline constexpr uint32_t PlanActedRepeatLimit = 5;
inline constexpr uint32_t CrowdLimit = 8;
inline constexpr float CrowdYards = 40.0f;
inline constexpr float PlanSegmentYards = 45.0f;
inline constexpr uint32_t LoginStaggerMs = 750;
inline constexpr uint32_t PlanCooldownMs = 180000;
inline constexpr float GuideStallYards = 5.0f;
inline constexpr uint32_t GuideClaimLimit = 2;
inline constexpr uint32_t GuideSpreadChoices = 3;

enum class GuideTravel : uint8_t
{
    Walk = 0,
    Flight
};

enum class GuidePick : uint8_t
{
    None = 0,
    TurnIn,
    Objective,
    Accept
};

struct GuideQuestFact
{
    uint32_t id = 0;
    int32_t minLevel = 1;
    int32_t zoneId = 0;
    int32_t prevQuestId = 0;
    int32_t exclusiveGroup = 0;
    bool rewarded = false;
    bool inLog = false;
    bool complete = false;
    bool canTake = false;
    bool escort = false;
    bool vehicle = false;
    bool supported = false;
    uint32_t claims = 0;
    float distance = 0.0f;
};

inline uint32_t ParentZone(uint32_t areaOrZone)
{
    switch (areaOrZone)
    {
        case 131: case 132: case 135: case 137: case 138: case 211: return 1;       // Dun Morogh
        case 9: case 24: case 34: case 87: case 88: return 12;                       // Elwynn
        case 186: case 188: case 256: case 257: return 141;                          // Teldrassil
        case 362: case 363: case 367: case 369: case 372: return 14;                 // Durotar
        case 220: case 221: case 222: case 360: case 637: return 215;                // Mulgore
        case 154: case 159: case 165: return 85;                                     // Tirisfal
        default: return areaOrZone;
    }
}

inline uint32_t const* ZoneChain(uint8_t race, std::size_t& count)
{
    static uint32_t const dwarf[] = { 1u, 38u, 11u };
    static uint32_t const human[] = { 12u, 40u, 44u };
    static uint32_t const nightElf[] = { 141u, 148u, 331u };
    static uint32_t const orc[] = { 14u, 17u, 406u };
    static uint32_t const tauren[] = { 215u, 17u, 406u };
    static uint32_t const undead[] = { 85u, 130u, 267u };
    switch (race)
    {
        case 3: case 7: count = 3; return dwarf;
        case 1: count = 3; return human;
        case 4: count = 3; return nightElf;
        case 2: case 8: count = 3; return orc;
        case 6: count = 3; return tauren;
        case 5: count = 3; return undead;
        default: count = 0; return nullptr;
    }
}

// Stay while this zone still has a level-appropriate quest. Otherwise the next hub.
inline uint32_t NextLevelingZone(uint8_t race, uint32_t zone, bool questsRemain)
{
    uint32_t const here = ParentZone(zone);
    if (questsRemain)
        return here;
    std::size_t count = 0;
    uint32_t const* chain = ZoneChain(race, count);
    if (!chain || count == 0)
        return here;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (chain[i] != here)
            continue;
        if (i + 1 < count)
            return chain[i + 1];
        return here;
    }
    return chain[0];
}

inline bool PrevQuestMet(GuideQuestFact const* rows, std::size_t count, GuideQuestFact const& quest)
{
    if (quest.prevQuestId == 0)
        return true;
    uint32_t const need = quest.prevQuestId > 0 ? uint32_t(quest.prevQuestId) : uint32_t(-quest.prevQuestId);
    for (std::size_t i = 0; i < count; ++i)
    {
        if (rows[i].id != need)
            continue;
        if (quest.prevQuestId > 0)
            return rows[i].rewarded;
        return rows[i].inLog || rows[i].rewarded;
    }
    return false;
}

inline bool ExclusiveGroupBlocked(GuideQuestFact const* rows, std::size_t count, GuideQuestFact const& quest)
{
    if (quest.exclusiveGroup == 0)
        return false;
    for (std::size_t i = 0; i < count; ++i)
    {
        GuideQuestFact const& other = rows[i];
        if (other.id == quest.id || other.exclusiveGroup != quest.exclusiveGroup)
            continue;
        if (other.rewarded || other.inLog)
            return true;
    }
    return false;
}

inline char const* GuideSkipReason(GuideQuestFact const& quest)
{
    if (quest.escort)
        return "escort";
    if (quest.vehicle)
        return "vehicle";
    if (!quest.supported)
        return "unsupported";
    return nullptr;
}

// Turn-in, then the current objective, then the next quest in the chain for this zone.
inline int32_t SelectGuideQuest(GuideQuestFact const* rows, std::size_t count, int32_t botLevel, uint32_t zone)
{
    int32_t turnIn = -1;
    float turnDistance = 1.0e12f;
    int32_t objective = -1;
    float objectiveDistance = 1.0e12f;
    int32_t crowdedObjective = -1;
    float crowdedDistance = 1.0e12f;
    int32_t accept = -1;
    int32_t acceptLevel = 1000;
    float acceptDistance = 1.0e12f;
    uint32_t const here = ParentZone(zone);
    if (!rows)
        return -1;
    for (std::size_t i = 0; i < count; ++i)
    {
        GuideQuestFact const& quest = rows[i];
        if (GuideSkipReason(quest))
            continue;
        if (quest.complete && quest.inLog)
        {
            if (quest.distance < turnDistance)
            {
                turnIn = int32_t(i);
                turnDistance = quest.distance;
            }
            continue;
        }
        if (quest.inLog && !quest.rewarded)
        {
            if (quest.claims >= GuideClaimLimit)
            {
                if (quest.distance < crowdedDistance)
                {
                    crowdedObjective = int32_t(i);
                    crowdedDistance = quest.distance;
                }
                continue;
            }
            if (quest.distance < objectiveDistance)
            {
                objective = int32_t(i);
                objectiveDistance = quest.distance;
            }
            continue;
        }
        if (quest.rewarded || quest.inLog || !quest.canTake)
            continue;
        if (ParentZone(uint32_t(quest.zoneId)) != here)
            continue;
        if (quest.minLevel > botLevel || quest.minLevel <= 0)
            continue;
        if (!PrevQuestMet(rows, count, quest) || ExclusiveGroupBlocked(rows, count, quest))
            continue;
        if (quest.minLevel < acceptLevel || (quest.minLevel == acceptLevel && quest.distance < acceptDistance))
        {
            accept = int32_t(i);
            acceptLevel = quest.minLevel;
            acceptDistance = quest.distance;
        }
    }
    if (turnIn >= 0)
        return turnIn;
    if (objective >= 0)
        return objective;
    if (crowdedObjective >= 0)
        return crowdedObjective;
    return accept;
}

inline GuidePick GuidePickKind(GuideQuestFact const& quest)
{
    if (quest.complete && quest.inLog)
        return GuidePick::TurnIn;
    if (quest.inLog && !quest.rewarded)
        return GuidePick::Objective;
    if (quest.canTake)
        return GuidePick::Accept;
    return GuidePick::None;
}

struct ClusterFact
{
    uint32_t entry = 0;
    uint32_t id = 0;
    float distance = 0.0f;
    uint32_t claims = 0;
    bool blacklisted = false;
};

// Up to three nearest free clusters, then a stable pick so the bots do not stack.
inline int32_t SelectCluster(ClusterFact const* rows, std::size_t count, uint32_t salt)
{
    int32_t best[GuideSpreadChoices] = { -1, -1, -1 };
    float distance[GuideSpreadChoices] = { 1.0e12f, 1.0e12f, 1.0e12f };
    if (!rows)
        return -1;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (rows[i].blacklisted || rows[i].claims >= GuideClaimLimit)
            continue;
        float const candidate = rows[i].distance;
        for (uint32_t slot = 0; slot < GuideSpreadChoices; ++slot)
        {
            if (candidate >= distance[slot])
                continue;
            for (uint32_t shift = GuideSpreadChoices - 1; shift > slot; --shift)
            {
                distance[shift] = distance[shift - 1];
                best[shift] = best[shift - 1];
            }
            distance[slot] = candidate;
            best[slot] = int32_t(i);
            break;
        }
    }
    uint32_t available = 0;
    for (uint32_t slot = 0; slot < GuideSpreadChoices; ++slot)
        if (best[slot] >= 0)
            ++available;
    if (available == 0)
        return -1;
    return best[salt % available];
}

inline bool ShouldBreakStall(uint32_t idleSeconds, bool inCombat)
{
    return idleSeconds >= GuideStallSeconds && !inCombat;
}

inline bool LeashOnlyWhenIdle(bool hasPlanTarget)
{
    return !hasPlanTarget;
}

inline GuideTravel ChooseTravel(bool knowsFlightToZone)
{
    return knowsFlightToZone ? GuideTravel::Flight : GuideTravel::Walk;
}

struct SegmentPoint
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// A long path is walked as successive segments. The last segment is the destination itself.
inline SegmentPoint SegmentDestination(float fromX, float fromY, float fromZ, float toX, float toY, float toZ, float segmentYards)
{
    float const dx = toX - fromX;
    float const dy = toY - fromY;
    float const dz = toZ - fromZ;
    float const distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (segmentYards < 1.0f)
        segmentYards = 1.0f;
    if (distance <= segmentYards || distance < 0.001f)
        return { toX, toY, toZ };
    float const scale = segmentYards / distance;
    return { fromX + dx * scale, fromY + dy * scale, fromZ + dz * scale };
}

inline bool ShouldDropPlan(uint32_t pathFails, uint32_t actedRepeats)
{
    return pathFails >= PlanPathFailLimit || actedRepeats > PlanActedRepeatLimit;
}

inline bool ShouldDisperse(uint32_t botsWithinYards, uint32_t limit = CrowdLimit)
{
    return botsWithinYards > limit;
}

inline uint32_t LoginReadyAt(uint32_t nowMs, uint32_t queueIndex, uint32_t staggerMs = LoginStaggerMs)
{
    return nowMs + queueIndex * staggerMs;
}

// Alliance slots rotate human, dwarf, night elf, gnome. Horde rotates orc, undead, tauren, troll.
inline uint8_t StarterRaceForSlot(uint8_t faction, uint32_t slot)
{
    static uint8_t const alliance[] = { 1u, 3u, 4u, 7u };
    static uint8_t const horde[] = { 2u, 5u, 6u, 8u };
    uint8_t const* table = faction == 1 ? horde : alliance;
    return table[slot % 4];
}

inline bool NameLetters(std::string_view name)
{
    if (name.size() < 2 || name.size() > 12)
        return false;
    for (unsigned char letter : name)
        if (letter < 'A' || (letter > 'Z' && letter < 'a') || letter > 'z')
            return false;
    for (std::size_t i = 2; i < name.size(); ++i)
        if (name[i] == name[i - 1] && name[i] == name[i - 2])
            return false;
    return true;
}

struct BotName
{
    std::string first;
    std::string last;
    std::string full;
};

inline char const* const* FirstNames(uint8_t race, uint8_t sex, std::size_t& count)
{
    static char const* const humanMale[] = { "Marcus", "Cedric", "Aldric", "Rowan", "Edmund", "Harlan", "Corwin", "Darian" };
    static char const* const humanFemale[] = { "Elena", "Maren", "Lysa", "Cora", "Brenna", "Talia", "Serena", "Mira" };
    static char const* const orcMale[] = { "Grimak", "Throgar", "Durgash", "Kargath", "Mozjin", "Rokar", "Gromm", "Nazgrel" };
    static char const* const orcFemale[] = { "Draka", "Aggra", "Borgah", "Kagra", "Morgra", "Shazka", "Uraka", "Veshka" };
    static char const* const dwarfMale[] = { "Bromli", "Kazrim", "Tordek", "Durgan", "Helgar", "Orin", "Bram", "Thorg" };
    static char const* const dwarfFemale[] = { "Bruna", "Disa", "Helga", "Ingrid", "Magra", "Soren", "Tilda", "Vetra" };
    static char const* const elfMale[] = { "Caelen", "Elion", "Faelan", "Lirion", "Nael", "Orion", "Theron", "Vaelan" };
    static char const* const elfFemale[] = { "Aleria", "Elyssa", "Faelia", "Liriel", "Myra", "Naia", "Saria", "Vestia" };
    static char const* const undeadMale[] = { "Alaric", "Mordec", "Davor", "Harlow", "Marius", "Osric", "Renwick", "Voss" };
    static char const* const undeadFemale[] = { "Ada", "Celes", "Dreda", "Helma", "Mora", "Nessa", "Rhea", "Vesper" };
    static char const* const taurenMale[] = { "Ahanu", "Hamuul", "Kotori", "Mahu", "Ornamm", "Tahmo", "Torro", "Winno" };
    static char const* const taurenFemale[] = { "Aponi", "Hara", "Kima", "Mara", "Nara", "Sora", "Tama", "Wina" };
    static char const* const gnomeMale[] = { "Bink", "Coggin", "Fizz", "Glim", "Nim", "Pip", "Tink", "Wizzle" };
    static char const* const gnomeFemale[] = { "Binki", "Cori", "Fizza", "Glima", "Nima", "Pippa", "Tinka", "Wiza" };
    static char const* const trollMale[] = { "Jinzo", "Kazra", "Mojin", "Rokko", "Suljin", "Tazan", "Voljin", "Zulmar" };
    static char const* const trollFemale[] = { "Jinka", "Kazri", "Mojira", "Rokka", "Sulja", "Tazia", "Volja", "Zulma" };
    auto choose = [&](char const* const* male, std::size_t maleCount, char const* const* female, std::size_t femaleCount)
    {
        if (sex == 1)
        {
            count = femaleCount;
            return female;
        }
        count = maleCount;
        return male;
    };
    switch (race)
    {
        case 2: return choose(orcMale, 8, orcFemale, 8);
        case 3: return choose(dwarfMale, 8, dwarfFemale, 8);
        case 4: return choose(elfMale, 8, elfFemale, 8);
        case 5: return choose(undeadMale, 8, undeadFemale, 8);
        case 6: return choose(taurenMale, 8, taurenFemale, 8);
        case 7: return choose(gnomeMale, 8, gnomeFemale, 8);
        case 8: return choose(trollMale, 8, trollFemale, 8);
        default: return choose(humanMale, 8, humanFemale, 8);
    }
}

inline char const* const* LastNames(uint8_t race, std::size_t& count)
{
    static char const* const human[] = { "Ashford", "Hale", "Moore", "Penn", "Reed", "Shaw", "Blake", "Crowe" };
    static char const* const orc[] = { "Redblade", "Ironhide", "Grimjaw", "Ashmane", "Rockfist", "Deadeye", "Steeljaw", "Warpath" };
    static char const* const dwarf[] = { "Stonehelm", "Deepforge", "Flintbrow", "Coppervein", "Ironbrow", "Hillforge", "Orebrow", "Caskward" };
    static char const* const elf[] = { "Moonshade", "Starfire", "Nightwind", "Leafsong", "Silverdew", "Fernshade", "Dawnblade", "Duskwhisper" };
    static char const* const undead[] = { "Blightwood", "Ashmore", "Graves", "Marrow", "Coldwell", "Harlow", "Voss", "Dredge" };
    static char const* const tauren[] = { "Windmane", "Stonehoof", "Rainchaser", "Wildmane", "Highmesa", "Cloudsong", "Dawnstrider", "Swiftarrow" };
    static char const* const gnome[] = { "Sparkwell", "Tinkerton", "Gearshift", "Copperbolt", "Nimblecog", "Springcog", "Fizzwell", "Cogsworth" };
    static char const* const troll[] = { "Junglefang", "Shadowmark", "Hexweaver", "Bonecharm", "Riverfang", "Ashspear", "Voodoomark", "Mosstusk" };
    switch (race)
    {
        case 2: count = 8; return orc;
        case 3: count = 8; return dwarf;
        case 4: count = 8; return elf;
        case 5: count = 8; return undead;
        case 6: count = 8; return tauren;
        case 7: count = 8; return gnome;
        case 8: count = 8; return troll;
        default: count = 8; return human;
    }
}

inline bool FullNameTaken(std::string const& full, std::vector<std::string> const& taken)
{
    for (std::string const& row : taken)
        if (row == full)
            return true;
    return false;
}

// First and last are separate 2-12 letter words. The full name is "First Last".
inline BotName PickBotName(uint8_t race, uint8_t sex, uint32_t salt, std::vector<std::string> const& takenFull)
{
    std::size_t firstCount = 0;
    std::size_t lastCount = 0;
    char const* const* first = FirstNames(race, sex, firstCount);
    char const* const* last = LastNames(race, lastCount);
    BotName chosen;
    if (!first || !last || firstCount == 0 || lastCount == 0)
        return chosen;
    uint32_t const pairs = uint32_t(firstCount * lastCount);
    for (uint32_t step = 0; step < pairs; ++step)
    {
        uint32_t const index = salt + step;
        std::string const given = first[index % firstCount];
        std::string const family = last[(index / firstCount) % lastCount];
        if (!NameLetters(given) || !NameLetters(family) || given == family)
            continue;
        std::string full = given + " " + family;
        if (FullNameTaken(full, takenFull))
            continue;
        chosen.first = given;
        chosen.last = family;
        chosen.full = std::move(full);
        return chosen;
    }
    return chosen;
}
}
#endif
