/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include "BotLogin.h"
#include "LifecyclePolicy.h"
#include "Session/BotSession.h"
#include "StarterCombat.h"
#include "StarterQuest.h"

#include "AccountMgr.h"
#include "CellImpl.h"
#include "Config.h"
#include "Corpse.h"
#include "DB2Structure.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
#include "GameTime.h"
#include "LootItemType.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "PathGenerator.h"
#include "PetDefines.h"
#include "Player.h"
#include "RealmList.h"
#include "SpellHistory.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldSession.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <list>
#include <string_view>
#include <vector>

namespace Playerbot::V2
{
namespace
{
struct PendingCreate
{
    std::shared_ptr<BotSession> session;
    std::shared_ptr<bool> finished;
};

struct LiveLogin
{
    std::shared_ptr<BotSession> session;
    BotFaction faction = BotFaction::Unknown;
};

std::vector<PendingCreate> g_creates;
std::vector<LiveLogin> g_logins;
}

void HoldCreateSession(std::shared_ptr<BotSession> session, std::shared_ptr<bool> finished)
{
    g_creates.push_back({ std::move(session), std::move(finished) });
}

uint32_t OnlineCount(BotFaction faction)
{
    return uint32_t(std::count_if(g_logins.begin(), g_logins.end(), [faction](LiveLogin const& live)
    {
        return live.faction == faction;
    }));
}

bool OwnedOnline(ObjectGuid character)
{
    return std::any_of(g_logins.begin(), g_logins.end(), [character](LiveLogin const& live)
    {
        Player* player = live.session->GetPlayer();
        return player && player->GetGUID() == character;
    });
}

void AcceptPendingInvites()
{
    for (LiveLogin const& live : g_logins)
    {
        Player* bot = live.session->GetPlayer();
        if (!bot || !bot->IsInWorld())
            continue;
        Group* group = bot->GetGroupInvite();
        if (!group)
            continue;

        Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID());
        bool const leaderIsBot = leader && leader->GetSession() && leader->GetSession()->IsBot();
        BotFaction const leaderFaction = leader ? FactionForStarterRace(leader->GetRace()) : BotFaction::Unknown;
        if (!AcceptGroupInvite(true, leaderIsBot, live.faction, leaderFaction, false))
        {
            bot->UninviteFromGroup();
            continue;
        }

        group->RemoveInvite(bot);
        if (group->GetLeaderGUID() == bot->GetGUID() || group->IsFull())
            continue;
        if (!group->IsCreated())
        {
            if (!leader)
            {
                group->RemoveAllInvites();
                continue;
            }
            group->RemoveInvite(leader);
            group->Create(leader);
            sGroupMgr->AddGroup(group);
        }
        if (group->AddMember(bot))
            group->BroadcastGroupUpdate();
    }
}

HostileFacts FactsFor(Player const* bot, Unit const* unit, bool pvpEnabled)
{
    HostileFacts facts;
    facts.player = unit->GetTypeId() == TYPEID_PLAYER;
    facts.creature = unit->GetTypeId() == TYPEID_UNIT;
    facts.alive = unit->IsAlive();
    facts.validAttack = bot->IsValidAttackTarget(unit);
    facts.botLevel = bot->GetLevel();
    facts.distance = bot->GetDistance(unit);
    facts.pvpEnabled = pvpEnabled;
    if (facts.player)
    {
        Player const* player = unit->ToPlayer();
        facts.gameMaster = player->IsGameMaster();
        facts.sameFaction = player->GetTeam() == bot->GetTeam();
        facts.level = player->GetLevel();
    }
    else if (Creature const* creature = unit->ToCreature())
    {
        facts.civilian = creature->IsCivilian();
        facts.critter = creature->GetCreatureType() == CREATURE_TYPE_CRITTER;
        facts.pet = creature->IsPet() || creature->IsTotem();
        facts.level = creature->GetLevel();
    }
    return facts;
}

bool EvaluateLocalStep(Player* bot, float towardX, float towardY, G3D::Vector3& out)
{
    float const dx = towardX - bot->GetPositionX();
    float const dy = towardY - bot->GetPositionY();
    float const dist = std::sqrt(dx * dx + dy * dy);
    float const step = dist > 8.0f ? 8.0f : dist;
    float const destX = dist > 0.5f ? bot->GetPositionX() + dx / dist * step : towardX;
    float const destY = dist > 0.5f ? bot->GetPositionY() + dy / dist * step : towardY;
    PathGenerator path(bot);
    bool const calculated = path.CalculatePath(destX, destY, bot->GetPositionZ());
    if (!CorePathIsUsable(calculated, uint32_t(path.GetPathType())) || path.GetPath().size() < 2)
        return false;
    out = path.GetPath()[1];
    return LocalStepPoint(out.x - bot->GetPositionX(), out.y - bot->GetPositionY(), out.z - bot->GetPositionZ());
}

bool WalkLocalStep(Player* bot, float towardX, float towardY)
{
    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != IDLE_MOTION_TYPE)
        return false;
    G3D::Vector3 point;
    if (!EvaluateLocalStep(bot, towardX, towardY, point))
        return false;
    bot->GetMotionMaster()->MovePoint(1, point.x, point.y, point.z);
    return true;
}

uint32_t FindKnownHarmfulSpell(Player* bot, Unit* target)
{
    float const distance = bot->GetDistance(target);
    uint32_t examined = 0;
    for (auto const& [spellId, known] : bot->GetSpellMap())
    {
        if (++examined > 32)
            break;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId, bot->GetMap()->GetDifficultyID());
        if (!info)
            continue;
        KnownSpellFacts facts;
        facts.known = bot->HasSpell(spellId);
        facts.active = known.active;
        facts.removed = known.state == PLAYERSPELL_REMOVED;
        facts.disabled = known.disabled;
        facts.passive = info->IsPassive();
        facts.positive = info->IsPositive();
        facts.cooldown = bot->GetSpellHistory()->HasCooldown(spellId);
        facts.maxRange = info->GetMaxRange(false, bot);
        facts.distance = distance;
        if (HarmfulSpellReady(facts))
            return spellId;
    }
    return 0;
}

bool RecoverOwnedBot(Player* bot)
{
    bool const ghost = bot->HasPlayerFlag(PLAYER_FLAGS_GHOST);
    bool const alive = bot->IsAlive();
    if (alive && !ghost)
        return false;

    Corpse* corpse = bot->GetCorpse();
    bool delayDone = false;
    bool inRange = false;
    bool pathUsable = false;
    G3D::Vector3 ignored;
    if (corpse)
    {
        delayDone = time_t(corpse->GetGhostTime() + bot->GetCorpseReclaimDelay(corpse->GetType() == CORPSE_RESURRECTABLE_PVP)) <= time_t(GameTime::GetGameTime());
        inRange = corpse->IsWithinDistInMap(bot, CORPSE_RECLAIM_RADIUS, true);
        if (corpse->GetMapId() == bot->GetMapId())
            pathUsable = EvaluateLocalStep(bot, corpse->GetPositionX(), corpse->GetPositionY(), ignored);
    }

    switch (ChooseRecovery(alive, ghost, corpse != nullptr, delayDone, inRange, pathUsable))
    {
        case RecoveryKind::ReleaseSpirit:
            if (bot->IsBeingTeleported() || bot->HasAuraType(SPELL_AURA_PREVENT_RESURRECTION))
                return true;
            if (bot->getDeathState() == JUST_DIED)
                bot->KillPlayer();
            bot->RemovePet(nullptr, PET_SAVE_NOT_IN_SLOT, true);
            bot->BuildPlayerRepop();
            bot->RepopAtGraveyard();
            return true;
        case RecoveryKind::Reclaim:
            if (bot->InArena() || bot->RefuseHardcoreResurrect())
                return true;
            bot->ResurrectPlayer(bot->InBattleground() ? 1.0f : 0.5f);
            bot->SpawnCorpseBones();
            return true;
        case RecoveryKind::WalkToCorpse:
            if (corpse)
                WalkLocalStep(bot, corpse->GetPositionX(), corpse->GetPositionY());
            return true;
        case RecoveryKind::Hold:
        case RecoveryKind::None:
            return !alive || ghost;
    }
    return true;
}

Unit* SelectHostile(Player* bot, Player* leader, bool pvpEnabled)
{
    auto usable = [&](Unit* unit)
    {
        return unit && unit != bot && JudgeHostile(FactsFor(bot, unit, pvpEnabled)) != HostileUse::Refuse;
    };

    if (leader)
        if (Unit* victim = leader->GetVictim(); usable(victim))
            return victim;

    Unit* nearest = nullptr;
    float nearestDist = AcquireYards + 1.0f;
    auto consider = [&](Unit* unit)
    {
        if (!usable(unit))
            return;
        float const distance = bot->GetDistance(unit);
        if (distance < nearestDist)
        {
            nearest = unit;
            nearestDist = distance;
        }
    };

    for (Unit* attacker : bot->getAttackers())
        consider(attacker);

    std::list<Creature*> nearby;
    struct NearbyCreature
    {
        Player const* bot = nullptr;
        float range = 0.0f;
        bool operator()(Creature const* creature) const
        {
            return creature && bot->IsWithinDist(creature, range);
        }
    } check { bot, AcquireYards };
    Trinity::CreatureListSearcher searcher(bot, nearby, check);
    Cell::VisitGridObjects(bot, searcher, AcquireYards);
    for (Creature* creature : nearby)
        consider(creature);

    if (pvpEnabled)
    {
        for (Unit* attacker : bot->getAttackers())
            if (attacker && attacker->GetTypeId() == TYPEID_PLAYER)
                consider(attacker);
    }
    return nearest;
}

bool ActOnStarterQuest(Player* bot, bool pvpEnabled, Unit*& combatTarget, bool& blockFreeCombat);

void StepOneOwnedBot(bool pvpEnabled)
{
    if (g_logins.empty())
        return;
    static std::size_t cursor = 0;
    auto const started = std::chrono::steady_clock::now();
    uint32_t steps = 0;
    for (std::size_t n = 0; n < g_logins.size() && steps < AiStepsPerTick; ++n)
    {
        if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count() >= AiBudgetMs)
            break;
        LiveLogin const& live = g_logins[cursor % g_logins.size()];
        cursor = (cursor + 1) % g_logins.size();
        Player* bot = live.session->GetPlayer();
        if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
            continue;
        ++steps;
        if (RecoverOwnedBot(bot))
            continue;

        Player* leader = nullptr;
        if (Group* group = bot->GetGroup())
            if (Player* found = ObjectAccessor::FindPlayer(group->GetLeaderGUID()); found && found != bot)
                leader = found;

        bool const leaderAlive = leader && leader->IsAlive();
        bool const sameMap = leader && leader->GetMapId() == bot->GetMapId();
        float const leaderDistance = leader ? bot->GetDistance(leader) : 0.0f;
        if (ShouldFollowLeader(leader != nullptr, leaderAlive, sameMap, bot->IsInCombat(), leaderDistance))
        {
            WalkLocalStep(bot, leader->GetPositionX(), leader->GetPositionY());
            continue;
        }

        Unit* questTarget = nullptr;
        bool blockFreeCombat = false;
        if (ActOnStarterQuest(bot, pvpEnabled, questTarget, blockFreeCombat))
            continue;
        if (blockFreeCombat && !questTarget)
            continue;

        Unit* target = questTarget ? questTarget : SelectHostile(bot, leader, pvpEnabled);
        if (!target)
            continue;
        HostileUse const use = JudgeHostile(FactsFor(bot, target, pvpEnabled));
        if (use == HostileUse::Melee)
        {
            if (uint32_t spellId = FindKnownHarmfulSpell(bot, target))
            {
                SpellCastResult const cast = bot->CastSpell(CastSpellTargetArg(target), spellId);
                if (cast == SPELL_CAST_OK)
                    continue;
            }
            if (bot->GetVictim() != target)
                bot->Attack(target, true);
        }
        else if (use == HostileUse::Approach)
            WalkLocalStep(bot, target->GetPositionX(), target->GetPositionY());
    }
}

void TickSessions(uint32_t diff, bool gateClosed)
{
    for (auto it = g_creates.begin(); it != g_creates.end();)
    {
        PacketFilter filter(it->session.get());
        it->session->Update(diff, filter);
        if (it->finished && *it->finished)
            it = g_creates.erase(it);
        else
            ++it;
    }

    if (gateClosed)
        for (LiveLogin const& live : g_logins)
            live.session->KickPlayer("population gate closed");

    for (auto it = g_logins.begin(); it != g_logins.end();)
    {
        PacketFilter filter(it->session.get());
        bool const keep = it->session->Update(diff, filter);
        if (!keep || (!it->session->PlayerLoading() && !it->session->GetPlayer()))
            it = g_logins.erase(it);
        else
            ++it;
    }

    if (!gateClosed)
    {
        AcceptPendingInvites();
        StepOneOwnedBot(sConfigMgr->GetBoolDefault("Playerbot.PvP"sv, false));
    }
}

bool LogoutOwned(ObjectGuid character, std::string& detail)
{
    for (LiveLogin const& live : g_logins)
    {
        Player* player = live.session->GetPlayer();
        if (!player || player->GetGUID() != character)
            continue;
        live.session->KickPlayer("owned bot logout");
        detail = "owned bot session will log out";
        return true;
    }
    detail = "that character is not an owned bot session";
    return false;
}

bool ActOnStarterQuest(Player* bot, bool pvpEnabled, Unit*& combatTarget, bool& blockFreeCombat)
{
    combatTarget = nullptr;
    blockFreeCombat = false;

    auto nearestInvolved = [&](uint32_t questId) -> Creature*
    {
        Creature* nearest = nullptr;
        float nearestDist = AcquireYards + 1.0f;
        std::list<Creature*> nearby;
        struct AnyCreature
        {
            Player const* bot = nullptr;
            bool operator()(Creature const* creature) const { return creature && bot->IsWithinDist(creature, AcquireYards); }
        } check { bot };
        Trinity::CreatureListSearcher searcher(bot, nearby, check);
        Cell::VisitGridObjects(bot, searcher, AcquireYards);
        for (Creature* creature : nearby)
        {
            if (!sObjectMgr->GetCreatureQuestInvolvedRelations(creature->GetEntry()).HasQuest(questId))
                continue;
            float const distance = bot->GetDistance(creature);
            if (distance < nearestDist)
            {
                nearest = creature;
                nearestDist = distance;
            }
        }
        return nearest;
    };

    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32_t const questId = bot->GetQuestSlotQuestId(slot);
        if (!questId)
            continue;
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest || bot->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE)
            continue;
        Creature* giver = nearestInvolved(questId);
        if (!giver)
            continue;
        bool const choice = quest->GetRewChoiceItemsCount() > 0;
        bool const canReward = bot->CanRewardQuest(quest, false) && bot->CanRewardQuest(quest, LootItemType::Item, 0, false);
        if (!MayTurnInQuest(true, canReward, true, choice))
        {
            if (choice)
                blockFreeCombat = true;
            continue;
        }
        if (bot->GetDistance(giver) > INTERACTION_DISTANCE)
        {
            WalkLocalStep(bot, giver->GetPositionX(), giver->GetPositionY());
            return true;
        }
        bot->RewardQuest(quest, LootItemType::Item, 0, giver, true);
        return true;
    }

    bool sawUnsupported = false;
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32_t const questId = bot->GetQuestSlotQuestId(slot);
        if (!questId || bot->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
            continue;
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
            continue;
        for (QuestObjective const& objective : quest->GetObjectives())
        {
            if (objective.Amount > 0 && bot->GetQuestObjectiveData(objective) >= objective.Amount)
                continue;
            ObjectiveSupport const support = ClassifyObjective(objective.Type);
            if (support == ObjectiveSupport::Unsupported)
            {
                sawUnsupported = true;
                continue;
            }
            if (support == ObjectiveSupport::Kill && objective.ObjectID > 0)
            {
                std::list<Creature*> creatures;
                bot->GetCreatureListWithEntryInGrid(creatures, uint32_t(objective.ObjectID), AcquireYards);
                Creature* nearest = nullptr;
                float nearestDist = AcquireYards + 1.0f;
                for (Creature* creature : creatures)
                {
                    if (JudgeHostile(FactsFor(bot, creature, pvpEnabled)) == HostileUse::Refuse)
                        continue;
                    float const distance = bot->GetDistance(creature);
                    if (distance < nearestDist)
                    {
                        nearest = creature;
                        nearestDist = distance;
                    }
                }
                if (!nearest)
                {
                    blockFreeCombat = true;
                    return true;
                }
                if (PlanObjective(objective.Type, true, true) != ObjectivePlan::Attack)
                    return true;
                if (bot->GetDistance(nearest) > MeleeReachYards)
                {
                    WalkLocalStep(bot, nearest->GetPositionX(), nearest->GetPositionY());
                    return true;
                }
                combatTarget = nearest;
                return false;
            }
            if (support == ObjectiveSupport::GameObject && objective.ObjectID > 0)
            {
                std::list<GameObject*> objects;
                bot->GetGameObjectListWithEntryInGrid(objects, uint32_t(objective.ObjectID), AcquireYards);
                GameObject* nearest = nullptr;
                float nearestDist = AcquireYards + 1.0f;
                for (GameObject* object : objects)
                {
                    float const distance = bot->GetDistance(object);
                    if (distance < nearestDist)
                    {
                        nearest = object;
                        nearestDist = distance;
                    }
                }
                if (!nearest || PlanObjective(objective.Type, nearest != nullptr, false) != ObjectivePlan::UseObject)
                {
                    blockFreeCombat = true;
                    return true;
                }
                if (bot->GetDistance(nearest) > INTERACTION_DISTANCE)
                {
                    WalkLocalStep(bot, nearest->GetPositionX(), nearest->GetPositionY());
                    return true;
                }
                nearest->Use(bot);
                return true;
            }
        }
    }
    if (sawUnsupported)
    {
        blockFreeCombat = true;
        return true;
    }

    std::list<Creature*> nearby;
    struct AnyCreature
    {
        Player const* bot = nullptr;
        bool operator()(Creature const* creature) const { return creature && bot->IsWithinDist(creature, AcquireYards); }
    } check { bot };
    Trinity::CreatureListSearcher searcher(bot, nearby, check);
    Cell::VisitGridObjects(bot, searcher, AcquireYards);
    Creature* giver = nullptr;
    Quest const* offer = nullptr;
    float giverDist = AcquireYards + 1.0f;
    for (Creature* creature : nearby)
    {
        for (uint32_t questId : sObjectMgr->GetCreatureQuestRelations(creature->GetEntry()))
        {
            Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
            if (!quest)
                continue;
            bool const inLog = bot->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE || bot->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE || bot->GetQuestStatus(questId) == QUEST_STATUS_FAILED;
            if (!MayAcceptQuest(true, bot->CanTakeQuest(quest, false), bot->CanAddQuest(quest, false), bot->IsQuestRewarded(questId), inLog))
                continue;
            float const distance = bot->GetDistance(creature);
            if (distance < giverDist)
            {
                giver = creature;
                offer = quest;
                giverDist = distance;
            }
        }
    }
    if (!giver || !offer)
        return false;
    if (bot->GetDistance(giver) > INTERACTION_DISTANCE)
    {
        WalkLocalStep(bot, giver->GetPositionX(), giver->GetPositionY());
        return true;
    }
    bot->AddQuest(offer, giver);
    return true;
}

bool TryScheduleLogin(PopulationConfig const& config, BotFaction faction, uint32_t alreadyOnline, uint32_t accountId, ObjectGuid character, uint8_t level, uint8_t maxLevel, std::string& detail)
{
    if (!PopulationAllowsNewBot(config, faction, alreadyOnline))
    {
        detail = "population gate is closed";
        return false;
    }
    if (!LoginLevelAllowed(level, maxLevel))
    {
        detail = "level is outside the normal bounds";
        return false;
    }
    if (accountId == 0 || character.IsEmpty())
    {
        detail = "account or character guid is empty";
        return false;
    }
    if (OwnedOnline(character))
    {
        detail = "that owned bot is already logging in or online";
        return false;
    }

    QueryResult marker = CharacterDatabase.PQuery("SELECT account, faction, factory_version, start_level FROM playerbot_v2_character WHERE guid = {}", character.GetCounter());
    MarkerRow row;
    if (marker)
    {
        Field* fields = marker->Fetch();
        row.present = true;
        row.account = fields[0].GetUInt32();
        row.faction = fields[1].GetUInt8();
        row.factoryVersion = fields[2].GetUInt32();
        row.startLevel = fields[3].GetUInt8();
    }
    if (!MarkerBindsOwner(row, accountId, faction))
    {
        detail = "factory marker does not bind this guid, account, and faction";
        return false;
    }

    std::string accountName;
    if (!sAccountMgr->GetName(accountId, accountName))
    {
        detail = "pool account name was not found";
        return false;
    }
    if (!PoolSecurityAllowed(sAccountMgr->GetSecurity(accountId, int32_t(sRealmList->GetCurrentRealmId().Realm))))
    {
        detail = "pool account is GM or admin; login refused";
        return false;
    }

    auto session = std::make_shared<BotSession>(accountId, std::move(accountName));
    if (!session->BeginLogin(character))
    {
        detail = "login holder was not submitted";
        return false;
    }

    g_logins.push_back({ std::move(session), faction });
    TC_LOG_INFO("entities.player.character", "Playerbot V2 login submitted for {} on account {}", character.ToString(), accountId);
    detail = "login submitted; the session is ticked by the module and is not a navigation test";
    return true;
}
}
