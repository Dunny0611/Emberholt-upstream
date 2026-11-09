/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * World-thread owner of headless sessions. A closed population gate returns
 * before a session is constructed. Login loads only a guid present in
 * playerbot_v2_character.
 */

#ifndef TRINITY_PLAYERBOT_V2_BOT_LOGIN_H
#define TRINITY_PLAYERBOT_V2_BOT_LOGIN_H

#include "CreationPolicy.h"
#include "ObjectGuid.h"
#include <memory>
#include <string>

namespace Playerbot::V2
{
class BotSession;

void HoldCreateSession(std::shared_ptr<BotSession> session, std::shared_ptr<bool> finished);
void TickSessions(uint32_t diff, bool gateClosed);
uint32_t OnlineCount(BotFaction faction);

// False when the gate is closed, the level is outside 1..maxLevel, the guid is
// empty, or the factory marker does not bind this account and faction.
// Current level is not required to still be 1. An unmarked character is not loaded.
bool TryScheduleLogin(PopulationConfig const& config, BotFaction faction, uint32_t alreadyOnline, uint32_t accountId, ObjectGuid character, uint8_t level, uint8_t maxLevel, std::string& detail);

// Kicks one session this module opened. A guid it does not own is left alone.
bool LogoutOwned(ObjectGuid character, std::string& detail);
}
#endif
