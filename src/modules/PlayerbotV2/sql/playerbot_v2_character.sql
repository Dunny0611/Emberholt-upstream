-- Manual character-database table for the experimental Playerbot V2 factory.
-- Do not copy this file into sql/custom. The worldserver updater applies that
-- directory on its own.
--
-- guid is the character. account, faction, factory_version, and start_level
-- are the origin. Current level is not stored here and is not rewritten.
-- An old two-column table does not match this insert. Add the columns below
-- before using the new factory. Do not UPDATE old rows to factory_version 1;
-- a row that did not record a level-1 factory start stays unbound.

CREATE TABLE IF NOT EXISTS `playerbot_v2_character` (
  `guid` bigint unsigned NOT NULL,
  `account` int unsigned NOT NULL,
  `faction` tinyint unsigned NOT NULL,
  `factory_version` int unsigned NOT NULL,
  `start_level` tinyint unsigned NOT NULL,
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ALTER TABLE `playerbot_v2_character`
--   ADD COLUMN `faction` tinyint unsigned NOT NULL DEFAULT 255,
--   ADD COLUMN `factory_version` int unsigned NOT NULL DEFAULT 0,
--   ADD COLUMN `start_level` tinyint unsigned NOT NULL DEFAULT 0;
