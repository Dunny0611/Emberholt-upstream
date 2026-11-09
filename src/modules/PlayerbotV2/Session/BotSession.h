/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * Headless session derived from the GPL-2.0 BotSession in
 * wow-forever-source-0.1.33.zip
 * (sha256 8ed3810b3f1b04bdc049b705aaa38ace2311e5830c0f68b3ab8c0d493861c6e0).
 * Battlenet account lookup, guild-charter signing, and InitializeSession are
 * not part of this port. Nothing in the module constructs a session at startup.
 */

#ifndef TRINITY_PLAYERBOT_V2_BOT_SESSION_H
#define TRINITY_PLAYERBOT_V2_BOT_SESSION_H

#include "WorldSession.h"

namespace Playerbot::V2
{
class BotSession final : public WorldSession
{
public:
    BotSession(uint32 accountId, std::string name);
    ~BotSession() override = default;

    BotSession(BotSession const&) = delete;
    BotSession& operator=(BotSession const&) = delete;

    // Submits LoginQueryHolder and returns. Does not place the character.
    // The module startup path does not call this.
    bool BeginLogin(ObjectGuid characterGuid);

    bool Update(uint32 diff, PacketFilter& updater);
    void KickPlayer(std::string_view reason);
    void SendPacket(WorldPacket const* packet, bool forced = false) override;
};
}
#endif
