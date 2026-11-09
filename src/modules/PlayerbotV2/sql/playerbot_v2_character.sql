-- Manual character-database table for the experimental Playerbot V2 factory.
-- Do not copy this file into sql/custom. The worldserver updater applies that
-- directory on its own, and this table must not be created on a live database
-- unless the factory is intentionally being turned on.
--
-- The factory inserts one row in the same transaction as the new character.
-- Login refuses any guid that is not in this table, so an existing character
-- is not loaded by the bot session.

CREATE TABLE IF NOT EXISTS `playerbot_v2_character` (
  `guid` bigint unsigned NOT NULL,
  `account` int unsigned NOT NULL,
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
