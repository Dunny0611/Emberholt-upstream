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
#include "DB2Stores.h"
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
#include "PhasingHandler.h"
#include "PetDefines.h"
#include "Player.h"
#include "RealmList.h"
#include "SpellHistory.h"
#include "SpellInfo.h"
#include "ReputationMgr.h"
#include "SpellMgr.h"
#include "WorldSession.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <list>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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
    // A full path to a far wolf can fail. An 8-yard hop toward that point still moves.
    if (!PathFirstStep(bot, towardX, towardY, bot->GetPositionZ(), point) && !EvaluateLocalStep(bot, towardX, towardY, point))
        return false;
    bot->GetMotionMaster()->MovePoint(1, point.x, point.y, point.z);
    return true;
}

bool MoveToward(Player* bot, float towardX, float towardY)
{
    if (bot->m_homebind.GetMapId() == bot->GetMapId())
    {
        float const anchorX = bot->m_homebind.GetPositionX();
        float const anchorY = bot->m_homebind.GetPositionY();
        float const dx = towardX - anchorX;
        float const dy = towardY - anchorY;
        float const destDistance = std::sqrt(dx * dx + dy * dy);
        float const here = bot->GetExactDist2d(anchorX, anchorY);
        if (destDistance > WanderLeashYards)
        {
            if (here <= WanderLeashYards)
                return false;
            towardX = anchorX;
            towardY = anchorY;
        }
    }
    MovementGeneratorType const motion = bot->GetMotionMaster()->GetCurrentMovementGeneratorType();
    if (motion == POINT_MOTION_TYPE || motion == CHASE_MOTION_TYPE)
        return true;
    return WalkLocalStep(bot, towardX, towardY);
}

void TryMarkAtWar(Player* bot, Unit* target)
{
    Creature const* creature = target->ToCreature();
    if (!creature)
        return;
    FactionTemplateEntry const* factionTemplate = creature->GetFactionTemplateEntry();
    if (!factionTemplate)
        return;
    FactionEntry const* faction = sFactionStore.LookupEntry(factionTemplate->Faction);
    if (!faction || !faction->CanHaveReputation() || bot->GetReputationMgr().IsAtWar(faction))
        return;
    bot->GetReputationMgr().SetAtWarForCombat(faction);
}

char const* AttackBlockReason(Player const* bot, Unit const* target)
{
    if (target->HasUnitState(UNIT_STATE_UNATTACKABLE))
        return "unattackable-state";
    if (!bot->CanSeeOrDetect(target))
        return "cannot-see";
    if (!target->IsAlive())
        return "dead";
    if (target->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE_2))
        return "non-attackable-2";
    if (target->IsUninteractible())
        return "uninteractible";
    if (bot->HasPlayerFlag(PLAYER_FLAGS_UBER))
        return "uber";
    if (target->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_ON_TAXI | UNIT_FLAG_NOT_ATTACKABLE_1))
        return "non-attackable";
    if (bot->IsImmuneToNPC())
        return "immune-to-npc";
    if (target->IsImmuneToPC())
        return "immune-to-pc";
    if (bot->IsFriendlyTo(target))
        return "friendly";
    if (target->IsFriendlyTo(bot))
        return "friendly-from";
    if (FactionTemplateEntry const* factionTemplate = target->GetFactionTemplateEntry())
        if (FactionEntry const* faction = sFactionStore.LookupEntry(factionTemplate->Faction))
            if (FactionState const* state = bot->GetReputationMgr().GetState(faction))
                if (!state->Flags.HasFlag(ReputationFlags::AtWar))
                    return "not-at-war";
    if (!bot->IsValidAttackTarget(target))
        return "other";
    return "ok";
}

SwingFacts SwingFor(Player const* bot, Unit const* target)
{
    SwingFacts facts;
    facts.inMeleeRange = bot->IsWithinMeleeRange(target);
    facts.inArc = bot->HasInArc(2.0f * float(M_PI) / 3.0f, target) || bot->IsWithinBoundaryRadius(target);
    facts.attackReady = bot->isAttackReady(BASE_ATTACK);
    facts.attackTimer = bot->getAttackTimer(BASE_ATTACK);
    return facts;
}

struct MeleeTrace
{
    SwingFacts swing;
    bool validAttack = false;
    bool los = false;
    char const* attackBlock = "ok";
    bool attacked = false;
    bool swingCalled = false;
    float minDamage = 0.0f;
    float maxDamage = 0.0f;
};

struct HpWatch
{
    uint32_t health = 0;
    uint32_t since = 0;
    bool started = false;
};

std::unordered_map<uint64_t, std::unordered_map<uint64_t, HpWatch>>& MeleeWatches()
{
    static std::unordered_map<uint64_t, std::unordered_map<uint64_t, HpWatch>> watches;
    return watches;
}

std::unordered_map<uint64_t, std::unordered_map<uint64_t, uint32_t>>& MeleeBlacklist()
{
    static std::unordered_map<uint64_t, std::unordered_map<uint64_t, uint32_t>> blacklist;
    return blacklist;
}

bool IsMeleeBlacklisted(Player const* bot, ObjectGuid guid)
{
    auto botIt = MeleeBlacklist().find(bot->GetGUID().GetCounter());
    if (botIt == MeleeBlacklist().end())
        return false;
    auto itr = botIt->second.find(guid.GetCounter());
    if (itr == botIt->second.end())
        return false;
    if (GameTime::GetGameTimeMS() >= itr->second)
    {
        botIt->second.erase(itr);
        return false;
    }
    return true;
}

void BlacklistMeleeTarget(Player* bot, ObjectGuid guid)
{
    MeleeBlacklist()[bot->GetGUID().GetCounter()][guid.GetCounter()] = GameTime::GetGameTimeMS() + 120000;
    MeleeWatches()[bot->GetGUID().GetCounter()].erase(guid.GetCounter());
    if (bot->GetVictim() && bot->GetVictim()->GetGUID() == guid)
        bot->AttackStop();
}

bool MeleeGaveUp(Player* bot, Unit* target)
{
    HpWatch& watch = MeleeWatches()[bot->GetGUID().GetCounter()][target->GetGUID().GetCounter()];
    uint32_t const now = GameTime::GetGameTimeMS();
    uint32_t const health = target->GetHealth();
    if (!watch.started)
    {
        watch.started = true;
        watch.health = health;
        watch.since = now;
        return false;
    }
    if (health < watch.health)
    {
        watch.health = health;
        watch.since = now;
        return false;
    }
    if (!MeleeTargetUnreachable(now - watch.since, watch.health, health))
        return false;
    BlacklistMeleeTarget(bot, target->GetGUID());
    return true;
}

void EngageHostile(Player* bot, Unit* target, MeleeTrace& trace)
{
    bot->SetSheath(SHEATH_STATE_MELEE);
    bot->CalculateMinMaxDamage(BASE_ATTACK, false, true, trace.minDamage, trace.maxDamage);
    if (trace.minDamage <= 0.0f && trace.maxDamage <= 0.0f)
    {
        bot->UpdateDamagePhysical(BASE_ATTACK);
        bot->CalculateMinMaxDamage(BASE_ATTACK, false, true, trace.minDamage, trace.maxDamage);
    }
    bot->SetInFront(target);
    trace.swing = SwingFor(bot, target);
    trace.los = bot->IsWithinLOSInMap(target);
    trace.attackBlock = AttackBlockReason(bot, target);
    trace.validAttack = bot->IsValidAttackTarget(target);
    if (!trace.validAttack)
    {
        TryMarkAtWar(bot, target);
        trace.attackBlock = AttackBlockReason(bot, target);
        trace.validAttack = bot->IsValidAttackTarget(target);
    }
    SwingAction action = ChooseSwing(trace.swing);
    if (action == SwingAction::Close || !trace.los || !trace.validAttack)
        return;
    if (bot->GetVictim() != target || !bot->HasUnitState(UNIT_STATE_MELEE_ATTACKING))
    {
        trace.attacked = bot->Attack(target, true);
        trace.swing = SwingFor(bot, target);
        action = ChooseSwing(trace.swing);
    }
    else
        trace.attacked = true;
    if (!SwingCallsAttack(action))
        return;
    trace.swingCalled = true;
    bot->DoMeleeAttackIfReady();
    trace.swing = SwingFor(bot, target);
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
    // A seek step left POINT or CHASE running. That generator blocks the corpse walk.
    static std::unordered_set<uint64_t> motionCleared;
    if (alive && !ghost)
    {
        motionCleared.erase(bot->GetGUID().GetCounter());
        return false;
    }
    if (motionCleared.insert(bot->GetGUID().GetCounter()).second)
        bot->GetMotionMaster()->Clear();

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
            if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
                bot->GetMotionMaster()->Clear();
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

void DebugRefuseCandidate(Player const* bot, Unit const* unit, HostileFacts const& facts)
{
    if (!PlayerbotDebug() || !bot)
        return;
    char const* const reason = HostileRefuseReason(facts);
    char const* note = "refused";
    if (reason && std::string_view(reason) == "not-hostile")
        note = (facts.templateFriendly || facts.friendlyTo) ? "friendly-template" : "not-prey";
    std::string const signature = Trinity::StringFormat("refuse|{}|{}|{}", facts.entry, facts.factionTemplate, reason ? reason : "");
    if (!DebugDue(bot->GetGUID().GetCounter(), signature))
        return;
    WritePlayerbotLine(Trinity::StringFormat(
        "bot {} {} refuse name=\"{}\" entry={} level={} faction={} unitFlags={} npcFlags={} alive={} reaction={} hostile={} friendly={} hostileFrom={} friendlyFrom={} templateFriendly={} dist={:.1f} reason={} note={}",
        bot->GetName(), bot->GetGUID().ToString(), unit ? unit->GetName() : std::string(), facts.entry, facts.level, facts.factionTemplate,
        facts.unitFlags, facts.npcFlags, facts.alive ? 1 : 0, facts.reaction, facts.hostileTo ? 1 : 0, facts.friendlyTo ? 1 : 0,
        facts.hostileFrom ? 1 : 0, facts.friendlyFrom ? 1 : 0, facts.templateFriendly ? 1 : 0, facts.distance, reason ? reason : "", note));
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
    float nearestDist = PreySeekYards + 1.0f;
    for (Creature* creature : CollectCreatures(bot, PreySeekYards, stats))
    {
        if (!creature || creature->GetGUID() == bot->GetGUID() || IsMeleeBlacklisted(bot, creature->GetGUID()))
            continue;
        HostileFacts const facts = FactsFor(bot, creature, pvpEnabled);
        if (JudgeHostile(facts, PreySeekYards) == HostileUse::Refuse)
        {
            ++stats.refused;
            AccumulateRefuse(stats.reasons, HostileRefuseReason(facts));
            DebugRefuseCandidate(bot, creature, facts);
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
bool DiscoverQuestOffer(Player* bot, bool pvpEnabled);
bool WanderInsideLeash(Player* bot);
void TryLootQuestItems(Player* bot, Unit* target);

uint32_t HeldQuestCount(Player const* bot)
{
    uint32_t held = 0;
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        if (bot->GetQuestSlotQuestId(slot))
            ++held;
    return held;
}

std::string QuestLogText(Player const* bot)
{
    std::string text;
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32_t const questId = bot->GetQuestSlotQuestId(slot);
        if (!questId)
            continue;
        if (!text.empty())
            text += ',';
        text += Trinity::StringFormat("{}:{}", questId, uint32(bot->GetQuestStatus(questId)));
    }
    return text.empty() ? "empty" : text;
}

void DebugQuestBranch(Player const* bot, char const* action)
{
    if (!PlayerbotDebug() || !bot)
        return;
    WritePlayerbotLine(Trinity::StringFormat(
        "bot {} {} quest-branch log={} held={} action={}",
        bot->GetName(), bot->GetGUID().ToString(), QuestLogText(bot), HeldQuestCount(bot), action ? action : ""));
}

void DebugDecision(Player const* bot, char const* state, char const* reason, Unit const* target, ScanStats const* scan, uint32_t seekEntry = 0, MeleeTrace const* melee = nullptr)
{
    if (!sConfigMgr->GetBoolDefault("Playerbot.Debug"sv, false) || !bot)
        return;
    float corpseDistance = -1.0f;
    if (bot->HasCorpse() && bot->GetCorpseLocation().GetMapId() == bot->GetMapId())
        corpseDistance = bot->GetExactDist(bot->GetCorpseLocation());
    uint32_t const entry = target && target->GetTypeId() == TYPEID_UNIT ? target->GetEntry() : seekEntry;
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
    std::string line = Trinity::StringFormat(
        "bot {} {} state={} reason={} target={} entry={} level={} ghost={} corpseDist={:.1f} grid={} raw={} kept={} refused={} repeated={}",
        bot->GetName(), bot->GetGUID().ToString(), state, reason ? reason : "",
        target ? target->GetGUID().ToString() : "none", entry, level,
        bot->HasPlayerFlag(PLAYER_FLAGS_GHOST), corpseDistance,
        scan && scan->gridLoaded ? 1 : 0, scan ? scan->raw : 0, scan ? scan->kept : 0, scan ? scan->refused : 0,
        previous.suppressed);
    if (bot->m_homebind.GetMapId() == bot->GetMapId())
        line += Trinity::StringFormat(" anchorDist={:.1f} leash={:.0f}", bot->GetExactDist2d(bot->m_homebind.GetPositionX(), bot->m_homebind.GetPositionY()), WanderLeashYards);
    if (melee && target)
    {
        Unit const* victim = bot->GetVictim();
        line += Trinity::StringFormat(
            " attackTimer={} ready={} inMeleeRange={} inArc={} validAttack={} attackBlock={} los={} victim={} meleeState={} moving={} targetHp={}/{} botHp={}/{} botCombat={} targetCombat={} minDmg={:.2f} maxDmg={:.2f} attacked={} swingCalled={} orient={:.3f}",
            melee->swing.attackTimer, melee->swing.attackReady ? 1 : 0, melee->swing.inMeleeRange ? 1 : 0, melee->swing.inArc ? 1 : 0,
            melee->validAttack ? 1 : 0, melee->attackBlock ? melee->attackBlock : "", melee->los ? 1 : 0, victim ? victim->GetGUID().ToString() : "none",
            bot->HasUnitState(UNIT_STATE_MELEE_ATTACKING) ? 1 : 0, bot->HasUnitState(UNIT_STATE_MOVING) ? 1 : 0,
            target->GetHealth(), target->GetMaxHealth(), bot->GetHealth(), bot->GetMaxHealth(),
            bot->IsInCombat() ? 1 : 0, target->IsInCombat() ? 1 : 0, melee->minDamage, melee->maxDamage,
            melee->attacked ? 1 : 0, melee->swingCalled ? 1 : 0, bot->GetOrientation());
    }
    if (sLog->ShouldLog("playerbot", LOG_LEVEL_INFO))
        TC_LOG_INFO("playerbot", "{}", line);
    else
        TC_LOG_INFO("server.worldserver", "playerbot {}", line);
}

void EnsureWorldPhase(Player* bot)
{
    uint32 zone = 0;
    uint32 area = 0;
    bot->GetMap()->GetZoneAndAreaId(bot->GetPhaseShift(), zone, area, *bot);
    static std::unordered_map<uint64_t, uint32> appliedArea;
    uint32& previous = appliedArea[bot->GetGUID().GetCounter()];
    if (previous == area && area != 0)
        return;
    previous = area;
    // Same server-side calls as SendInitialPacketsAfterAddToMap. Packets are dropped for a bot,
    // but the phase shift, zone, and visibility updates still have to run after the grid exists.
    PhasingHandler::OnMapChange(bot);
    if (zone || area)
        bot->UpdateZone(zone, area);
    bot->UpdateObjectVisibility(false);
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
        EnsureWorldPhase(bot);
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

        DebugQuestBranch(bot, SeekQuestBeforePrey(HeldQuestCount(bot)) ? "empty-log" : "have-log");
        if (SeekQuestBeforePrey(HeldQuestCount(bot)) && DiscoverQuestOffer(bot, pvpEnabled))
        {
            DebugDecision(bot, "seek", "quest-offer", nullptr, nullptr);
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
        if (target && IsMeleeBlacklisted(bot, target->GetGUID()))
            target = nullptr;
        if (target && bot->IsWithinMeleeRange(target) && MeleeGaveUp(bot, target))
        {
            DebugDecision(bot, "give-up", "unreachable", target, &scan);
            target = nullptr;
        }
        if (target && (!bot->IsWithinMeleeRange(target) || !bot->IsWithinLOSInMap(target)))
        {
            if (MoveToward(bot, target->GetPositionX(), target->GetPositionY()))
            {
                DebugDecision(bot, "seek", bot->IsWithinLOSInMap(target) ? "prey" : "no-los", target, &scan);
                continue;
            }
            target = questTarget ? target : nullptr;
        }
        if (target)
        {
            HostileFacts const facts = FactsFor(bot, target, pvpEnabled);
            if (uint32_t spellId = FindKnownHarmfulSpell(bot, target))
            {
                SpellCastResult const cast = bot->CastSpell(CastSpellTargetArg(target), spellId);
                if (cast == SPELL_CAST_OK)
                {
                    DebugDecision(bot, "spell", "cast", target, &scan);
                    continue;
                }
            }
            MeleeTrace trace;
            EngageHostile(bot, target, trace);
            TryLootQuestItems(bot, target);
            DebugDecision(bot, "melee", HostileRefuseReason(facts), target, &scan, 0, &trace);
            continue;
        }
        if (DiscoverQuestOffer(bot, pvpEnabled))
        {
            DebugDecision(bot, "seek", "quest-offer", nullptr, &scan);
            continue;
        }
        if (WanderInsideLeash(bot))
        {
            DebugDecision(bot, "wander", "leash", nullptr, &scan);
            continue;
        }
        DebugDecision(bot, "idle", scan.gridLoaded ? "no-legal-target" : "grid-not-loaded", nullptr, &scan);
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

struct SpawnSpot
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

std::unordered_map<uint64_t, std::vector<SpawnSpot>> const& SpawnIndex(bool creatures)
{
    static std::unordered_map<uint64_t, std::vector<SpawnSpot>> creatureIndex;
    static std::unordered_map<uint64_t, std::vector<SpawnSpot>> objectIndex;
    static bool built = false;
    if (!built)
    {
        built = true;
        for (auto const& node : sObjectMgr->GetAllCreatureData())
        {
            CreatureData const& data = node.second;
            if (data.mapId == MAPID_INVALID || data.id == 0)
                continue;
            creatureIndex[(uint64_t(data.mapId) << 32) | data.id].push_back(
                { data.spawnPoint.GetPositionX(), data.spawnPoint.GetPositionY(), data.spawnPoint.GetPositionZ() });
        }
        for (auto const& node : sObjectMgr->GetAllGameObjectData())
        {
            GameObjectData const& data = node.second;
            if (data.mapId == MAPID_INVALID || data.id == 0)
                continue;
            objectIndex[(uint64_t(data.mapId) << 32) | data.id].push_back(
                { data.spawnPoint.GetPositionX(), data.spawnPoint.GetPositionY(), data.spawnPoint.GetPositionZ() });
        }
    }
    return creatures ? creatureIndex : objectIndex;
}

bool NearestSpawn(bool creatures, uint32 mapId, uint32 entry, float x, float y, float& outX, float& outY, float& outZ, float& outDistance)
{
    auto const& index = SpawnIndex(creatures);
    auto found = index.find((uint64_t(mapId) << 32) | entry);
    if (found == index.end())
        return false;
    bool any = false;
    for (SpawnSpot const& spot : found->second)
    {
        float const dx = spot.x - x;
        float const dy = spot.y - y;
        float const distance = std::sqrt(dx * dx + dy * dy);
        if (!any || distance < outDistance)
        {
            any = true;
            outDistance = distance;
            outX = spot.x;
            outY = spot.y;
            outZ = spot.z;
        }
    }
    return any;
}

bool HasCreatureSpawn(uint32 mapId, uint32 entry)
{
    auto const& index = SpawnIndex(true);
    return index.find((uint64_t(mapId) << 32) | entry) != index.end();
}

std::list<Creature*> LiveEntries(Player* bot, float range, uint32 entry)
{
    bot->GetMap()->LoadGridsInRange(bot->GetPositionX(), bot->GetPositionY(), range);
    FindCreatureOptions options;
    options.IsAlive = FindCreatureAliveState::Alive;
    options.CreatureId = entry;
    std::list<Creature*> creatures;
    bot->GetCreatureListWithOptionsInGrid(creatures, range, options);
    return creatures;
}

std::list<GameObject*> LiveObjects(Player* bot, float range, uint32 entry)
{
    bot->GetMap()->LoadGridsInRange(bot->GetPositionX(), bot->GetPositionY(), range);
    FindGameObjectOptions options;
    options.GameObjectId = entry;
    std::list<GameObject*> objects;
    bot->GetGameObjectListWithOptionsInGrid(objects, range, options);
    return objects;
}

struct Destination
{
    Creature* creature = nullptr;
    GameObject* object = nullptr;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float distance = 0.0f;
    uint32 entry = 0;
    bool known = false;
    bool kill = false;
    bool talk = false;
    bool objectUse = false;
};

void NoteCreature(Player* bot, Destination& best, Creature* creature, bool kill, bool talk)
{
    if (!creature)
        return;
    float const distance = bot->GetDistance(creature);
    if (best.known && distance >= best.distance)
        return;
    best.creature = creature;
    best.object = nullptr;
    best.x = creature->GetPositionX();
    best.y = creature->GetPositionY();
    best.z = creature->GetPositionZ();
    best.distance = distance;
    best.entry = creature->GetEntry();
    best.known = true;
    best.kill = kill;
    best.talk = talk;
    best.objectUse = false;
}

void NoteObject(Player* bot, Destination& best, GameObject* object)
{
    if (!object)
        return;
    float const distance = bot->GetDistance(object);
    if (best.known && distance >= best.distance)
        return;
    best.creature = nullptr;
    best.object = object;
    best.x = object->GetPositionX();
    best.y = object->GetPositionY();
    best.z = object->GetPositionZ();
    best.distance = distance;
    best.entry = object->GetEntry();
    best.known = true;
    best.kill = false;
    best.talk = false;
    best.objectUse = true;
}

void NoteSpawn(Destination& best, float x, float y, float z, float distance, uint32 entry, bool kill, bool talk, bool objectUse)
{
    if (best.known && distance >= best.distance)
        return;
    best.creature = nullptr;
    best.object = nullptr;
    best.x = x;
    best.y = y;
    best.z = z;
    best.distance = distance;
    best.entry = entry;
    best.known = true;
    best.kill = kill;
    best.talk = talk;
    best.objectUse = objectUse;
}

struct TravelMemory
{
    float angle = 0.4f;
    bool anchorSet = false;
    float anchorX = 0.0f;
    float anchorY = 0.0f;
};

TravelMemory& MemoryFor(Player* bot)
{
    static std::unordered_map<uint64_t, TravelMemory> memory;
    TravelMemory& item = memory[bot->GetGUID().GetCounter()];
    if (!item.anchorSet)
    {
        item.anchorSet = true;
        if (bot->m_homebind.GetMapId() == bot->GetMapId())
        {
            item.anchorX = bot->m_homebind.GetPositionX();
            item.anchorY = bot->m_homebind.GetPositionY();
        }
        else
        {
            item.anchorX = bot->GetPositionX();
            item.anchorY = bot->GetPositionY();
        }
    }
    return item;
}

bool WanderInsideLeash(Player* bot)
{
    TravelMemory& memory = MemoryFor(bot);
    SeekFacts facts;
    facts.anchorDistance = bot->GetExactDist2d(memory.anchorX, memory.anchorY);
    if (ChooseSeek(facts) == SeekAction::ReturnToLeash)
        return MoveToward(bot, memory.anchorX, memory.anchorY);
    float x = 0.0f;
    float y = 0.0f;
    LeashedStep(memory.anchorX, memory.anchorY, bot->GetPositionX(), bot->GetPositionY(), memory.angle, WanderStepYards, WanderLeashYards, x, y);
    if (MoveToward(bot, x, y))
        return true;
    memory.angle += 0.9f;
    LeashedStep(memory.anchorX, memory.anchorY, bot->GetPositionX(), bot->GetPositionY(), memory.angle, WanderStepYards, WanderLeashYards, x, y);
    return MoveToward(bot, x, y);
}

bool ActOnStarterQuest(Player* bot, bool pvpEnabled, Unit*& combatTarget)
{
    combatTarget = nullptr;
    Destination best;

    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32_t const questId = bot->GetQuestSlotQuestId(slot);
        if (!questId)
            continue;
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest || bot->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE)
            continue;
        Destination ender;
        for (auto const& relation : sObjectMgr->GetCreatureQuestInvolvedRelationReverseBounds(questId))
        {
            uint32 const entry = relation.second;
            bool live = false;
            for (Creature* creature : LiveEntries(bot, GiverSeekYards, entry))
            {
                NoteCreature(bot, ender, creature, false, false);
                live = true;
            }
            if (live)
                continue;
            float x = 0.0f, y = 0.0f, z = 0.0f, distance = 1.0e9f;
            if (NearestSpawn(true, bot->GetMapId(), entry, bot->GetPositionX(), bot->GetPositionY(), x, y, z, distance))
                NoteSpawn(ender, x, y, z, distance, entry, false, false, false);
        }
        for (auto const& relation : sObjectMgr->GetGOQuestInvolvedRelationReverseBounds(questId))
        {
            uint32 const entry = relation.second;
            bool live = false;
            for (GameObject* object : LiveObjects(bot, GiverSeekYards, entry))
            {
                NoteObject(bot, ender, object);
                live = true;
            }
            if (live)
                continue;
            float x = 0.0f, y = 0.0f, z = 0.0f, distance = 1.0e9f;
            if (NearestSpawn(false, bot->GetMapId(), entry, bot->GetPositionX(), bot->GetPositionY(), x, y, z, distance))
                NoteSpawn(ender, x, y, z, distance, entry, false, false, true);
        }
        if (!ender.known)
            continue;
        bool const choice = quest->GetRewChoiceItemsCount() > 0;
        bool const canReward = bot->CanRewardQuest(quest, false) && bot->CanRewardQuest(quest, LootItemType::Item, 0, false);
        if (ender.creature && ender.distance <= INTERACTION_DISTANCE && MayTurnInQuest(true, canReward, true, choice))
        {
            bot->RewardQuest(quest, LootItemType::Item, 0, ender.creature, true);
            return true;
        }
        if (ender.object && ender.distance <= INTERACTION_DISTANCE)
        {
            ender.object->Use(bot);
            return true;
        }
        if (MoveToward(bot, ender.x, ender.y))
        {
            DebugDecision(bot, "seek", "turn-in", ender.creature, nullptr, ender.entry);
            return true;
        }
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
            if (objective.ObjectID <= 0)
                continue;
            if (objective.Amount > 0 && bot->GetQuestObjectiveData(objective) >= objective.Amount)
                continue;
            if (ClassifyObjective(objective.Type) == ObjectiveSupport::Item)
            {
                ScanStats ignoredDead;
                bool looted = false;
                for (Creature* creature : CollectCreatures(bot, INTERACTION_DISTANCE, ignoredDead, FindCreatureAliveState::Dead))
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
            if (!ObjectiveNeedsSeek(objective.Type))
                continue;
            uint32 const entry = uint32(objective.ObjectID);
            ObjectiveSupport const support = ClassifyObjective(objective.Type);
            if (support == ObjectiveSupport::GameObject)
            {
                bool live = false;
                for (GameObject* object : LiveObjects(bot, ObjectiveSeekYards, entry))
                {
                    NoteObject(bot, best, object);
                    live = true;
                }
                if (!live)
                {
                    float x = 0.0f, y = 0.0f, z = 0.0f, distance = 1.0e9f;
                    if (NearestSpawn(false, bot->GetMapId(), entry, bot->GetPositionX(), bot->GetPositionY(), x, y, z, distance))
                        NoteSpawn(best, x, y, z, distance, entry, false, false, true);
                }
                continue;
            }
            bool const kill = support == ObjectiveSupport::Kill;
            bool sawLive = false;
            bool accepted = false;
            for (Creature* creature : LiveEntries(bot, ObjectiveSeekYards, entry))
            {
                sawLive = true;
                if (IsMeleeBlacklisted(bot, creature->GetGUID()))
                    continue;
                if (kill && JudgeHostile(FactsFor(bot, creature, pvpEnabled), ObjectiveSeekYards) == HostileUse::Refuse)
                    continue;
                NoteCreature(bot, best, creature, kill, !kill);
                accepted = true;
            }
            if (!accepted && !sawLive)
            {
                float x = 0.0f, y = 0.0f, z = 0.0f, distance = 1.0e9f;
                if (NearestSpawn(true, bot->GetMapId(), entry, bot->GetPositionX(), bot->GetPositionY(), x, y, z, distance))
                    NoteSpawn(best, x, y, z, distance, entry, kill, !kill, false);
            }
        }
    }

    if (!best.known)
        return false;
    if (best.kill && best.creature && best.distance <= MeleeReachYards)
    {
        combatTarget = best.creature;
        return false;
    }
    if (best.talk && best.creature && best.distance <= INTERACTION_DISTANCE)
    {
        bot->TalkedToCreature(best.creature->GetEntry(), best.creature->GetGUID());
        return true;
    }
    if (best.objectUse && best.object && best.distance <= INTERACTION_DISTANCE)
    {
        best.object->Use(bot);
        return true;
    }
    if (!MoveToward(bot, best.x, best.y))
        return false;
    DebugDecision(bot, "seek", best.kill ? "objective-kill" : (best.talk ? "objective-talk" : "objective-object"), best.creature, nullptr, best.entry);
    return true;
}

bool DiscoverQuestOffer(Player* bot, bool pvpEnabled)
{
    ScanStats ignoredGivers;
    Creature* giver = nullptr;
    Quest const* offer = nullptr;
    float giverDist = GiverSeekYards + 1.0f;
    QuestGiverHistogram questHist;
    for (Creature* creature : CollectCreatures(bot, GiverSeekYards, ignoredGivers))
    {
        if (!creature || !creature->HasNpcFlag(UNIT_NPC_FLAG_QUESTGIVER))
            continue;
        ++questHist.npcs;
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
            offerFacts.questGiver = true;
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
        if (offers == 0)
        {
            ++questHist.noOffer;
            DebugQuestOffer(bot, FactsFor(bot, creature, pvpEnabled), 0, "no-offer", false, false);
        }
    }
    DebugQuestHistogram(bot, questHist);
    if (!giver || !offer)
    {
        float bestDistance = 1.0e9f;
        float bestX = 0.0f;
        float bestY = 0.0f;
        uint32_t bestEntry = 0;
        uint32_t bestQuestId = 0;
        Quest const* bestQuest = nullptr;
        FactionTemplateEntry const* mine = bot->GetFactionTemplateEntry();
        QuestRelations const* relations = sObjectMgr->GetCreatureQuestRelationMapHACK();
        if (relations)
        {
            for (auto const& relation : *relations)
            {
                uint32_t const entry = relation.first;
                uint32_t const questId = relation.second;
                if (!HasCreatureSpawn(bot->GetMapId(), entry))
                    continue;
                CreatureTemplate const* proto = sObjectMgr->GetCreatureTemplate(entry);
                bool hostile = false;
                if (proto && mine)
                    if (FactionTemplateEntry const* theirs = sFactionTemplateStore.LookupEntry(proto->faction))
                        hostile = theirs->IsHostileTo(mine);
                Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
                if (!quest)
                    continue;
                bool const inLog = bot->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE || bot->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE || bot->GetQuestStatus(questId) == QUEST_STATUS_FAILED;
                bool const canTake = bot->CanTakeQuest(quest, false) && bot->CanAddQuest(quest, false) && !bot->IsQuestRewarded(questId) && !inLog;
                if (!ConsiderQuestGiver(true, hostile, canTake))
                    continue;
                float x = 0.0f, y = 0.0f, z = 0.0f, distance = 1.0e9f;
                if (!NearestSpawn(true, bot->GetMapId(), entry, bot->GetPositionX(), bot->GetPositionY(), x, y, z, distance) || distance >= bestDistance)
                    continue;
                bestDistance = distance;
                bestX = x;
                bestY = y;
                bestEntry = entry;
                bestQuestId = questId;
                bestQuest = quest;
            }
        }
        if (PlayerbotDebug())
        {
            std::string const signature = Trinity::StringFormat("giver|{}|{}", bestEntry, bestQuestId);
            if (DebugDue(bot->GetGUID().GetCounter(), signature))
                WritePlayerbotLine(Trinity::StringFormat(
                    "bot {} {} quest-giver-pick entry={} quest={} dist={:.1f} map={}",
                    bot->GetName(), bot->GetGUID().ToString(), bestEntry, bestQuestId, bestQuest ? bestDistance : -1.0f, bot->GetMapId()));
        }
        if (!bestQuest)
            return false;
        for (Creature* creature : LiveEntries(bot, INTERACTION_DISTANCE, bestEntry))
        {
            if (creature && bot->GetDistance(creature) <= INTERACTION_DISTANCE)
            {
                bot->AddQuest(bestQuest, creature);
                return true;
            }
        }
        DebugDecision(bot, "seek", "quest-giver", nullptr, nullptr, bestEntry);
        return MoveToward(bot, bestX, bestY);
    }
    if (giverDist > INTERACTION_DISTANCE)
        return MoveToward(bot, giver->GetPositionX(), giver->GetPositionY());
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
