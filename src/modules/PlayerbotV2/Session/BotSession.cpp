/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include "BotSession.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Log.h"
#include "LoginQueryHolder.h"
#include "Player.h"
#include "SharedDefines.h"
#include "WorldPacket.h"

namespace Playerbot::V2
{
BotSession::BotSession(uint32 accountId, std::string name)
    : WorldSession(accountId, std::move(name), 0, std::string(), std::shared_ptr<WorldSocket>(), SEC_PLAYER,
        EXPANSION_CLASSIC, 0, std::string(), Minutes(0), 0, ClientBuild::VariantId{}, LOCALE_enUS, 0, false, true)
{
    // No InitializeSession(): that loads account collections and emits the auth handshake.
    // No Battlenet account query. The battlenet id stays 0 until a later, separate step.
}

bool BotSession::BeginLogin(ObjectGuid characterGuid)
{
    if (PlayerLoading() || GetPlayer())
    {
        TC_LOG_WARN("server.loading", "Playerbot V2 BeginLogin ignored; account {} is already loading or loaded", GetAccountId());
        return false;
    }

    SetPlayerLoading(characterGuid);
    auto holder = std::make_shared<LoginQueryHolder>(GetAccountId(), characterGuid);
    if (!holder->Initialize())
    {
        TC_LOG_ERROR("server.loading", "Playerbot V2 LoginQueryHolder failed for {}", characterGuid.ToString());
        ClearPlayerLoading();
        return false;
    }

    AddQueryHolderCallback(CharacterDatabase.DelayQueryHolder(holder)).AfterComplete(
        [this](SQLQueryHolderBase const& done)
        {
            HandlePlayerLogin(static_cast<LoginQueryHolder const&>(done));
        });
    return true;
}

void BotSession::SendPacket(WorldPacket const* /*packet*/, bool /*forced*/)
{
}

bool BotSession::Update(uint32 /*diff*/, PacketFilter& /*updater*/)
{
    ProcessQueryCallbacks();

    if (!IsForceExit())
        return true;

    if (Player* player = GetPlayer(); player && !PlayerLogout())
    {
        if (Group* group = player->GetGroup(); group && group->isBGGroup())
            group->RemoveMember(player->GetGUID(), GROUP_REMOVEMETHOD_LEAVE);
        LogoutPlayer(true);
    }
    return false;
}

void BotSession::KickPlayer(std::string_view reason)
{
    TC_LOG_INFO("server.loading", "Playerbot V2 session for account {} marked for exit: {}", GetAccountId(), reason);
    MarkForceExit();
}
}
