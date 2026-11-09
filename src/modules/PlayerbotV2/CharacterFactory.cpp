/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * Create path derived from the GPL-2.0 BotCharacterFactory in
 * wow-forever-source-0.1.33.zip
 * (sha256 8ed3810b3f1b04bdc049b705aaa38ace2311e5830c0f68b3ab8c0d493861c6e0).
 * Account creation, name pools, appearance repair, and GiveLevel(owner) are
 * not included. The pool account id is supplied by the caller.
 */

#include "CharacterFactory.h"
#include "BotLogin.h"
#include "LifecyclePolicy.h"
#include "QuestGuide.h"
#include "Session/BotSession.h"

#include "AccountMgr.h"
#include "CharacterCache.h"
#include "CharacterPackets.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RealmList.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "WorldSession.h"

#include <memory>
#include <string>
#include <vector>

namespace Playerbot::V2
{
namespace
{
struct InflightCreate
{
    std::string name;
    std::string surname;
    ObjectGuid guid;
    uint32_t account = 0;
};

std::vector<InflightCreate> g_inflight;

bool OwnedMarker(ObjectGuid guid, uint32_t account, BotFaction faction)
{
    QueryResult marker = CharacterDatabase.PQuery("SELECT account, faction, factory_version, start_level FROM playerbot_v2_character WHERE guid = {}", guid.GetCounter());
    if (!marker)
        return false;
    Field* fields = marker->Fetch();
    MarkerRow row;
    row.present = true;
    row.account = fields[0].GetUInt32();
    row.faction = fields[1].GetUInt8();
    row.factoryVersion = fields[2].GetUInt32();
    row.startLevel = fields[3].GetUInt8();
    return MarkerBindsOwner(row, account, faction);
}

void ReleaseInflight(ObjectGuid guid)
{
    std::erase_if(g_inflight, [guid](InflightCreate const& row) { return row.guid == guid; });
}
}
bool AcceptNamePart(std::string& name)
{
    if (!normalizePlayerName(name))
        return false;
    if (ObjectMgr::CheckPlayerName(name, LOCALE_enUS, true) != CHAR_NAME_SUCCESS)
        return false;
    if (sObjectMgr->IsReservedName(name))
        return false;
    return true;
}

bool ChooseBotNames(PersistRequest& request)
{
    bool const generateFirst = request.name.empty() || request.name == "auto";
    if (!generateFirst && !AcceptNamePart(request.name))
        return false;
    uint32_t const salt = request.character.poolAccountId * 17u + uint32_t(request.character.race) * 3u + request.character.sex;
    std::vector<std::string> taken;
    for (InflightCreate const& row : g_inflight)
        if (!row.name.empty() && !row.surname.empty())
            taken.push_back(row.name + " " + row.surname);
    for (uint32_t step = 0; step < 64; ++step)
    {
        BotName const pick = PickBotName(request.character.race, request.character.sex, salt + step, taken);
        if (pick.first.empty())
            return false;
        std::string first = generateFirst ? pick.first : request.name;
        std::string last = request.surname.empty() ? pick.last : request.surname;
        if (!AcceptNamePart(first) || !AcceptNamePart(last))
        {
            if (!generateFirst && !request.surname.empty())
                return false;
            continue;
        }
        std::string const full = first + " " + last;
        if (FullNameTaken(full, taken))
            continue;
        if (CharacterCacheEntry const* existing = sCharacterCache->GetCharacterCacheByName(first))
        {
            if (existing->Surname == last || generateFirst)
                continue;
        }
        request.name = std::move(first);
        request.surname = std::move(last);
        return true;
    }
    return false;
}

bool IsStarterClass(uint8_t classId)
{
    switch (classId)
    {
        case CLASS_WARRIOR:
        case CLASS_PALADIN:
        case CLASS_HUNTER:
        case CLASS_ROGUE:
        case CLASS_PRIEST:
        case CLASS_SHAMAN:
        case CLASS_MAGE:
        case CLASS_WARLOCK:
        case CLASS_DRUID:
            return true;
        default:
            return false;
    }
}

PersistResult PersistLevelOneCharacter(PersistRequest& request)
{
    NewCharacterDecision const admission = AdmitPersistence(request.config, request.alreadyOnline, request.character);
    if (admission.refusal != CreateRefusal::None)
        return { admission.refusal, ObjectGuid::Empty, "population gate, account, faction, or level refused the character before any database call" };

    switch (ScreenPoolIds(request.character.poolAccountId, request.otherFactionPoolAccount, admission.faction, request.poolFaction))
    {
        case PoolScreen::Missing:
            return { CreateRefusal::MissingPoolAccount, ObjectGuid::Empty, "pool account is not configured" };
        case PoolScreen::Shared:
            return { CreateRefusal::SharedPoolAccount, ObjectGuid::Empty, "alliance and horde pool accounts must be different accounts" };
        case PoolScreen::WrongFaction:
            return { CreateRefusal::WrongPoolFaction, ObjectGuid::Empty, "race faction does not match the pool account faction" };
        case PoolScreen::Ok:
            break;
    }

    if (!IsStarterClass(request.character.classId))
        return { CreateRefusal::InvalidClass, ObjectGuid::Empty, "only a classic starter class is created, at level 1" };

    if (request.character.sex != GENDER_MALE && request.character.sex != GENDER_FEMALE)
        return { CreateRefusal::InvalidClass, ObjectGuid::Empty, "gender must be male or female" };

    if (!sObjectMgr->GetPlayerInfo(request.character.race, request.character.classId))
        return { CreateRefusal::InvalidClass, ObjectGuid::Empty, "race and class have no starter position" };

    if (!ChooseBotNames(request))
        return { CreateRefusal::InvalidName, ObjectGuid::Empty, "first or last name was rejected by the character-name rules" };

    std::string normalized = request.name;
    if (!normalizePlayerName(normalized) || !AcceptNamePart(request.surname))
        return { CreateRefusal::InvalidName, ObjectGuid::Empty, "name or surname was rejected" };

    for (InflightCreate const& row : g_inflight)
        if (row.name == normalized && row.account == request.character.poolAccountId)
            return { CreateRefusal::None, row.guid, "create already queued for this marker name" };

    bool const nameTaken = sCharacterCache->GetCharacterCacheByName(normalized) != nullptr;
    if (nameTaken)
    {
        CharacterCacheEntry const* entry = sCharacterCache->GetCharacterCacheByName(normalized);
        bool const sameOwned = entry && OwnedMarker(entry->Guid, request.character.poolAccountId, admission.faction);
        switch (ClassifyName(true, sameOwned, entry ? entry->AccountId : 0, request.character.poolAccountId))
        {
            case NameUse::Reuse:
                return { CreateRefusal::None, entry->Guid, "this factory already owns that character; no second row is inserted and its level is left as it is" };
            case NameUse::Free:
                break;
            case NameUse::Reject:
                return { CreateRefusal::InvalidName, ObjectGuid::Empty, "name belongs to a character this factory does not own" };
        }
    }

    std::string accountName;
    if (!sAccountMgr->GetName(request.character.poolAccountId, accountName))
        return { CreateRefusal::UnknownAccount, ObjectGuid::Empty, "pool account does not exist; this factory does not create accounts" };

    uint32_t const security = sAccountMgr->GetSecurity(request.character.poolAccountId, int32_t(sRealmList->GetCurrentRealmId().Realm));
    if (!PoolSecurityAllowed(security))
        return { CreateRefusal::PrivilegedAccount, ObjectGuid::Empty, "pool account has GM or admin security; a player account is required" };

    uint32_t characterRows = 0;
    if (QueryResult count = CharacterDatabase.PQuery("SELECT COUNT(*) FROM characters WHERE account = {}", request.character.poolAccountId))
        characterRows = uint32_t(count->Fetch()[0].GetUInt64());
    uint32_t markerRows = 0;
    if (QueryResult marked = CharacterDatabase.PQuery("SELECT COUNT(*) FROM playerbot_v2_character WHERE account = {}", request.character.poolAccountId))
        markerRows = uint32_t(marked->Fetch()[0].GetUInt64());
    if (PersistedCapReached(markerRows, characterRows, request.maxOnAccount))
        return { CreateRefusal::AccountFull, ObjectGuid::Empty, "persisted characters or factory markers already fill this pool account" };

    auto createInfo = std::make_shared<WorldPackets::Character::CharacterCreateInfo>();
    createInfo->Race = request.character.race;
    createInfo->Class = request.character.classId;
    createInfo->Sex = request.character.sex;
    createInfo->Name = normalized;

    auto creationSession = std::make_shared<BotSession>(request.character.poolAccountId, std::move(accountName));
    std::shared_ptr<Player> created(new Player(creationSession.get()), [](Player* player)
    {
        player->CleanupsBeforeDelete();
        delete player;
    });
    created->GetMotionMaster()->Initialize();

    ObjectGuid::LowType const low = sObjectMgr->GetGenerator<HighGuid::Player>().Generate();
    if (!created->Create(low, createInfo.get()))
        return { CreateRefusal::CreateFailed, ObjectGuid::Empty, "Player::Create refused the character" };

    // Only the unsaved new character is forced to the factory start level. Login never does this.
    if (created->GetLevel() != NewBotLevel)
        created->SetLevel(NewBotLevel, false);
    if (created->GetLevel() != NewBotLevel)
        return { CreateRefusal::CreateFailed, ObjectGuid::Empty, "character could not be kept at level 1, so it was not saved" };
    created->SetAtLoginFlag(AT_LOGIN_FIRST);

    ObjectGuid const guid = created->GetGUID();
    CharacterDatabaseTransaction characters = CharacterDatabase.BeginTransaction();
    LoginDatabaseTransaction login = LoginDatabase.BeginTransaction();
    created->SaveToDB(login, characters, true);
    characters->PAppend("INSERT INTO playerbot_v2_character (guid, account, faction, factory_version, start_level) VALUES ({}, {}, {}, {}, {})",
        guid.GetCounter(), request.character.poolAccountId, uint32_t(admission.faction), FactoryVersion, uint32_t(NewBotLevel));
    std::string const surname = request.surname;
    if (!surname.empty())
    {
        std::string escapedSurname = surname;
        CharacterDatabase.EscapeString(escapedSurname);
        characters->PAppend("UPDATE characters SET surname = '{}' WHERE guid = {}", escapedSurname, guid.GetCounter());
    }

    LoginDatabasePreparedStatement* realmCharacters = LoginDatabase.GetPreparedStatement(LOGIN_REP_REALM_CHARACTERS);
    realmCharacters->setUInt32(0, characterRows + 1);
    realmCharacters->setUInt32(1, request.character.poolAccountId);
    realmCharacters->setUInt32(2, sRealmList->GetCurrentRealmId().Realm);
    login->Append(realmCharacters);

    auto finished = std::make_shared<bool>(false);
    uint32_t const poolAccount = request.character.poolAccountId;
    uint8_t const sex = request.character.sex;
    creationSession->AddTransactionCallback(CharacterDatabase.AsyncCommitTransaction(characters)).AfterComplete(
        [created, login, finished, poolAccount, sex, surname](bool success) mutable
        {
            if (success)
            {
                LoginDatabase.CommitTransaction(login);
                sScriptMgr->OnPlayerCreate(created.get());
                sCharacterCache->AddCharacterCacheEntry(created->GetGUID(), poolAccount, created->GetName(), sex, created->GetRace(), created->GetClass(), created->GetLevel(), false);
                sCharacterCache->UpdateCharacterSurname(created->GetGUID(), surname);
                TC_LOG_INFO("entities.player.character", "Playerbot V2 queued character {} {} ({}) level {} on pool account {}",
                    created->GetName(), surname, created->GetGUID().ToString(), uint32_t(created->GetLevel()), poolAccount);
            }
            else
            {
                ReleaseInflight(created->GetGUID());
                TC_LOG_ERROR("entities.player.character", "Playerbot V2 character insert failed for {} on pool account {}", created->GetName(), poolAccount);
            }
            *finished = true;
        });
    g_inflight.push_back({ normalized, surname, guid, poolAccount });
    HoldCreateSession(creationSession, finished);

    return { CreateRefusal::None, guid, "insert queued for a new level-1 character; login is a separate command and waits for the commit" };
}
}
