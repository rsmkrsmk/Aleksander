-- ============================================================================
--  Leśny Dziennik Aleksandra — schemat MySQL dla silnika danych
--  Uruchom w cPanel → phpMyAdmin na swojej bazie, gdy chcesz włączyć tryb MySQL.
--  Format kolumn odpowiada wpisom CSV (data,godzina,typ,ml,piers_lewa,piers_prawa).
-- ============================================================================

CREATE TABLE IF NOT EXISTS `entries` (
  `id`          BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `entry_date`  DATE            NOT NULL,
  `entry_time`  TIME            NOT NULL,
  `type`        VARCHAR(32)     NOT NULL,   -- KARMIENIE, MLEKO_MATKI, MLEKO_MODYFIKOWANE,
                                            -- PIELUCHA_MOKRA/BRUDNA, ODCIAGANIE, WITAMINA_D,
                                            -- WAGA (ml=gramy), SEN_START, SEN_STOP
  `ml`          INT             NOT NULL DEFAULT 0,
  `piers_left`  INT             NOT NULL DEFAULT 0,
  `piers_right` INT             NOT NULL DEFAULT 0,
  `created_at`  TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `idx_date` (`entry_date`),
  KEY `idx_date_time` (`entry_date`, `entry_time`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `settings` (
  `key`   VARCHAR(64)  NOT NULL,
  `value` VARCHAR(255) NOT NULL,
  PRIMARY KEY (`key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Opcjonalnie: pierwszy import danych z CSV zrobisz przyciskiem IMPORTUJ DANE
-- w panelu (po przełączeniu STORAGE_DRIVER na 'mysql'), albo ładując CSV w phpMyAdmin.
