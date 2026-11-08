-- Public upstream row fix (advocaite/TrinityCore PR 327). Not a world or realm repack.
-- Does not touch realm ids. Apply only after comparing these keys with the target world database.
-- Rollback: UPDATE `world_safe_locs` SET `LocZ` = 31.97 WHERE `ID` = 4 AND `LocZ` = 36.0; only after confirming ID 4 is still Sentinel Hill.
-- Sentinel Hill graveyard (Westfall): the spirit healer point sat 3.5 y under the terrain (floor 35.5, stored Z 31.97); the core lifted it on every use.
UPDATE `world_safe_locs` SET `LocZ` = 36.0 WHERE `ID` = 4 AND `LocZ` < 35;
