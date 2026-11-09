/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * Supported starter objectives are creature kills and gameobject uses.
 * Item, talk-to, currency, reputation, and area-trigger objectives are refused.
 * Nothing here grants credit, items, or a completed status.
 */

#ifndef TRINITY_PLAYERBOT_V2_STARTER_QUEST_H
#define TRINITY_PLAYERBOT_V2_STARTER_QUEST_H

#include <cstdint>
#include <string>
#include <string_view>

namespace Playerbot::V2
{
enum class ObjectiveSupport : uint8_t
{
    Kill = 0,
    GameObject,
    Talk,
    Item,
    Unsupported
};

inline ObjectiveSupport ClassifyObjective(uint8_t type)
{
    switch (type)
    {
        case 0: // QUEST_OBJECTIVE_MONSTER
            return ObjectiveSupport::Kill;
        case 1: // QUEST_OBJECTIVE_ITEM, progressed by normal loot, never by a grant
            return ObjectiveSupport::Item;
        case 2: // QUEST_OBJECTIVE_GAMEOBJECT
            return ObjectiveSupport::GameObject;
        case 3: // QUEST_OBJECTIVE_TALKTO
            return ObjectiveSupport::Talk;
        default:
            return ObjectiveSupport::Unsupported;
    }
}

inline bool MayAcceptQuest(bool giverNearby, bool canTake, bool canAdd, bool alreadyRewarded, bool alreadyInLog)
{
    return giverNearby && canTake && canAdd && !alreadyRewarded && !alreadyInLog;
}

// Combat eligibility does not decide this. A friendly quest giver is still an offer
// when the core CanTakeQuest and CanAddQuest checks pass.
struct QuestOfferFacts
{
    bool alive = false;
    bool questGiver = false;
    bool canTake = false;
    bool canAdd = false;
    bool rewarded = false;
    bool inLog = false;
};

inline bool MayOfferQuest(QuestOfferFacts const& facts)
{
    return facts.alive && facts.canTake && facts.canAdd && !facts.rewarded && !facts.inLog;
}

inline char const* QuestOfferReason(QuestOfferFacts const& facts)
{
    if (!facts.alive)
        return "dead";
    if (facts.rewarded)
        return "rewarded";
    if (facts.inLog)
        return "already-have";
    if (!facts.canTake)
        return "cannot-take";
    if (!facts.canAdd)
        return "cannot-add";
    return "can-take";
}

struct QuestGiverHistogram
{
    uint32_t npcs = 0;
    uint32_t offers = 0;
    uint32_t canTake = 0;
    uint32_t cannotTake = 0;
    uint32_t noOffer = 0;
};

inline void NoteQuestOffer(QuestGiverHistogram& histogram, char const* reason)
{
    ++histogram.offers;
    if (reason && std::string_view(reason) == "can-take")
        ++histogram.canTake;
    else
        ++histogram.cannotTake;
}

inline std::string FormatQuestGiverHistogram(QuestGiverHistogram const& histogram)
{
    return "npcs=" + std::to_string(histogram.npcs)
        + " offers=" + std::to_string(histogram.offers)
        + " canTake=" + std::to_string(histogram.canTake)
        + " cannotTake=" + std::to_string(histogram.cannotTake)
        + " noOffer=" + std::to_string(histogram.noOffer);
}

inline bool MayTurnInQuest(bool statusComplete, bool canReward, bool giverInvolved, bool rewardChoiceRequired)
{
    return statusComplete && canReward && giverInvolved && !rewardChoiceRequired;
}

enum class ObjectivePlan : uint8_t
{
    Refuse = 0,
    Attack,
    UseObject,
    Talk,
    Loot,
    Skip
};

// An unsupported or item objective does not freeze the rest of combat.
inline ObjectivePlan PlanObjective(uint8_t type, bool entryPresent, bool hostileAllowed)
{
    switch (ClassifyObjective(type))
    {
        case ObjectiveSupport::Kill:
            if (!entryPresent || !hostileAllowed)
                return ObjectivePlan::Refuse;
            return ObjectivePlan::Attack;
        case ObjectiveSupport::GameObject:
            if (!entryPresent)
                return ObjectivePlan::Refuse;
            return ObjectivePlan::UseObject;
        case ObjectiveSupport::Talk:
            if (!entryPresent)
                return ObjectivePlan::Refuse;
            return ObjectivePlan::Talk;
        case ObjectiveSupport::Item:
            return ObjectivePlan::Loot;
        case ObjectiveSupport::Unsupported:
            return ObjectivePlan::Skip;
    }
    return ObjectivePlan::Skip;
}
}
#endif
