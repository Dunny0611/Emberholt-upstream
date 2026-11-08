-- OPTIONAL PATCH. The ready branch does not contain this file.
-- ID 4 is Sentinel Hill in stock Trinity and vanilla data. A database that reused ID 4 for another place whose Z is below 35 will move that place.
-- Confirm MapID and Comment before applying. Realm ids are not touched.
-- Rollback, only after that confirmation: UPDATE `world_safe_locs` SET `LocZ` = 31.97 WHERE `ID` = 4 AND `LocZ` = 36.0;

-- Public upstream row fix (advocaite/TrinityCore PR 327). Not a world or realm repack.
-- Does not touch realm ids. Apply only after comparing these keys with the target world database.
-- Rollback: UPDATE `world_safe_locs` SET `LocZ` = 31.97 WHERE `ID` = 4 AND `LocZ` = 36.0; only after confirming ID 4 is still Sentinel Hill.
-- Sentinel Hill graveyard (Westfall): the spirit healer point sat 3.5 y under the terrain (floor 35.5, stored Z 31.97); the core lifted it on every use.
UPDATE `world_safe_locs` SET `LocZ` = 36.0 WHERE `ID` = 4 AND `LocZ` < 35;
