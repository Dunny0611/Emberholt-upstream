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
#include "Loot.h"
#include "LootItemType.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Log.h"
#include "Map.h"
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
#include <unordered_map>
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
    uint32_t decisionMs = 0;
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

uint32_t UnitFlagBits(Unit const* unit)
{
    uint32_t bits = 0;
    auto add = [&](UnitFlags flag)
    {
        if (unit->HasUnitFlag(flag))
            bits |= uint32_t(flag);
    };
    add(UNIT_FLAG_SERVER_CONTROLLED);
    add(UNIT_FLAG_NON_ATTACKABLE);
    add(UNIT_FLAG_REMOVE_CLIENT_CONTROL);
    add(UNIT_FLAG_PLAYER_CONTROLLED);
    add(UNIT_FLAG_RENAME);
    add(UNIT_FLAG_PREPARATION);
    add(UNIT_FLAG_UNK_6);
    add(UNIT_FLAG_NOT_ATTACKABLE_1);
    add(UNIT_FLAG_IMMUNE_TO_PC);
    add(UNIT_FLAG_IMMUNE_TO_NPC);
    add(UNIT_FLAG_LOOTING);
    add(UNIT_FLAG_PET_IN_COMBAT);
    add(UNIT_FLAG_PVP_ENABLING);
    add(UNIT_FLAG_FORCE_NAMEPLATE);
    add(UNIT_FLAG_CANT_SWIM);
    add(UNIT_FLAG_CAN_SWIM);
    add(UNIT_FLAG_NON_ATTACKABLE_2);
    add(UNIT_FLAG_PACIFIED);
    add(UNIT_FLAG_STUNNED);
    add(UNIT_FLAG_IN_COMBAT);
    add(UNIT_FLAG_ON_TAXI);
    add(UNIT_FLAG_DISARMED);
    add(UNIT_FLAG_CONFUSED);
    add(UNIT_FLAG_FLEEING);
    add(UNIT_FLAG_POSSESSED);
    add(UNIT_FLAG_UNINTERACTIBLE);
    add(UNIT_FLAG_SKINNABLE);
    add(UNIT_FLAG_MOUNT);
    add(UNIT_FLAG_UNK_28);
    add(UNIT_FLAG_PREVENT_EMOTES_FROM_CHAT_TEXT);
    add(UNIT_FLAG_SHEATHE);
    add(UNIT_FLAG_IMMUNE);
    return bits;
}

FactionTemplateFacts ReadTemplate(FactionTemplateEntry const* entry)
{
    FactionTemplateFacts facts;
    if (!entry)
        return facts;
    facts.id = entry->ID;
    facts.faction = entry->Faction;
    facts.flags = uint32_t(entry->Flags);
    facts.factionGroup = entry->FactionGroup;
    facts.friendGroup = entry->FriendGroup;
    facts.enemyGroup = entry->EnemyGroup;
    for (std::size_t i = 0; i < facts.enemies.size() && i < entry->Enemies.size(); ++i)
    {
        facts.enemies[i] = entry->Enemies[i];
        facts.friends[i] = entry->Friend[i];
    }
    return facts;
}

HostileFacts FactsFor(Player const* bot, Unit const* unit, bool pvpEnabled)
{
    HostileFacts facts;
    facts.player = unit->GetTypeId() == TYPEID_PLAYER;
    facts.creature = unit->GetTypeId() == TYPEID_UNIT;
    facts.alive = unit->IsAlive();
    facts.validAttack = bot->IsValidAttackTarget(unit);
    facts.hostileTo = bot->IsHostileTo(unit);
    facts.friendlyTo = bot->IsFriendlyTo(unit);
    facts.hostileFrom = unit->IsHostileTo(bot);
    facts.friendlyFrom = unit->IsFriendlyTo(bot);
    facts.factionHostile = facts.hostileTo || facts.hostileFrom;
    facts.reaction = int32_t(bot->GetReactionTo(unit));
    facts.botLevel = bot->GetLevel();
    facts.distance = bot->GetDistance(unit);
    facts.pvpEnabled = pvpEnabled;
    facts.entry = unit->GetEntry();
    facts.unitFlags = UnitFlagBits(unit);
    facts.npcFlags = uint32_t(unit->GetNpcFlags());
    facts.immune = (facts.unitFlags & UnitFlagNotAttackable) != 0;
    facts.nonCombatService = (facts.npcFlags & NpcFlagNonCombatService) != 0;
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
        facts.totem = creature->IsTotem();
        facts.pet = creature->IsPet();
        facts.level = creature->GetLevel();
        ApplyFactionRelation(facts, ReadTemplate(creature->GetFactionTemplateEntry()), ReadTemplate(bot->GetFactionTemplateEntry()));
        if (facts.factionTemplate == 0)
            facts.factionTemplate = unit->GetFaction();
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

bool PathFirstStep(Player* bot, float destX, float destY, float destZ, G3D::Vector3& out)
{
    PathGenerator path(bot);
    bool const calculated = path.CalculatePath(destX, destY, destZ);
    if (!CorePathIsUsable(calculated, uint32_t(path.GetPathType())) || path.GetPath().size() < 2)
        return false;
    out = path.GetPath()[1];
    return LocalStepPoint(out.x - bot->GetPositionX(), out.y - bot->GetPositionY(), out.z - bot->GetPositionZ());
}

bool WalkLocalStep(Player* bot, float towardX, float towardY)
{
    MovementGeneratorType const motion = bot->GetMotionMaster()->GetCurrentMovementGeneratorType();
    if (motion == POINT_MOTION_TYPE || motion == CHASE_MOTION_TYPE)
        return false;
    G3D::Vector3 point;
    if (!PathFirstStep(bot, towardX, towardY, bot->GetPositionZ(), point))
        return false;
    bot->GetMotionMaster()->MovePoint(1, point.x, point.y, point.z);
    return true;
}

void EngageHostile(Player* bot, Unit* target)
{
    bot->SetFacingToObject(target);
    if (!bot->IsWithinMeleeRange(target))
    {
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
            bot->GetMotionMaster()->MoveChase(target);
    }
    if (bot->Attack(target, true))
        bot->DoMeleeAttackIfReady();
    else if (bot->GetVictim() == target)
        bot->DoMeleeAttackIfReady();
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

    WorldLocation const& stored = bot->GetCorpseLocation();
    bool const storedHere = bot->HasCorpse() && stored.GetMapId() == bot->GetMapId();
    Corpse* corpse = bot->GetCorpse();
    bool delayDone = false;
    bool inRange = false;
    bool pathUsable = false;
    G3D::Vector3 ignored;
    if (corpse)
    {
        delayDone = time_t(corpse->GetGhostTime() + bot->GetCorpseReclaimDelay(corpse->GetType() == CORPSE_RESURRECTABLE_PVP)) <= time_t(GameTime::GetGameTime());
        inRange = corpse->IsWithinDistInMap(bot, CORPSE_RECLAIM_RADIUS, true);
    }
    if (storedHere)
    {
        pathUsable = PathFirstStep(bot, stored.GetPositionX(), stored.GetPositionY(), stored.GetPositionZ(), ignored);
        if (!pathUsable)
            pathUsable = EvaluateLocalStep(bot, stored.GetPositionX(), stored.GetPositionY(), ignored);
    }

    switch (ChooseRecovery(alive, ghost, storedHere, delayDone, inRange, pathUsable))
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
            if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
                WalkLocalStep(bot, stored.GetPositionX(), stored.GetPositionY());
            return true;
        case RecoveryKind::Hold:
        case RecoveryKind::None:
            return !alive || ghost;
    }
    return true;
}

struct ScanStats
{
    bool gridLoaded = false;
    uint32_t raw = 0;
    uint32_t kept = 0;
    uint32_t refused = 0;
    RefuseHistogram reasons;
};

bool PlayerbotDebug()
{
    return sConfigMgr->GetBoolDefault("Playerbot.Debug"sv, false);
}

bool DebugDue(uint64_t bot, std::string const& signature)
{
    static std::unordered_map<uint64_t, std::unordered_map<std::string, uint32_t>> seen;
    uint32_t& count = seen[bot][signature];
    bool const due = count % 8 == 0;
    ++count;
    return due;
}

void WritePlayerbotLine(std::string const& line)
{
    if (sLog->ShouldLog("playerbot", LOG_LEVEL_INFO))
        TC_LOG_INFO("playerbot", "{}", line);
    else
        TC_LOG_INFO("server.worldserver", "playerbot {}", line);
}

void DebugRefuseCandidate(Player const* bot, HostileFacts const& facts)
{
    if (!PlayerbotDebug() || !bot)
        return;
    std::string const signature = Trinity::StringFormat("refuse|{}|{}|{}", facts.entry, facts.factionTemplate, HostileRefuseReason(facts));
    if (!DebugDue(bot->GetGUID().GetCounter(), signature))
        return;
    WritePlayerbotLine(Trinity::StringFormat(
        "bot {} {} refuse entry={} level={} faction={} unitFlags={} npcFlags={} alive={} reaction={} hostile={} friendly={} hostileFrom={} friendlyFrom={} dist={:.1f} reason={}",
        bot->GetName(), bot->GetGUID().ToString(), facts.entry, facts.level, facts.factionTemplate, facts.unitFlags, facts.npcFlags,
        facts.alive ? 1 : 0, facts.reaction, facts.hostileTo ? 1 : 0, facts.friendlyTo ? 1 : 0, facts.hostileFrom ? 1 : 0,
        facts.friendlyFrom ? 1 : 0, facts.distance, HostileRefuseReason(facts)));
}

void DebugQuestOffer(Player const* bot, HostileFacts const& facts, uint32_t questId, char const* reason, bool canTake, bool canAdd)
{
    if (!PlayerbotDebug() || !bot)
        return;
    std::string const signature = Trinity::StringFormat("quest|{}|{}|{}", facts.entry, questId, reason ? reason : "");
    if (!DebugDue(bot->GetGUID().GetCounter(), signature))
        return;
    WritePlayerbotLine(Trinity::StringFormat(
        "bot {} {} questgiver entry={} level={} faction={} unitFlags={} npcFlags={} alive={} reaction={} hostile={} friendly={} hostileFrom={} friendlyFrom={} dist={:.1f} quest={} canTake={} canAdd={} reason={}",
        bot->GetName(), bot->GetGUID().ToString(), facts.entry, facts.level, facts.factionTemplate, facts.unitFlags, facts.npcFlags,
        facts.alive ? 1 : 0, facts.reaction, facts.hostileTo ? 1 : 0, facts.friendlyTo ? 1 : 0, facts.hostileFrom ? 1 : 0,
        facts.friendlyFrom ? 1 : 0, facts.distance, questId, canTake ? 1 : 0, canAdd ? 1 : 0, reason ? reason : ""));
}

void DebugQuestHistogram(Player const* bot, QuestGiverHistogram const& histogram)
{
    if (!PlayerbotDebug() || !bot)
        return;
    std::string const text = FormatQuestGiverHistogram(histogram);
    if (!DebugDue(bot->GetGUID().GetCounter(), "qhist|" + text))
        return;
    WritePlayerbotLine(Trinity::StringFormat("bot {} {} questgiver-hist {}", bot->GetName(), bot->GetGUID().ToString(), text));
}

void DebugRefuseHistogram(Player const* bot, ScanStats const& stats)
{
    if (!PlayerbotDebug() || !bot)
        return;
    std::string const histogram = FormatRefuseHistogram(stats.reasons);
    std::string const signature = Trinity::StringFormat("hist|{}|{}|{}", histogram, stats.raw, stats.refused);
    if (!DebugDue(bot->GetGUID().GetCounter(), signature))
        return;
    WritePlayerbotLine(Trinity::StringFormat(
        "bot {} {} refuse-hist {} raw={} kept={} refused={} grid={}",
        bot->GetName(), bot->GetGUID().ToString(), histogram, stats.raw, stats.kept, stats.refused, stats.gridLoaded ? 1 : 0));
}

std::list<Creature*> CollectCreatures(Player* bot, float range, ScanStats& stats, FindCreatureAliveState alive = FindCreatureAliveState::Alive)
{
    Map* map = bot->GetMap();
    map->LoadGridsInRange(bot->GetPositionX(), bot->GetPositionY(), range);
    stats.gridLoaded = map->IsGridLoaded(*bot);
    FindCreatureOptions options;
    options.IgnorePhases = true;
    options.IsAlive = alive;
    std::list<Creature*> creatures;
    bot->GetCreatureListWithOptionsInGrid(creatures, range, options);
    stats.raw = uint32_t(creatures.size());
    return creatures;
}

Unit* SelectHostile(Player* bot, Player* leader, bool pvpEnabled, ScanStats& stats)
{
    auto eligible = [&](Unit* unit)
    {
        if (!unit || unit == bot)
            return false;
        return JudgeHostile(FactsFor(bot, unit, pvpEnabled)) != HostileUse::Refuse;
    };

    if (leader)
        if (Unit* victim = leader->GetVictim(); eligible(victim))
            return victim;

    Unit* nearest = nullptr;
    float nearestDist = AcquireYards + 1.0f;
    for (Creature* creature : CollectCreatures(bot, AcquireYards, stats))
    {
        if (!creature || creature->GetGUID() == bot->GetGUID())
            continue;
        HostileFacts const facts = FactsFor(bot, creature, pvpEnabled);
        if (JudgeHostile(facts) == HostileUse::Refuse)
        {
            ++stats.refused;
            AccumulateRefuse(stats.reasons, HostileRefuseReason(facts));
            DebugRefuseCandidate(bot, facts);
            continue;
        }
        ++stats.kept;
        if (facts.distance < nearestDist)
        {
            nearest = creature;
            nearestDist = facts.distance;
        }
    }
    DebugRefuseHistogram(bot, stats);

    for (Unit* attacker : bot->getAttackers())
    {
        if (!eligible(attacker))
            continue;
        float const distance = bot->GetDistance(attacker);
        if (distance < nearestDist)
        {
            nearest = attacker;
            nearestDist = distance;
        }
    }

    return nearest;
}

bool ActOnStarterQuest(Player* bot, bool pvpEnabled, Unit*& combatTarget);
void TryLootQuestItems(Player* bot, Unit* target);

void DebugDecision(Player const* bot, char const* state, char const* reason, Unit const* target, ScanStats const* scan)
{
    if (!sConfigMgr->GetBoolDefault("Playerbot.Debug"sv, false) || !bot)
        return;
    float corpseDistance = -1.0f;
    if (bot->HasCorpse() && bot->GetCorpseLocation().GetMapId() == bot->GetMapId())
        corpseDistance = bot->GetExactDist(bot->GetCorpseLocation());
    uint32_t const entry = target && target->GetTypeId() == TYPEID_UNIT ? target->GetEntry() : 0;
    uint32_t const level = target ? target->GetLevel() : 0;
    std::string const signature = Trinity::StringFormat("{}|{}|{}|{}|{}", state, reason ? reason : "", entry,
        scan ? scan->raw : 0, scan && scan->gridLoaded ? 1 : 0);
    struct LastLine
    {
        std::string signature;
        uint32_t suppressed = 0;
    };
    static std::unordered_map<uint64_t, LastLine> last;
    LastLine& previous = last[bot->GetGUID().GetCounter()];
    if (previous.signature == signature)
    {
        ++previous.suppressed;
        if (previous.suppressed % 8 != 0)
            return;
    }
    else
    {
        previous.signature = signature;
        previous.suppressed = 0;
    }
    std::string const line = Trinity::StringFormat(
        "bot {} {} state={} reason={} target={} entry={} level={} ghost={} corpseDist={:.1f} grid={} raw={} kept={} refused={} repeated={}",
        bot->GetName(), bot->GetGUID().ToString(), state, reason ? reason : "",
        target ? target->GetGUID().ToString() : "none", entry, level,
        bot->HasPlayerFlag(PLAYER_FLAGS_GHOST), corpseDistance,
        scan && scan->gridLoaded ? 1 : 0, scan ? scan->raw : 0, scan ? scan->kept : 0, scan ? scan->refused : 0,
        previous.suppressed);
    if (sLog->ShouldLog("playerbot", LOG_LEVEL_INFO))
        TC_LOG_INFO("playerbot", "{}", line);
    else
        TC_LOG_INFO("server.worldserver", "playerbot {}", line);
}

void StepOneOwnedBot(bool pvpEnabled, uint32_t diff)
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
        LiveLogin& live = g_logins[cursor % g_logins.size()];
        cursor = (cursor + 1) % g_logins.size();
        live.decisionMs += diff;
        if (live.decisionMs < DecisionIntervalMs)
            continue;
        live.decisionMs = 0;
        Player* bot = live.session->GetPlayer();
        if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
        {
            if (bot)
                DebugDecision(bot, "skip", bot->IsBeingTeleported() ? "teleporting" : "not-in-world", nullptr, nullptr);
            continue;
        }
        ++steps;
        if (RecoverOwnedBot(bot))
        {
            DebugDecision(bot, bot->HasPlayerFlag(PLAYER_FLAGS_GHOST) ? "ghost" : "dead",
                bot->HasCorpse() ? "recovery" : "no-corpse-location", nullptr, nullptr);
            continue;
        }

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
            DebugDecision(bot, "follow", "leader", leader, nullptr);
            continue;
        }

        Unit* questTarget = nullptr;
        if (ActOnStarterQuest(bot, pvpEnabled, questTarget))
        {
            DebugDecision(bot, "quest", "acted", questTarget, nullptr);
            continue;
        }

        ScanStats scan;
        Unit* target = questTarget ? questTarget : SelectHostile(bot, leader, pvpEnabled, scan);
        if (!target)
        {
            DebugDecision(bot, "idle", scan.gridLoaded ? "no-legal-target" : "grid-not-loaded", nullptr, &scan);
            continue;
        }
        HostileFacts const facts = FactsFor(bot, target, pvpEnabled);
        HostileUse const use = JudgeHostile(facts);
        if (use == HostileUse::Melee || use == HostileUse::Approach)
        {
            if (use == HostileUse::Melee)
            {
                if (uint32_t spellId = FindKnownHarmfulSpell(bot, target))
                {
                    SpellCastResult const cast = bot->CastSpell(CastSpellTargetArg(target), spellId);
                    if (cast == SPELL_CAST_OK)
                    {
                        DebugDecision(bot, "spell", "cast", target, &scan);
                        continue;
                    }
                }
            }
            EngageHostile(bot, target);
            TryLootQuestItems(bot, target);
            DebugDecision(bot, use == HostileUse::Melee ? "melee" : "chase", HostileRefuseReason(facts), target, &scan);
        }
        else
            DebugDecision(bot, "refused", HostileRefuseReason(facts), target, &scan);
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
        StepOneOwnedBot(sConfigMgr->GetBoolDefault("Playerbot.PvP"sv, false), diff);
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

void TryLootQuestItems(Player* bot, Unit* target)
{
    Creature* creature = target->ToCreature();
    if (!creature || creature->IsAlive() || !creature->m_loot || bot->GetDistance(creature) > INTERACTION_DISTANCE)
        return;
    Loot* loot = creature->m_loot.get();
    for (uint8 slot = 0; slot < loot->items.size() && slot < 16; ++slot)
    {
        LootItem const& item = loot->items[slot];
        if (item.is_looted || !item.needs_quest)
            continue;
        bot->StoreLootItem(creature->GetGUID(), slot, loot);
    }
}

bool ActOnStarterQuest(Player* bot, bool pvpEnabled, Unit*& combatTarget)
{
    combatTarget = nullptr;

    auto nearestInvolved = [&](uint32_t questId) -> Creature*
    {
        Creature* nearest = nullptr;
        float nearestDist = AcquireYards + 1.0f;
        ScanStats ignored;
        for (Creature* creature : CollectCreatures(bot, AcquireYards, ignored))
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
            continue;
        if (bot->GetDistance(giver) > INTERACTION_DISTANCE)
        {
            WalkLocalStep(bot, giver->GetPositionX(), giver->GetPositionY());
            return true;
        }
        bot->RewardQuest(quest, LootItemType::Item, 0, giver, true);
        return true;
    }

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
            if (support == ObjectiveSupport::Unsupported || PlanObjective(objective.Type, objective.ObjectID > 0, true) == ObjectivePlan::Skip)
                continue;
            if (support == ObjectiveSupport::Item)
            {
                ScanStats ignoredDead;
                std::list<Creature*> corpses = CollectCreatures(bot, INTERACTION_DISTANCE, ignoredDead, FindCreatureAliveState::Dead);
                bool looted = false;
                for (Creature* creature : corpses)
                {
                    uint8_t const before = creature->m_loot ? uint8_t(std::min<size_t>(creature->m_loot->items.size(), 16)) : 0;
                    TryLootQuestItems(bot, creature);
                    if (before)
                        looted = true;
                }
                if (looted)
                    return true;
                continue;
            }
            if (support == ObjectiveSupport::Talk && objective.ObjectID > 0)
            {
                ScanStats ignoredTalk;
                std::list<Creature*> speakers = CollectCreatures(bot, AcquireYards, ignoredTalk);
                speakers.remove_if([entry = uint32_t(objective.ObjectID)](Creature* creature) { return creature->GetEntry() != entry; });
                Creature* speaker = nullptr;
                float speakerDist = AcquireYards + 1.0f;
                for (Creature* creature : speakers)
                {
                    float const distance = bot->GetDistance(creature);
                    if (distance < speakerDist)
                    {
                        speaker = creature;
                        speakerDist = distance;
                    }
                }
                if (!speaker)
                    continue;
                if (speakerDist > INTERACTION_DISTANCE)
                {
                    WalkLocalStep(bot, speaker->GetPositionX(), speaker->GetPositionY());
                    return true;
                }
                bot->TalkedToCreature(speaker->GetEntry(), speaker->GetGUID());
                return true;
            }
            if (support == ObjectiveSupport::Kill && objective.ObjectID > 0)
            {
                ScanStats ignoredKill;
                std::list<Creature*> creatures = CollectCreatures(bot, AcquireYards, ignoredKill);
                creatures.remove_if([entry = uint32_t(objective.ObjectID)](Creature* creature) { return creature->GetEntry() != entry; });
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
                    continue;
                if (PlanObjective(objective.Type, true, true) != ObjectivePlan::Attack)
                    continue;
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
                FindGameObjectOptions objectOptions;
                objectOptions.IgnorePhases = true;
                objectOptions.GameObjectId = uint32_t(objective.ObjectID);
                std::list<GameObject*> objects;
                bot->GetGameObjectListWithOptionsInGrid(objects, AcquireYards, objectOptions);
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
                if (!nearest || PlanObjective(objective.Type, true, false) != ObjectivePlan::UseObject)
                    continue;
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

    ScanStats ignoredGivers;
    std::list<Creature*> nearby = CollectCreatures(bot, AcquireYards, ignoredGivers);
    Creature* giver = nullptr;
    Quest const* offer = nullptr;
    float giverDist = AcquireYards + 1.0f;
    QuestGiverHistogram questHist;
    for (Creature* creature : nearby)
    {
        if (!creature)
            continue;
        bool const questFlag = creature->HasNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);
        uint32_t offers = 0;
        for (uint32_t questId : sObjectMgr->GetCreatureQuestRelations(creature->GetEntry()))
        {
            Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
            if (!quest)
                continue;
            ++offers;
            bool const inLog = bot->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE || bot->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE || bot->GetQuestStatus(questId) == QUEST_STATUS_FAILED;
            QuestOfferFacts offerFacts;
            offerFacts.alive = creature->IsAlive();
            offerFacts.questGiver = questFlag;
            offerFacts.canTake = bot->CanTakeQuest(quest, false);
            offerFacts.canAdd = bot->CanAddQuest(quest, false);
            offerFacts.rewarded = bot->IsQuestRewarded(questId);
            offerFacts.inLog = inLog;
            char const* const reason = QuestOfferReason(offerFacts);
            NoteQuestOffer(questHist, reason);
            DebugQuestOffer(bot, FactsFor(bot, creature, pvpEnabled), questId, reason, offerFacts.canTake, offerFacts.canAdd);
            if (!MayOfferQuest(offerFacts))
                continue;
            float const distance = bot->GetDistance(creature);
            if (distance < giverDist)
            {
                giver = creature;
                offer = quest;
                giverDist = distance;
            }
        }
        if (questFlag)
        {
            ++questHist.npcs;
            if (offers == 0)
            {
                ++questHist.noOffer;
                DebugQuestOffer(bot, FactsFor(bot, creature, pvpEnabled), 0, "no-offer", false, false);
            }
        }
    }
    DebugQuestHistogram(bot, questHist);
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
