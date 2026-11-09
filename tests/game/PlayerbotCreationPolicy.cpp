/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include "tc_catch2.h"
#include "BotGroupRules.h"
#include "BotLogin.h"
#include "CharacterFactory.h"
#include "CreationPolicy.h"
#include "LifecyclePolicy.h"
#include "StarterCombat.h"
#include "StarterQuest.h"

using Playerbot::V2::AcceptGroupInvite;
using Playerbot::V2::AdmitPersistence;
using Playerbot::V2::AiStepsPerTick;
using Playerbot::V2::BotFaction;
using Playerbot::V2::ChooseStep;
using Playerbot::V2::ClassifyName;
using Playerbot::V2::CorePathIsUsable;
using Playerbot::V2::DropOwnedSession;
using Playerbot::V2::LocalStepPoint;
using Playerbot::V2::NameUse;
using Playerbot::V2::PoolScreen;
using Playerbot::V2::PoolSecurityAllowed;
using Playerbot::V2::ScreenPoolIds;
using Playerbot::V2::StepInput;
using Playerbot::V2::StepKind;
using Playerbot::V2::AccumulateRefuse;
using Playerbot::V2::ApplyFactionRelation;
using Playerbot::V2::FactionTemplateFacts;
using Playerbot::V2::FormatQuestGiverHistogram;
using Playerbot::V2::FormatRefuseHistogram;
using Playerbot::V2::HostileFacts;
using Playerbot::V2::HostileRefuseReason;
using Playerbot::V2::HostileUse;
using Playerbot::V2::ChoosePreyIndex;
using Playerbot::V2::ClientCanAttack;
using Playerbot::V2::ConsiderQuestGiver;
using Playerbot::V2::MeleeTargetUnreachable;
using Playerbot::V2::ChooseSwing;
using Playerbot::V2::NextSwingTimer;
using Playerbot::V2::SeekQuestBeforePrey;
using Playerbot::V2::SwingAction;
using Playerbot::V2::SwingCallsAttack;
using Playerbot::V2::SwingFacts;
using Playerbot::V2::ChooseSeek;
using Playerbot::V2::AttackAllowed;
using Playerbot::V2::IsMajorCityFaction;
using Playerbot::V2::JudgeHostile;
using Playerbot::V2::NeedsSetAtWar;
using Playerbot::V2::ValidAttackAfterAtWar;
using Playerbot::V2::LeashedStep;
using Playerbot::V2::MapSpawn;
using Playerbot::V2::NearestSpawnIndex;
using Playerbot::V2::ObjectiveNeedsSeek;
using Playerbot::V2::PreySeekYards;
using Playerbot::V2::SeekAction;
using Playerbot::V2::SeekFacts;
using Playerbot::V2::WanderLeashYards;
using Playerbot::V2::MayOfferQuest;
using Playerbot::V2::NoteQuestOffer;
using Playerbot::V2::NpcFlagNonCombatService;
using Playerbot::V2::QuestGiverHistogram;
using Playerbot::V2::QuestOfferFacts;
using Playerbot::V2::QuestOfferReason;
using Playerbot::V2::RefuseHistogram;
using Playerbot::V2::ResurrectionSicknessApplies;
using Playerbot::V2::ShouldFinishSpiritResurrect;
using Playerbot::V2::UnitFlagImmuneToPc;
using Playerbot::V2::UnitFlagNonAttackable;
using Playerbot::V2::UnitFlagNotSelectable;
using Playerbot::V2::KnownSpellFacts;
using Playerbot::V2::HarmfulSpellReady;
using Playerbot::V2::ShouldFollowLeader;
using Playerbot::V2::RecoveryKind;
using Playerbot::V2::BotTeleportKind;
using Playerbot::V2::CanSeeBlock;
using Playerbot::V2::CanSeeSamePhaseCreature;
using Playerbot::V2::ChooseRecovery;
using Playerbot::V2::ClassifyBotTeleport;
using Playerbot::V2::MayResurrectAfterTeleport;
using Playerbot::V2::SeeFacts;
using Playerbot::V2::TeleportAckCompletes;
using Playerbot::V2::MarkerRow;
using Playerbot::V2::MarkerBindsOwner;
using Playerbot::V2::LoginLevelAllowed;
using Playerbot::V2::PersistedCapReached;
using Playerbot::V2::FactoryVersion;
using Playerbot::V2::AppendAccountIds;
using Playerbot::V2::DecisionIntervalMs;
using Playerbot::V2::ClassifyObjective;
using Playerbot::V2::ObjectiveSupport;
using Playerbot::V2::MayAcceptQuest;
using Playerbot::V2::MayTurnInQuest;
using Playerbot::V2::PlanObjective;
using Playerbot::V2::ObjectivePlan;
using Playerbot::V2::CreateRefusal;
using Playerbot::V2::DecideNewCharacter;
using Playerbot::V2::NewCharacterDecision;
using Playerbot::V2::NewCharacterRequest;
using Playerbot::V2::PersistLevelOneCharacter;
using Playerbot::V2::PersistRequest;
using Playerbot::V2::PersistResult;
using Playerbot::V2::TryScheduleLogin;
using Playerbot::V2::BotOrigin;
using Playerbot::V2::FactionForStarterRace;
using Playerbot::V2::LevelForCreation;
using Playerbot::V2::MayEnterWorld;
using Playerbot::V2::PopulationAllowsNewBot;
using Playerbot::V2::PopulationConfig;

TEST_CASE("New bots are level 1 and existing characters keep their level", "[PlayerbotCreation]")
{
    CHECK(LevelForCreation(BotOrigin::NewBot, 1, 60) == 1);
    CHECK(LevelForCreation(BotOrigin::NewBot, 20, 60) == 1);
    CHECK(LevelForCreation(BotOrigin::OwnerAlt, 37, 60) == 37);
    CHECK(LevelForCreation(BotOrigin::RealCharacter, 60, 1) == 60);
}

TEST_CASE("Starter races map to a faction and other races do not", "[PlayerbotCreation]")
{
    CHECK(FactionForStarterRace(1) == BotFaction::Alliance);
    CHECK(FactionForStarterRace(7) == BotFaction::Alliance);
    CHECK(FactionForStarterRace(2) == BotFaction::Horde);
    CHECK(FactionForStarterRace(8) == BotFaction::Horde);
    CHECK(FactionForStarterRace(0) == BotFaction::Unknown);
    CHECK(FactionForStarterRace(25) == BotFaction::Unknown);
}

TEST_CASE("Population is off by default and the kill switch stops creation", "[PlayerbotCreation]")
{
    PopulationConfig const defaults;
    CHECK_FALSE(defaults.enable);
    CHECK(defaults.killSwitch);
    CHECK(defaults.allianceCap == 0);
    CHECK(defaults.hordeCap == 0);
    CHECK_FALSE(PopulationAllowsNewBot(defaults, BotFaction::Alliance, 0));
    CHECK_FALSE(PopulationAllowsNewBot(defaults, BotFaction::Horde, 0));

    PopulationConfig armed = defaults;
    armed.enable = true;
    armed.killSwitch = false;
    armed.allianceCap = 2;
    armed.hordeCap = 1;
    CHECK(PopulationAllowsNewBot(armed, BotFaction::Alliance, 0));
    CHECK(PopulationAllowsNewBot(armed, BotFaction::Alliance, 1));
    CHECK_FALSE(PopulationAllowsNewBot(armed, BotFaction::Alliance, 2));
    CHECK(PopulationAllowsNewBot(armed, BotFaction::Horde, 0));
    CHECK_FALSE(PopulationAllowsNewBot(armed, BotFaction::Horde, 1));
    CHECK_FALSE(PopulationAllowsNewBot(armed, BotFaction::Unknown, 0));

    armed.killSwitch = true;
    CHECK_FALSE(PopulationAllowsNewBot(armed, BotFaction::Alliance, 0));
}

TEST_CASE("Bots are not placed in the world until navigation is validated", "[PlayerbotCreation]")
{
    PopulationConfig armed;
    armed.enable = true;
    armed.killSwitch = false;
    armed.allianceCap = 1;
    CHECK_FALSE(MayEnterWorld(armed, BotFaction::Alliance, 0, false));
    CHECK(MayEnterWorld(armed, BotFaction::Alliance, 0, true));
    CHECK_FALSE(MayEnterWorld(armed, BotFaction::Alliance, 1, true));
}

TEST_CASE("A new character is refused unless the pool account, level, and navigation gate all pass", "[PlayerbotCreation]")
{
    PopulationConfig armed;
    armed.enable = true;
    armed.killSwitch = false;
    armed.allianceCap = 1;
    NewCharacterRequest request;
    request.poolAccountId = 50;
    request.ownerAccountId = 9;
    request.race = 1;
    request.level = 1;

    CHECK(DecideNewCharacter(armed, 0, false, request).refusal == CreateRefusal::GateClosed);

    request.poolAccountId = 0;
    CHECK(DecideNewCharacter(armed, 0, true, request).refusal == CreateRefusal::MissingPoolAccount);

    request.poolAccountId = 9;
    CHECK(DecideNewCharacter(armed, 0, true, request).refusal == CreateRefusal::OwnerAccount);

    request.poolAccountId = 50;
    request.level = 60;
    CHECK(DecideNewCharacter(armed, 0, true, request).refusal == CreateRefusal::WrongLevel);
    CHECK(DecideNewCharacter(armed, 0, true, request).level == 1);

    request.level = 1;
    request.race = 25;
    CHECK(DecideNewCharacter(armed, 0, true, request).refusal == CreateRefusal::UnknownFaction);

    request.race = 1;
    NewCharacterDecision const admitted = DecideNewCharacter(armed, 0, true, request);
    CHECK(admitted.refusal == CreateRefusal::None);
    CHECK(admitted.level == 1);
    CHECK(admitted.faction == BotFaction::Alliance);
}

TEST_CASE("Tap and loot rules match the core when the group has no bot", "[PlayerbotCreation]")
{
    CHECK(BotGroupRules::SharesTap(true, false, false, false));
    CHECK(BotGroupRules::SharesTap(false, true, false, false));
    CHECK_FALSE(BotGroupRules::SharesTap(false, false, false, false));
    CHECK(BotGroupRules::SharesTap(false, false, true, false));
    CHECK_FALSE(BotGroupRules::SharesTap(false, false, true, true));

    std::vector<BotGroupRules::LooterCandidate> candidates(3);
    candidates[0].inRange = false;
    candidates[1].inRange = true;
    candidates[2].inRange = true;
    CHECK(BotGroupRules::NextRoundRobinLooter(candidates, 0, false) == 1);
    CHECK(BotGroupRules::NextRoundRobinLooter(candidates, 1, true) == 1);
    CHECK(BotGroupRules::NextRoundRobinLooter(candidates, 1, false) == 2);

    candidates[1].bot = true;
    CHECK(BotGroupRules::NextRoundRobinLooter(candidates, 0, false) == 2);
}

#ifdef TRINITY_PLAYERBOT_V2
TEST_CASE("A closed gate returns before a character insert or a login session", "[PlayerbotCreation]")
{
    PopulationConfig const defaults;
    PersistRequest request;
    request.config = defaults;
    request.character.poolAccountId = 50;
    request.character.ownerAccountId = 9;
    request.character.race = 1;
    request.character.classId = 1;
    request.character.sex = 0;
    request.character.level = 1;
    request.name = "Notcreated";

    PersistResult const created = PersistLevelOneCharacter(request);
    CHECK(created.refusal == CreateRefusal::GateClosed);
    CHECK(created.guid.IsEmpty());

    std::string detail;
    CHECK_FALSE(TryScheduleLogin(defaults, BotFaction::Alliance, 0, 50, ObjectGuid::Empty, 1, 60, detail));
    CHECK(detail == "population gate is closed");
}

TEST_CASE("Persistence admission does not require the navigation flag", "[PlayerbotCreation]")
{
    PopulationConfig armed;
    armed.enable = true;
    armed.killSwitch = false;
    armed.allianceCap = 1;
    NewCharacterRequest request;
    request.poolAccountId = 50;
    request.ownerAccountId = 9;
    request.race = 1;
    request.level = 1;
    NewCharacterDecision const admitted = AdmitPersistence(armed, 0, request);
    CHECK(admitted.refusal == CreateRefusal::None);
    CHECK(admitted.level == 1);
    CHECK_FALSE(MayEnterWorld(armed, BotFaction::Alliance, 0, false));
}
#endif

TEST_CASE("Pool accounts stay dedicated, unprivileged, and faction-matched", "[PlayerbotCreation]")
{
    CHECK(ScreenPoolIds(0, 0, BotFaction::Alliance, BotFaction::Alliance) == PoolScreen::Missing);
    CHECK(ScreenPoolIds(50, 50, BotFaction::Alliance, BotFaction::Alliance) == PoolScreen::Shared);
    CHECK(ScreenPoolIds(50, 60, BotFaction::Horde, BotFaction::Alliance) == PoolScreen::WrongFaction);
    CHECK(ScreenPoolIds(50, 60, BotFaction::Alliance, BotFaction::Alliance) == PoolScreen::Ok);
    CHECK(PoolSecurityAllowed(0));
    CHECK_FALSE(PoolSecurityAllowed(2));
}

TEST_CASE("A taken name is reused only for the same factory-owned character", "[PlayerbotCreation]")
{
    CHECK(ClassifyName(false, false, 0, 50) == NameUse::Free);
    CHECK(ClassifyName(true, true, 50, 50) == NameUse::Reuse);
    CHECK(ClassifyName(true, false, 50, 50) == NameUse::Reject);
    CHECK(ClassifyName(true, true, 9, 50) == NameUse::Reject);
}

TEST_CASE("A leveled factory character can log in and an unmarked or moved one cannot", "[PlayerbotCreation]")
{
    CHECK(LoginLevelAllowed(1, 60));
    CHECK(LoginLevelAllowed(20, 60));
    CHECK(LoginLevelAllowed(60, 60));
    CHECK_FALSE(LoginLevelAllowed(0, 60));
    CHECK_FALSE(LoginLevelAllowed(61, 60));

    MarkerRow owned;
    owned.present = true;
    owned.account = 50;
    owned.faction = uint8_t(BotFaction::Alliance);
    owned.factoryVersion = FactoryVersion;
    owned.startLevel = 1;
    CHECK(MarkerBindsOwner(owned, 50, BotFaction::Alliance));
    CHECK_FALSE(MarkerBindsOwner(owned, 9, BotFaction::Alliance));
    CHECK_FALSE(MarkerBindsOwner(owned, 50, BotFaction::Horde));
    owned.factoryVersion = 0;
    CHECK_FALSE(MarkerBindsOwner(owned, 50, BotFaction::Alliance));
    owned.factoryVersion = FactoryVersion;
    owned.startLevel = 10;
    CHECK_FALSE(MarkerBindsOwner(owned, 50, BotFaction::Alliance));
    owned.startLevel = 1;
    owned.present = false;
    CHECK_FALSE(MarkerBindsOwner(owned, 50, BotFaction::Alliance));

    CHECK_FALSE(PersistedCapReached(0, 0, 10));
    CHECK(PersistedCapReached(10, 0, 10));
    CHECK(PersistedCapReached(0, 10, 10));
    CHECK(PersistedCapReached(0, 0, 0));
}

TEST_CASE("Starter quests accept, kill or use, and turn in only through the core checks", "[PlayerbotCreation]")
{
    CHECK(ClassifyObjective(0) == ObjectiveSupport::Kill);
    CHECK(ClassifyObjective(2) == ObjectiveSupport::GameObject);
    CHECK(ClassifyObjective(1) == ObjectiveSupport::Item);
    CHECK(ClassifyObjective(3) == ObjectiveSupport::Talk);
    CHECK(ClassifyObjective(4) == ObjectiveSupport::Unsupported);
    CHECK(PlanObjective(1, true, true) == ObjectivePlan::Loot);
    CHECK(PlanObjective(3, true, false) == ObjectivePlan::Talk);
    CHECK(PlanObjective(4, true, true) == ObjectivePlan::Skip);
    CHECK(MayAcceptQuest(true, true, true, false, false));
    CHECK_FALSE(MayAcceptQuest(true, false, true, false, false));
    CHECK_FALSE(MayAcceptQuest(true, true, true, true, false));
    CHECK(MayTurnInQuest(true, true, true, false));
    CHECK_FALSE(MayTurnInQuest(true, true, true, true));
    CHECK_FALSE(MayTurnInQuest(false, true, true, false));
    CHECK(PlanObjective(0, true, true) == ObjectivePlan::Attack);
    CHECK(PlanObjective(0, true, false) == ObjectivePlan::Refuse);
    CHECK(PlanObjective(2, true, false) == ObjectivePlan::UseObject);

    std::vector<uint32_t> accounts;
    AppendAccountIds("11, 12,11,0 13", accounts);
    CHECK(accounts.size() == 3);
    CHECK(accounts[0] == 11);
    CHECK(accounts[1] == 12);
    CHECK(accounts[2] == 13);
    CHECK(DecisionIntervalMs >= 250);
    CHECK(DecisionIntervalMs <= 500);
}

TEST_CASE("The kill switch and logout drop owned sessions only", "[PlayerbotCreation]")
{
    CHECK(DropOwnedSession(true, true));
    CHECK_FALSE(DropOwnedSession(false, true));
    CHECK_FALSE(DropOwnedSession(true, false));
}

TEST_CASE("A same-faction player invite is accepted and a bot or cross-faction invite is not", "[PlayerbotCreation]")
{
    CHECK(AcceptGroupInvite(true, false, BotFaction::Alliance, BotFaction::Alliance, false));
    CHECK_FALSE(AcceptGroupInvite(true, true, BotFaction::Alliance, BotFaction::Alliance, false));
    CHECK_FALSE(AcceptGroupInvite(true, false, BotFaction::Alliance, BotFaction::Horde, false));
    CHECK_FALSE(AcceptGroupInvite(false, false, BotFaction::Alliance, BotFaction::Alliance, false));
    CHECK_FALSE(AcceptGroupInvite(true, false, BotFaction::Horde, BotFaction::Horde, true));
}

TEST_CASE("Movement follows a core mmap path or a melee attacker and does not invent a shortcut", "[PlayerbotCreation]")
{
    CHECK(CorePathIsUsable(true, 0x01));
    CHECK(CorePathIsUsable(true, 0x04));
    CHECK_FALSE(CorePathIsUsable(true, 0x10));
    CHECK_FALSE(CorePathIsUsable(true, 0x08));
    CHECK_FALSE(CorePathIsUsable(true, 0x02));
    CHECK_FALSE(CorePathIsUsable(true, 0x40));
    CHECK_FALSE(CorePathIsUsable(true, 0x80));
    CHECK_FALSE(CorePathIsUsable(false, 0x01));
    CHECK(LocalStepPoint(3.0f, 4.0f, 1.0f));
    CHECK_FALSE(LocalStepPoint(20.0f, 0.0f, 0.0f));
    CHECK_FALSE(LocalStepPoint(0.0f, 0.0f, 9.0f));

    StepInput input;
    input.owned = true;
    CHECK(ChooseStep(input) == StepKind::Hold);
    input.attackerInMelee = true;
    CHECK(ChooseStep(input) == StepKind::Melee);
    input.attackerInMelee = false;
    input.corePathUsable = true;
    CHECK(ChooseStep(input) == StepKind::Path);
    input.stepsAlreadyThisTick = AiStepsPerTick;
    CHECK(ChooseStep(input) == StepKind::Hold);
    input.stepsAlreadyThisTick = 0;
    input.gateClosed = true;
    CHECK(ChooseStep(input) == StepKind::None);
    input.gateClosed = false;
    input.owned = false;
    CHECK(ChooseStep(input) == StepKind::None);
}

TEST_CASE("Hostile targets are level-appropriate creatures, and players stay refused unless PvP is on", "[PlayerbotCreation]")
{
    HostileFacts creature;
    creature.creature = true;
    creature.alive = true;
    creature.validAttack = true;
    creature.level = 2;
    creature.botLevel = 1;
    creature.distance = 4.0f;
    CHECK(JudgeHostile(creature) == HostileUse::Melee);
    creature.distance = 18.0f;
    CHECK(JudgeHostile(creature) == HostileUse::Approach);
    creature.distance = 45.0f;
    CHECK(JudgeHostile(creature) == HostileUse::Refuse);
    creature.distance = 10.0f;
    creature.level = 60;
    CHECK(JudgeHostile(creature) == HostileUse::Refuse);
    creature.level = 0;
    CHECK(JudgeHostile(creature) == HostileUse::Approach);
    creature.level = 1;
    creature.distance = 4.0f;
    creature.civilian = true;
    CHECK(JudgeHostile(creature) == HostileUse::Melee);
    creature.civilian = false;
    creature.level = 5;
    creature.distance = 4.0f;
    CHECK(JudgeHostile(creature) == HostileUse::Melee);
    creature.level = 8;
    CHECK(JudgeHostile(creature) == HostileUse::Refuse);
    creature.level = 1;
    creature.critter = true;
    CHECK(JudgeHostile(creature) == HostileUse::Refuse);
    creature.critter = false;
    creature.validAttack = false;
    creature.factionHostile = true;
    creature.level = 0;
    CHECK(JudgeHostile(creature) == HostileUse::Melee);
    CHECK(std::string_view(HostileRefuseReason(creature)) == "ok");
    creature.factionHostile = false;
    creature.level = 1;
    CHECK(JudgeHostile(creature) == HostileUse::Melee);
    CHECK(std::string_view(HostileRefuseReason(creature)) == "ok");
    creature.gameMaster = true;
    CHECK(JudgeHostile(creature) == HostileUse::Refuse);

    HostileFacts player;
    player.player = true;
    player.alive = true;
    player.validAttack = true;
    player.level = 1;
    player.botLevel = 1;
    player.distance = 4.0f;
    player.sameFaction = false;
    CHECK(JudgeHostile(player) == HostileUse::Refuse);
    player.pvpEnabled = true;
    CHECK(JudgeHostile(player) == HostileUse::Melee);
    player.sameFaction = true;
    CHECK(JudgeHostile(player) == HostileUse::Refuse);
    player.sameFaction = false;
    player.gameMaster = true;
    CHECK(JudgeHostile(player) == HostileUse::Refuse);
}

// Public rows: creature_template from cmangos classic-db 1.12 (ClassicDB_1_12_1_z2815);
// faction templates from the AzerothCore factiontemplate_dbc dump of the same classic ids.
// Reaction is forced friendly and IsValidAttackTarget is forced false. That is the
// clientless failure mode: reputation at-war and phase both say "not a target".
FactionTemplateFacts ClassicTemplate(uint32_t id, uint16_t faction, uint32_t flags, uint8_t group, uint8_t friends, uint8_t enemies, uint16_t friendFaction = 0)
{
    FactionTemplateFacts row;
    row.id = id;
    row.faction = faction;
    row.flags = flags;
    row.factionGroup = group;
    row.friendGroup = friends;
    row.enemyGroup = enemies;
    row.friends[0] = friendFaction;
    return row;
}

HostileFacts ClassicCreature(uint32_t entry, uint8_t level, uint32_t npcFlags, uint32_t unitFlags, bool civilian, FactionTemplateFacts const& creature, FactionTemplateFacts const& player)
{
    HostileFacts facts;
    facts.creature = true;
    facts.alive = true;
    facts.entry = entry;
    facts.level = level;
    facts.botLevel = 1;
    facts.distance = 10.0f;
    facts.npcFlags = npcFlags;
    facts.unitFlags = unitFlags;
    facts.civilian = civilian;
    facts.nonCombatService = (npcFlags & NpcFlagNonCombatService) != 0;
    facts.flagNonAttackable = (unitFlags & UnitFlagNonAttackable) != 0;
    facts.flagImmuneToPc = (unitFlags & UnitFlagImmuneToPc) != 0;
    facts.flagNotSelectable = (unitFlags & UnitFlagNotSelectable) != 0;
    facts.immune = facts.flagNonAttackable || facts.flagImmuneToPc || facts.flagNotSelectable;
    facts.samePhase = true;
    facts.los = true;
    facts.validAttack = false;
    facts.factionHostile = false;
    facts.friendlyTo = true;
    facts.reaction = 4;
    ApplyFactionRelation(facts, creature, player);
    return facts;
}

TEST_CASE("Classic starter beasts stay eligible when reaction and attack checks both fail", "[PlayerbotCreation]")
{
    FactionTemplateFacts const horde = ClassicTemplate(2, 2, 72, 5, 4, 10);
    FactionTemplateFacts const alliance = ClassicTemplate(1, 1, 72, 3, 2, 12);
    FactionTemplateFacts const beast = ClassicTemplate(189, 7, 1024, 0, 0, 0);
    FactionTemplateFacts const creatureNeutral = ClassicTemplate(7, 7, 0, 0, 0, 0);
    FactionTemplateFacts const trogg = ClassicTemplate(36, 32, 1025, 0, 0, 0, 32);
    FactionTemplateFacts const cragBoar = ClassicTemplate(1094, 23, 1024, 0, 0, 0);
    FactionTemplateFacts const orgrimmar = ClassicTemplate(29, 76, 0, 4, 4, 2);
    FactionTemplateFacts const ironforge = ClassicTemplate(55, 47, 0, 2, 2, 4);
    FactionTemplateFacts const monster = ClassicTemplate(14, 14, 0, 8, 0, 1);
    FactionTemplateFacts const ambient = ClassicTemplate(35, 31, 0, 0, 1, 0);

    HostileFacts mottledBoar = ClassicCreature(3098, 1, 0, 0, false, beast, horde);
    HostileFacts mottledBoarMax = ClassicCreature(3098, 2, 0, 0, false, beast, horde);
    HostileFacts scorpid = ClassicCreature(3124, 3, 0, 0, false, creatureNeutral, horde);
    HostileFacts rockjaw = ClassicCreature(707, 1, 0, 0, true, trogg, alliance);
    HostileFacts burly = ClassicCreature(724, 2, 0, 0, true, trogg, alliance);
    HostileFacts crag = ClassicCreature(708, 3, 0, 0, false, cragBoar, alliance);
    CHECK(JudgeHostile(mottledBoar) == HostileUse::Approach);
    CHECK_FALSE(NeedsSetAtWar(mottledBoar));
    HostileFacts const kaltunk = ClassicCreature(10176, 20, 2, 4864, true, orgrimmar, horde);
    HostileFacts const gornek = ClassicCreature(3143, 5, 3, 4864, true, orgrimmar, horde);
    HostileFacts const sten = ClassicCreature(658, 5, 3, 0, true, ironforge, alliance);
    HostileFacts const balir = ClassicCreature(713, 5, 2, 0, true, ironforge, alliance);

    CHECK(JudgeHostile(mottledBoar) == HostileUse::Approach);
    CHECK(JudgeHostile(mottledBoarMax) == HostileUse::Approach);
    CHECK(JudgeHostile(scorpid) == HostileUse::Approach);
    CHECK(JudgeHostile(rockjaw) == HostileUse::Approach);
    CHECK(JudgeHostile(burly) == HostileUse::Approach);
    CHECK(JudgeHostile(crag) == HostileUse::Approach);
    CHECK(std::string_view(HostileRefuseReason(mottledBoar)) == "ok");
    CHECK(mottledBoar.neutralPrey);
    CHECK(rockjaw.neutralPrey);
    CHECK(rockjaw.civilian);

    CHECK(JudgeHostile(kaltunk) == HostileUse::Refuse);
    CHECK(std::string_view(HostileRefuseReason(kaltunk)) == "flag-immune-to-pc");
    CHECK(JudgeHostile(gornek) == HostileUse::Refuse);
    CHECK(std::string_view(HostileRefuseReason(gornek)) == "flag-immune-to-pc");
    CHECK(JudgeHostile(sten) == HostileUse::Refuse);
    CHECK(std::string_view(HostileRefuseReason(sten)) == "civilian");
    CHECK(JudgeHostile(balir) == HostileUse::Refuse);
    CHECK(std::string_view(HostileRefuseReason(balir)) == "civilian");

    HostileFacts monsterPrey = ClassicCreature(1, 1, 0, 0, false, monster, horde);
    monsterPrey.distance = 4.0f;
    monsterPrey.validAttack = true;
    CHECK(JudgeHostile(monsterPrey) == HostileUse::Melee);

    HostileFacts friendlyAmbient = ClassicCreature(1, 1, 0, 0, false, ambient, horde);
    CHECK(JudgeHostile(friendlyAmbient) == HostileUse::Refuse);
    CHECK(std::string_view(HostileRefuseReason(friendlyAmbient)) == "friendly-template");

    HostileFacts critter = mottledBoar;
    critter.critter = true;
    CHECK(JudgeHostile(critter) == HostileUse::Refuse);
    CHECK(std::string_view(HostileRefuseReason(critter)) == "critter");
    HostileFacts pet = mottledBoar;
    pet.pet = true;
    CHECK(std::string_view(HostileRefuseReason(pet)) == "pet");
    HostileFacts totem = mottledBoar;
    totem.totem = true;
    CHECK(std::string_view(HostileRefuseReason(totem)) == "totem");
    HostileFacts gameMaster = mottledBoar;
    gameMaster.gameMaster = true;
    CHECK(std::string_view(HostileRefuseReason(gameMaster)) == "gm");
    HostileFacts tall = mottledBoar;
    tall.level = 60;
    CHECK(std::string_view(HostileRefuseReason(tall)) == "level-or-range");

    HostileFacts sameFactionPlayer;
    sameFactionPlayer.player = true;
    sameFactionPlayer.alive = true;
    sameFactionPlayer.validAttack = true;
    sameFactionPlayer.pvpEnabled = true;
    sameFactionPlayer.sameFaction = true;
    sameFactionPlayer.level = 1;
    sameFactionPlayer.botLevel = 1;
    sameFactionPlayer.distance = 4.0f;
    CHECK(std::string_view(HostileRefuseReason(sameFactionPlayer)) == "same-faction");

    RefuseHistogram histogram;
    uint32_t kept = 0;
    uint32_t refused = 0;
    auto scan = [&](HostileFacts const& facts)
    {
        if (JudgeHostile(facts) == HostileUse::Refuse)
        {
            ++refused;
            AccumulateRefuse(histogram, HostileRefuseReason(facts));
        }
        else
            ++kept;
    };
    scan(mottledBoar);
    scan(scorpid);
    scan(rockjaw);
    scan(burly);
    scan(crag);
    scan(kaltunk);
    scan(gornek);
    scan(sten);
    scan(balir);
    CHECK(kept == 5);
    CHECK(refused == 4);
    CHECK(histogram.notHostile == 0);
    CHECK(histogram.civilian == 2);
    CHECK(histogram.flagImmuneToPc == 2);
    CHECK(FormatRefuseHistogram(histogram).find("not-hostile=0") != std::string::npos);

    QuestOfferFacts offer;
    offer.alive = true;
    offer.questGiver = true;
    offer.canTake = true;
    offer.canAdd = true;
    CHECK(MayOfferQuest(offer));
    CHECK(std::string_view(QuestOfferReason(offer)) == "can-take");
    CHECK(JudgeHostile(kaltunk) == HostileUse::Refuse);
    CHECK(JudgeHostile(sten) == HostileUse::Refuse);
    offer.canTake = false;
    CHECK_FALSE(MayOfferQuest(offer));
    CHECK(std::string_view(QuestOfferReason(offer)) == "cannot-take");
    QuestGiverHistogram quests;
    NoteQuestOffer(quests, "can-take");
    NoteQuestOffer(quests, "cannot-take");
    quests.npcs = 2;
    CHECK(FormatQuestGiverHistogram(quests) == "npcs=2 offers=2 canTake=1 cannotTake=1 noOffer=0");
}

TEST_CASE("A wolf outside 30 yards is sought, and friendly guards stay refused", "[PlayerbotCreation]")
{
    FactionTemplateFacts const alliance = ClassicTemplate(1, 1, 72, 3, 2, 12);
    FactionTemplateFacts const horde = ClassicTemplate(2, 2, 72, 5, 4, 10);
    FactionTemplateFacts const ironforgeGuard = ClassicTemplate(57, 47, 2081, 3, 2, 12);
    FactionTemplateFacts const orgrimmarGuard = ClassicTemplate(85, 76, 2081, 5, 4, 10);
    FactionTemplateFacts wolfFaction = ClassicTemplate(32, 29, 16, 0, 0, 0, 0);
    wolfFaction.enemies[0] = 28;

    HostileFacts mountaineer = ClassicCreature(853, 55, 0, 4096, false, ironforgeGuard, alliance);
    mountaineer.distance = 8.0f;
    CHECK(JudgeHostile(mountaineer) == HostileUse::Refuse);
    CHECK(std::string_view(HostileRefuseReason(mountaineer)) == "friendly-template");
    CHECK(mountaineer.templateFriendly);

    HostileFacts denGrunt = ClassicCreature(5952, 55, 0, 36864, false, orgrimmarGuard, horde);
    denGrunt.distance = 8.0f;
    CHECK(JudgeHostile(denGrunt) == HostileUse::Refuse);
    CHECK(std::string_view(HostileRefuseReason(denGrunt)) == "friendly-template");
    CHECK(denGrunt.templateFriendly);

    HostileFacts wolf = ClassicCreature(705, 1, 0, 0, true, wolfFaction, alliance);
    wolf.distance = 47.0f;
    CHECK(JudgeHostile(wolf) == HostileUse::Refuse);
    CHECK(std::string_view(HostileRefuseReason(wolf)) == "level-or-range");
    CHECK(JudgeHostile(wolf, PreySeekYards) == HostileUse::Approach);
    CHECK(wolf.neutralPrey);

    HostileFacts nearCritter = wolf;
    nearCritter.entry = 1;
    nearCritter.distance = 6.0f;
    nearCritter.critter = true;
    HostileFacts const row[3] = { mountaineer, nearCritter, wolf };
    CHECK(ChoosePreyIndex(row, 3, PreySeekYards) == 2);

    MapSpawn spawns[4];
    spawns[0] = { 0, 705, 47.0f, 0.0f, 0.0f, true };
    spawns[1] = { 0, 705, 200.0f, 0.0f, 0.0f, true };
    spawns[2] = { 1, 705, 10.0f, 0.0f, 0.0f, true };
    spawns[3] = { 0, 853, 5.0f, 0.0f, 0.0f, false };
    CHECK(NearestSpawnIndex(spawns, 4, 0, 705, 0.0f, 0.0f, 1000.0f) == 0);

    CHECK(ObjectiveNeedsSeek(0));
    CHECK(ObjectiveNeedsSeek(2));
    CHECK(ObjectiveNeedsSeek(3));
    CHECK_FALSE(ObjectiveNeedsSeek(1));

    SeekFacts seek;
    seek.objectiveKnown = true;
    seek.preyKnown = true;
    seek.preyDistance = 47.0f;
    CHECK(ChooseSeek(seek) == SeekAction::PathTo);
    seek.objectiveInRange = true;
    CHECK(ChooseSeek(seek) == SeekAction::Engage);
    seek = {};
    seek.turnInKnown = true;
    CHECK(ChooseSeek(seek) == SeekAction::PathTo);
    seek.turnInInRange = true;
    CHECK(ChooseSeek(seek) == SeekAction::Engage);
    seek = {};
    seek.preyKnown = true;
    seek.preyDistance = 47.0f;
    CHECK(ChooseSeek(seek) == SeekAction::PathTo);
    seek.preyInRange = true;
    seek.preyDistance = 4.0f;
    CHECK(ChooseSeek(seek) == SeekAction::Engage);
    seek = {};
    seek.offerKnown = true;
    seek.offerDistance = 70.0f;
    CHECK(ChooseSeek(seek) == SeekAction::PathTo);
    seek = {};
    seek.anchorDistance = 20.0f;
    CHECK(ChooseSeek(seek) == SeekAction::Wander);
    seek.anchorDistance = WanderLeashYards + 10.0f;
    CHECK(ChooseSeek(seek) == SeekAction::ReturnToLeash);

    float x = 0.0f;
    float y = 0.0f;
    LeashedStep(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 12.0f, WanderLeashYards, x, y);
    CHECK(x > 11.9f);
    CHECK(x < 12.1f);
    CHECK(y > -0.1f);
    CHECK(y < 0.1f);
    LeashedStep(0.0f, 0.0f, 140.0f, 0.0f, 0.0f, 20.0f, WanderLeashYards, x, y);
    CHECK(x > 149.0f);
    CHECK(x < 151.0f);
}

TEST_CASE("A running melee timer is not reset, and an empty quest log seeks a giver first", "[PlayerbotCreation]")
{
    SwingFacts waiting;
    waiting.inMeleeRange = true;
    waiting.inArc = true;
    waiting.attackReady = false;
    waiting.attackTimer = 1500;
    CHECK(ChooseSwing(waiting) == SwingAction::Wait);
    CHECK_FALSE(SwingCallsAttack(ChooseSwing(waiting)));
    CHECK(NextSwingTimer(waiting, 2000) == 1500);

    SwingFacts closing = waiting;
    closing.inMeleeRange = false;
    closing.attackReady = true;
    closing.attackTimer = 0;
    CHECK(ChooseSwing(closing) == SwingAction::Close);
    CHECK(NextSwingTimer(closing, 2000) == 0);

    SwingFacts facing = waiting;
    facing.attackReady = true;
    facing.attackTimer = 0;
    facing.inArc = false;
    CHECK(ChooseSwing(facing) == SwingAction::Face);
    CHECK(NextSwingTimer(facing, 2000) == 0);

    SwingFacts ready = waiting;
    ready.attackReady = true;
    ready.attackTimer = 0;
    CHECK(ChooseSwing(ready) == SwingAction::Swing);
    CHECK(SwingCallsAttack(ChooseSwing(ready)));
    CHECK(NextSwingTimer(ready, 2000) == 2000);

    CHECK(SeekQuestBeforePrey(0));
    CHECK_FALSE(SeekQuestBeforePrey(1));

    auto beast = [](uint32_t factionId, int32_t reputationIndex)
    {
        HostileFacts facts;
        facts.creature = true;
        facts.alive = true;
        facts.canSee = true;
        facts.level = 1;
        facts.botLevel = 1;
        facts.distance = 10.0f;
        facts.reputationFaction = factionId;
        facts.hasReputation = reputationIndex >= 0;
        facts.majorCity = IsMajorCityFaction(factionId);
        return facts;
    };
    // Public Faction.db2: Creature 7, Beast - Boar 23, and Beast - Wolf 29 have ReputationIndex -1.
    // Mottled Boar (template 189 -> 7) and Ragged Young Wolf (template 32 -> 29) need no at-war flag.
    HostileFacts boar = beast(7, -1);
    HostileFacts wolf = beast(29, -1);
    CHECK_FALSE(NeedsSetAtWar(boar));
    CHECK_FALSE(NeedsSetAtWar(wolf));
    CHECK(ValidAttackAfterAtWar(boar));
    CHECK(ValidAttackAfterAtWar(wolf));
    CHECK(AttackAllowed(boar) == false);
    boar.validAttack = true;
    wolf.validAttack = true;
    CHECK(AttackAllowed(boar));
    CHECK(AttackAllowed(wolf));
    CHECK(JudgeHostile(boar) == HostileUse::Approach);

    HostileFacts repBeast = beast(999, 40);
    repBeast.peaceful = false;
    CHECK(NeedsSetAtWar(repBeast));
    CHECK(ValidAttackAfterAtWar(repBeast));
    CHECK(AttackAllowed(repBeast));
    repBeast.atWar = true;
    CHECK_FALSE(NeedsSetAtWar(repBeast));
    CHECK(ValidAttackAfterAtWar(repBeast));
    repBeast.atWar = false;
    repBeast.peaceful = true;
    CHECK_FALSE(NeedsSetAtWar(repBeast));
    CHECK_FALSE(AttackAllowed(repBeast));

    uint32_t const cities[] = { 47u, 54u, 69u, 72u, 76u, 530u, 68u, 81u };
    for (uint32_t factionId : cities)
    {
        HostileFacts city = beast(factionId, 20);
        city.templateFriendly = true;
        city.friendlyTo = true;
        city.ownSide = true;
        CHECK(city.majorCity);
        CHECK_FALSE(NeedsSetAtWar(city));
        CHECK_FALSE(AttackAllowed(city));
        CHECK_FALSE(ValidAttackAfterAtWar(city));
    }

    CHECK_FALSE(MeleeTargetUnreachable(3000, 42, 42));
    CHECK(MeleeTargetUnreachable(8000, 42, 42));
    CHECK_FALSE(MeleeTargetUnreachable(8000, 42, 41));
    CHECK(ConsiderQuestGiver(true, false, true));
    CHECK_FALSE(ConsiderQuestGiver(false, false, true));
    CHECK_FALSE(ConsiderQuestGiver(true, true, true));
    CHECK_FALSE(ConsiderQuestGiver(true, false, false));
}

TEST_CASE("A class spell is cast only when it is already known, harmful, and in range", "[PlayerbotCreation]")
{
    KnownSpellFacts spell;
    spell.known = true;
    spell.active = true;
    spell.maxRange = 30.0f;
    spell.distance = 20.0f;
    CHECK(HarmfulSpellReady(spell));
    spell.positive = true;
    CHECK_FALSE(HarmfulSpellReady(spell));
    spell.positive = false;
    spell.cooldown = true;
    CHECK_FALSE(HarmfulSpellReady(spell));
    spell.cooldown = false;
    spell.known = false;
    CHECK_FALSE(HarmfulSpellReady(spell));
    spell.known = true;
    spell.distance = 40.0f;
    CHECK_FALSE(HarmfulSpellReady(spell));
    spell.distance = 5.0f;
    spell.maxRange = 0.0f;
    CHECK_FALSE(HarmfulSpellReady(spell));
}

TEST_CASE("Follow stays near the leader and death uses release, walk, or reclaim", "[PlayerbotCreation]")
{
    CHECK(ShouldFollowLeader(true, true, true, false, 12.0f));
    CHECK_FALSE(ShouldFollowLeader(true, true, true, true, 12.0f));
    CHECK_FALSE(ShouldFollowLeader(true, true, true, false, 80.0f));
    CHECK_FALSE(ShouldFollowLeader(true, true, false, false, 12.0f));
    CHECK_FALSE(ShouldFollowLeader(false, true, true, false, 12.0f));

    CHECK(ChooseRecovery(true, false, false, false, false, false) == RecoveryKind::None);
    CHECK(ChooseRecovery(false, false, false, false, false, false) == RecoveryKind::ReleaseSpirit);
    CHECK(ChooseRecovery(false, true, true, false, false, true) == RecoveryKind::WalkToCorpse);
    CHECK(ChooseRecovery(false, true, true, false, false, false) == RecoveryKind::Hold);
    CHECK(ChooseRecovery(false, true, true, true, true, false) == RecoveryKind::Reclaim);
    CHECK(ChooseRecovery(false, true, false, false, false, false) == RecoveryKind::SpiritHealer);
    CHECK(ShouldFinishSpiritResurrect(true, 0));
    CHECK_FALSE(ShouldFinishSpiritResurrect(false, 1000));
    CHECK(ShouldFinishSpiritResurrect(false, 20000));
    CHECK_FALSE(ResurrectionSicknessApplies(1));
    CHECK_FALSE(ResurrectionSicknessApplies(10));
    CHECK(ResurrectionSicknessApplies(11));

    CHECK(ClassifyBotTeleport(false, true) == BotTeleportKind::None);
    CHECK(ClassifyBotTeleport(true, true) == BotTeleportKind::Near);
    CHECK(ClassifyBotTeleport(true, false) == BotTeleportKind::Far);
    CHECK_FALSE(TeleportAckCompletes(BotTeleportKind::Near, false));
    CHECK_FALSE(TeleportAckCompletes(BotTeleportKind::Far, false));
    CHECK(TeleportAckCompletes(BotTeleportKind::Near, true));
    CHECK(TeleportAckCompletes(BotTeleportKind::Far, true));
    CHECK(TeleportAckCompletes(BotTeleportKind::None, false));
    CHECK_FALSE(MayResurrectAfterTeleport(true, false));
    CHECK(MayResurrectAfterTeleport(false, false));
    CHECK_FALSE(MayResurrectAfterTeleport(false, true));

    SeeFacts boar;
    boar.samePhase = true;
    boar.inSightRange = true;
    boar.sameMap = true;
    boar.inWorld = true;
    boar.seerIsSelf = true;
    boar.ghostVisible = true;
    CHECK(std::string_view(CanSeeBlock(boar)) == "transport-server-time");
    CHECK_FALSE(CanSeeSamePhaseCreature(boar));
    boar.transportServerTime = true;
    CHECK(CanSeeBlock(boar) == nullptr);
    CHECK(CanSeeSamePhaseCreature(boar));
    boar.samePhase = false;
    CHECK(std::string_view(CanSeeBlock(boar)) == "phase");
    boar.samePhase = true;
    boar.inSightRange = false;
    CHECK(std::string_view(CanSeeBlock(boar)) == "sight-range");
    boar.inSightRange = true;
    boar.teleporting = true;
    CHECK(std::string_view(CanSeeBlock(boar)) == "teleporting");

    HostileFacts boarFaction = ClassicCreature(3098, 1, 0, 0, false, ClassicTemplate(189, 7, 1024, 0, 0, 0), ClassicTemplate(2, 2, 72, 5, 4, 10));
    boarFaction.hasReputation = false;
    boarFaction.reputationFaction = 7;
    CHECK(ClientCanAttack(boarFaction));
    CHECK_FALSE(NeedsSetAtWar(boarFaction));
    CHECK(JudgeHostile(boarFaction) == HostileUse::Approach);

    HostileFacts wolfFactionFacts = ClassicCreature(705, 1, 0, 0, false, ClassicTemplate(32, 29, 16, 0, 0, 0), ClassicTemplate(1, 1, 72, 3, 2, 12));
    wolfFactionFacts.hasReputation = false;
    wolfFactionFacts.reputationFaction = 29;
    CHECK(ClientCanAttack(wolfFactionFacts));
    CHECK_FALSE(NeedsSetAtWar(wolfFactionFacts));

    HostileFacts cragFaction = ClassicCreature(708, 3, 0, 0, false, ClassicTemplate(1094, 23, 1024, 0, 0, 0), ClassicTemplate(1, 1, 72, 3, 2, 12));
    cragFaction.hasReputation = false;
    cragFaction.reputationFaction = 23;
    CHECK(ClientCanAttack(cragFaction));
    CHECK_FALSE(NeedsSetAtWar(cragFaction));
}
