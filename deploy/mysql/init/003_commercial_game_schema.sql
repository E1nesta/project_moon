-- Commercial raising-game backend target schema.
-- Logical databases:
--   account_db
--   player_db
--   battle_db

CREATE DATABASE IF NOT EXISTS account_db
  DEFAULT CHARACTER SET utf8mb4
  DEFAULT COLLATE utf8mb4_0900_ai_ci;

CREATE DATABASE IF NOT EXISTS player_db
  DEFAULT CHARACTER SET utf8mb4
  DEFAULT COLLATE utf8mb4_0900_ai_ci;

CREATE DATABASE IF NOT EXISTS battle_db
  DEFAULT CHARACTER SET utf8mb4
  DEFAULT COLLATE utf8mb4_0900_ai_ci;

USE account_db;

CREATE TABLE IF NOT EXISTS account (
    account_id BIGINT PRIMARY KEY,
    account_name VARCHAR(64) NOT NULL,
    password_hash VARCHAR(128) NOT NULL,
    salt VARCHAR(64) NOT NULL,
    status TINYINT NOT NULL DEFAULT 1,
    register_channel VARCHAR(32) NOT NULL,
    register_time DATETIME(3) NOT NULL,
    last_login_time DATETIME(3) NULL,
    created_at DATETIME(3) NOT NULL,
    updated_at DATETIME(3) NOT NULL,
    UNIQUE KEY uk_account_name (account_name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS account_bind (
    bind_id BIGINT PRIMARY KEY,
    account_id BIGINT NOT NULL,
    bind_type VARCHAR(16) NOT NULL,
    bind_value VARCHAR(128) NOT NULL,
    verified TINYINT NOT NULL DEFAULT 0,
    created_at DATETIME(3) NOT NULL,
    updated_at DATETIME(3) NOT NULL,
    UNIQUE KEY uk_bind (bind_type, bind_value),
    KEY idx_account_id (account_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS realname_state (
    account_id BIGINT PRIMARY KEY,
    realname_status TINYINT NOT NULL,
    realname_name_cipher VARBINARY(256) NULL,
    id_no_cipher VARBINARY(512) NULL,
    vendor VARCHAR(32) NULL,
    minor_flag TINYINT NOT NULL DEFAULT 0,
    verified_at DATETIME(3) NULL,
    updated_at DATETIME(3) NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS device_fingerprint (
    fingerprint_id BIGINT PRIMARY KEY,
    account_id BIGINT NOT NULL,
    device_hash CHAR(64) NOT NULL,
    platform VARCHAR(16) NOT NULL,
    device_model VARCHAR(64) NULL,
    os_type VARCHAR(32) NULL,
    os_version VARCHAR(32) NULL,
    cpu_info VARCHAR(128) NULL,
    memory_mb INT NULL,
    screen_resolution VARCHAR(32) NULL,
    ip VARBINARY(16) NULL,
    first_seen_at DATETIME(3) NOT NULL,
    last_seen_at DATETIME(3) NOT NULL,
    KEY idx_account_last_seen (account_id, last_seen_at),
    KEY idx_device_hash (device_hash)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS login_audit (
    audit_id BIGINT PRIMARY KEY,
    account_id BIGINT NOT NULL,
    login_time DATETIME(3) NOT NULL,
    login_result TINYINT NOT NULL,
    channel VARCHAR(32) NOT NULL,
    ip VARBINARY(16) NULL,
    device_hash CHAR(64) NULL,
    risk_score INT NOT NULL DEFAULT 0,
    risk_reason_digest VARCHAR(128) NULL,
    created_at DATETIME(3) NOT NULL,
    KEY idx_account_time (account_id, login_time),
    KEY idx_device_time (device_hash, login_time)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS risk_tag (
    account_id BIGINT NOT NULL,
    tag_code VARCHAR(32) NOT NULL,
    score INT NOT NULL DEFAULT 0,
    source VARCHAR(32) NOT NULL,
    expire_at DATETIME(3) NULL,
    updated_at DATETIME(3) NOT NULL,
    PRIMARY KEY (account_id, tag_code)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS ban_record (
    ban_id BIGINT PRIMARY KEY,
    account_id BIGINT NOT NULL,
    ban_scope VARCHAR(32) NOT NULL,
    reason_code VARCHAR(32) NOT NULL,
    start_time DATETIME(3) NOT NULL,
    end_time DATETIME(3) NULL,
    operator VARCHAR(64) NOT NULL,
    evidence_ref VARCHAR(128) NULL,
    created_at DATETIME(3) NOT NULL,
    KEY idx_account_scope (account_id, ban_scope)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS account_outbox (
    event_id BIGINT PRIMARY KEY,
    aggregate_type VARCHAR(32) NOT NULL,
    aggregate_id VARCHAR(64) NOT NULL,
    event_type VARCHAR(64) NOT NULL,
    payload_json JSON NOT NULL,
    idempotency_key VARCHAR(64) NOT NULL,
    trace_id VARCHAR(64) NOT NULL,
    publish_status TINYINT NOT NULL DEFAULT 0,
    retry_count INT NOT NULL DEFAULT 0,
    next_retry_at DATETIME(3) NULL,
    published_at DATETIME(3) NULL,
    created_at DATETIME(3) NOT NULL,
    updated_at DATETIME(3) NOT NULL,
    UNIQUE KEY uk_idempotency (idempotency_key),
    KEY idx_publish_status (publish_status, next_retry_at),
    KEY idx_aggregate (aggregate_type, aggregate_id, created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

USE player_db;

DELIMITER $$

DROP PROCEDURE IF EXISTS create_player_shard_tables $$
CREATE PROCEDURE create_player_shard_tables()
BEGIN
    DECLARE shard_no INT DEFAULT 0;
    DECLARE shard_suffix CHAR(2);

    WHILE shard_no < 16 DO
        SET shard_suffix = LPAD(LOWER(HEX(shard_no)), 2, '0');

        SET @sql = CONCAT(
            'CREATE TABLE IF NOT EXISTS `player_profile_', shard_suffix, '` (',
            'player_id BIGINT PRIMARY KEY,',
            'account_id BIGINT NOT NULL,',
            'server_id INT NOT NULL,',
            'nickname VARCHAR(64) NOT NULL,',
            'level INT NOT NULL DEFAULT 1,',
            'exp BIGINT NOT NULL DEFAULT 0,',
            'energy INT NOT NULL DEFAULT 0,',
            'stamina_recover_at DATETIME(3) NULL,',
            'main_progress INT NOT NULL DEFAULT 0,',
            'fight_power BIGINT NOT NULL DEFAULT 0,',
            'created_at DATETIME(3) NOT NULL,',
            'updated_at DATETIME(3) NOT NULL,',
            'UNIQUE KEY uk_server_nick (server_id, nickname),',
            'KEY idx_account_id (account_id)',
            ') ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci'
        );
        PREPARE stmt FROM @sql;
        EXECUTE stmt;
        DEALLOCATE PREPARE stmt;

        SET @sql = CONCAT(
            'CREATE TABLE IF NOT EXISTS `player_currency_', shard_suffix, '` (',
            'player_id BIGINT NOT NULL,',
            'currency_type VARCHAR(32) NOT NULL,',
            'amount BIGINT NOT NULL DEFAULT 0,',
            'version BIGINT NOT NULL DEFAULT 0,',
            'updated_at DATETIME(3) NOT NULL,',
            'PRIMARY KEY (player_id, currency_type)',
            ') ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci'
        );
        PREPARE stmt FROM @sql;
        EXECUTE stmt;
        DEALLOCATE PREPARE stmt;

        SET @sql = CONCAT(
            'CREATE TABLE IF NOT EXISTS `currency_txn_', shard_suffix, '` (',
            'txn_id BIGINT PRIMARY KEY,',
            'player_id BIGINT NOT NULL,',
            'currency_type VARCHAR(32) NOT NULL,',
            'delta_amount BIGINT NOT NULL,',
            'before_amount BIGINT NOT NULL,',
            'after_amount BIGINT NOT NULL,',
            'reason_code VARCHAR(32) NOT NULL,',
            'ref_type VARCHAR(32) NULL,',
            'ref_id VARCHAR(64) NULL,',
            'idempotency_key VARCHAR(64) NOT NULL,',
            'created_at DATETIME(3) NOT NULL,',
            'UNIQUE KEY uk_idempotency (idempotency_key, currency_type),',
            'KEY idx_player_time (player_id, created_at),',
            'KEY idx_player_reason (player_id, reason_code)',
            ') ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci'
        );
        PREPARE stmt FROM @sql;
        EXECUTE stmt;
        DEALLOCATE PREPARE stmt;

        SET @sql = CONCAT(
            'CREATE TABLE IF NOT EXISTS `player_item_', shard_suffix, '` (',
            'player_id BIGINT NOT NULL,',
            'item_uid BIGINT NOT NULL,',
            'item_id INT NOT NULL,',
            'item_count BIGINT NOT NULL DEFAULT 0,',
            'bind_flag TINYINT NOT NULL DEFAULT 0,',
            'expire_at DATETIME(3) NULL,',
            'ext_json JSON NULL,',
            'updated_at DATETIME(3) NOT NULL,',
            'PRIMARY KEY (player_id, item_uid),',
            'KEY idx_player_itemid (player_id, item_id)',
            ') ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci'
        );
        PREPARE stmt FROM @sql;
        EXECUTE stmt;
        DEALLOCATE PREPARE stmt;

        SET @sql = CONCAT(
            'CREATE TABLE IF NOT EXISTS `item_txn_', shard_suffix, '` (',
            'txn_id BIGINT PRIMARY KEY,',
            'player_id BIGINT NOT NULL,',
            'item_uid BIGINT NULL,',
            'item_id INT NOT NULL,',
            'delta_count BIGINT NOT NULL,',
            'before_count BIGINT NOT NULL,',
            'after_count BIGINT NOT NULL,',
            'reason_code VARCHAR(32) NOT NULL,',
            'ref_type VARCHAR(32) NULL,',
            'ref_id VARCHAR(64) NULL,',
            'idempotency_key VARCHAR(64) NOT NULL,',
            'created_at DATETIME(3) NOT NULL,',
            'UNIQUE KEY uk_idempotency (idempotency_key),',
            'KEY idx_player_time (player_id, created_at)',
            ') ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci'
        );
        PREPARE stmt FROM @sql;
        EXECUTE stmt;
        DEALLOCATE PREPARE stmt;

        SET @sql = CONCAT(
            'CREATE TABLE IF NOT EXISTS `player_role_', shard_suffix, '` (',
            'player_id BIGINT NOT NULL,',
            'role_id INT NOT NULL,',
            'level INT NOT NULL DEFAULT 1,',
            'star INT NOT NULL DEFAULT 1,',
            'breakthrough INT NOT NULL DEFAULT 0,',
            'skill_state_json JSON NULL,',
            'equip_state_json JSON NULL,',
            'updated_at DATETIME(3) NOT NULL,',
            'PRIMARY KEY (player_id, role_id)',
            ') ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci'
        );
        PREPARE stmt FROM @sql;
        EXECUTE stmt;
        DEALLOCATE PREPARE stmt;

        SET @sql = CONCAT(
            'CREATE TABLE IF NOT EXISTS `player_loadout_', shard_suffix, '` (',
            'player_id BIGINT NOT NULL,',
            'loadout_id INT NOT NULL,',
            'name VARCHAR(64) NOT NULL,',
            'slot1_role_id INT NULL,',
            'slot2_role_id INT NULL,',
            'slot3_role_id INT NULL,',
            'updated_at DATETIME(3) NOT NULL,',
            'PRIMARY KEY (player_id, loadout_id)',
            ') ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci'
        );
        PREPARE stmt FROM @sql;
        EXECUTE stmt;
        DEALLOCATE PREPARE stmt;

        SET @sql = CONCAT(
            'CREATE TABLE IF NOT EXISTS `task_progress_', shard_suffix, '` (',
            'player_id BIGINT NOT NULL,',
            'task_id BIGINT NOT NULL,',
            'progress_value BIGINT NOT NULL DEFAULT 0,',
            'status TINYINT NOT NULL DEFAULT 0,',
            'updated_at DATETIME(3) NOT NULL,',
            'PRIMARY KEY (player_id, task_id)',
            ') ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci'
        );
        PREPARE stmt FROM @sql;
        EXECUTE stmt;
        DEALLOCATE PREPARE stmt;

        SET @sql = CONCAT(
            'CREATE TABLE IF NOT EXISTS `player_mail_', shard_suffix, '` (',
            'mail_id BIGINT PRIMARY KEY,',
            'player_id BIGINT NOT NULL,',
            'mail_type TINYINT NOT NULL,',
            'title VARCHAR(128) NOT NULL,',
            'content TEXT NOT NULL,',
            'reward_json JSON NULL,',
            'status TINYINT NOT NULL DEFAULT 0,',
            'expire_at DATETIME(3) NULL,',
            'created_at DATETIME(3) NOT NULL,',
            'KEY idx_player_status (player_id, status),',
            'KEY idx_player_expire (player_id, expire_at)',
            ') ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci'
        );
        PREPARE stmt FROM @sql;
        EXECUTE stmt;
        DEALLOCATE PREPARE stmt;

        SET @sql = CONCAT(
            'CREATE TABLE IF NOT EXISTS `activity_progress_', shard_suffix, '` (',
            'player_id BIGINT NOT NULL,',
            'activity_id BIGINT NOT NULL,',
            'score BIGINT NOT NULL DEFAULT 0,',
            'progress_json JSON NULL,',
            'status TINYINT NOT NULL DEFAULT 0,',
            'updated_at DATETIME(3) NOT NULL,',
            'PRIMARY KEY (player_id, activity_id)',
            ') ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci'
        );
        PREPARE stmt FROM @sql;
        EXECUTE stmt;
        DEALLOCATE PREPARE stmt;

        SET @sql = CONCAT(
            'CREATE TABLE IF NOT EXISTS `player_outbox_', shard_suffix, '` (',
            'event_id BIGINT PRIMARY KEY,',
            'player_id BIGINT NOT NULL,',
            'aggregate_type VARCHAR(32) NOT NULL,',
            'aggregate_id VARCHAR(64) NOT NULL,',
            'event_type VARCHAR(64) NOT NULL,',
            'payload_json JSON NOT NULL,',
            'idempotency_key VARCHAR(64) NOT NULL,',
            'trace_id VARCHAR(64) NOT NULL,',
            'publish_status TINYINT NOT NULL DEFAULT 0,',
            'retry_count INT NOT NULL DEFAULT 0,',
            'next_retry_at DATETIME(3) NULL,',
            'published_at DATETIME(3) NULL,',
            'created_at DATETIME(3) NOT NULL,',
            'updated_at DATETIME(3) NOT NULL,',
            'UNIQUE KEY uk_idempotency (idempotency_key),',
            'KEY idx_player_status (player_id, publish_status, next_retry_at)',
            ') ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci'
        );
        PREPARE stmt FROM @sql;
        EXECUTE stmt;
        DEALLOCATE PREPARE stmt;

        SET shard_no = shard_no + 1;
    END WHILE;
END $$

CALL create_player_shard_tables() $$
DROP PROCEDURE create_player_shard_tables $$

DELIMITER ;

USE battle_db;

CREATE TABLE IF NOT EXISTS battle_month_registry (
    archive_month CHAR(6) PRIMARY KEY,
    created_at DATETIME(3) NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

DELIMITER $$

DROP PROCEDURE IF EXISTS create_battle_month_tables $$
CREATE PROCEDURE create_battle_month_tables(IN month_suffix CHAR(6))
BEGIN
    SET @sql = CONCAT(
        'CREATE TABLE IF NOT EXISTS `battle_session_', month_suffix, '` (',
        'session_id BIGINT PRIMARY KEY,',
        'player_id BIGINT NOT NULL,',
        'stage_id INT NOT NULL,',
        'mode VARCHAR(16) NOT NULL,',
        'client_version VARCHAR(32) NOT NULL,',
        'team_hash CHAR(64) NOT NULL,',
        'seed BIGINT NOT NULL,',
        'cost_energy INT NOT NULL DEFAULT 0,',
        'remain_energy_after INT NOT NULL DEFAULT 0,',
        'start_time DATETIME(3) NOT NULL,',
        'end_time DATETIME(3) NULL,',
        'status TINYINT NOT NULL DEFAULT 0,',
        'KEY idx_player_time (player_id, start_time),',
        'KEY idx_stage_time (stage_id, start_time)',
        ') ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci'
    );
    PREPARE stmt FROM @sql;
    EXECUTE stmt;
    DEALLOCATE PREPARE stmt;

    SET @sql = CONCAT(
        'CREATE TABLE IF NOT EXISTS `battle_team_snapshot_', month_suffix, '` (',
        'session_id BIGINT NOT NULL,',
        'slot_no TINYINT NOT NULL,',
        'role_id INT NOT NULL,',
        'role_level INT NOT NULL,',
        'equip_digest CHAR(64) NULL,',
        'attr_digest CHAR(64) NULL,',
        'PRIMARY KEY (session_id, slot_no)',
        ') ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci'
    );
    PREPARE stmt FROM @sql;
    EXECUTE stmt;
    DEALLOCATE PREPARE stmt;

    SET @sql = CONCAT(
        'CREATE TABLE IF NOT EXISTS `battle_result_', month_suffix, '` (',
        'session_id BIGINT PRIMARY KEY,',
        'player_id BIGINT NOT NULL,',
        'stage_id INT NOT NULL,',
        'result_code TINYINT NOT NULL,',
        'star TINYINT NOT NULL DEFAULT 0,',
        'cost_time_ms BIGINT NOT NULL DEFAULT 0,',
        'client_score BIGINT NULL,',
        'server_score BIGINT NULL,',
        'damage_digest CHAR(64) NULL,',
        'hit_digest CHAR(64) NULL,',
        'finish_time DATETIME(3) NOT NULL,',
        'KEY idx_player_finish (player_id, finish_time)',
        ') ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci'
    );
    PREPARE stmt FROM @sql;
    EXECUTE stmt;
    DEALLOCATE PREPARE stmt;

    SET @sql = CONCAT(
        'CREATE TABLE IF NOT EXISTS `anti_cheat_signal_', month_suffix, '` (',
        'signal_id BIGINT PRIMARY KEY,',
        'session_id BIGINT NOT NULL,',
        'player_id BIGINT NOT NULL,',
        'signal_type VARCHAR(32) NOT NULL,',
        'signal_value VARCHAR(128) NOT NULL,',
        'threshold_value VARCHAR(128) NULL,',
        'risk_level TINYINT NOT NULL DEFAULT 0,',
        'captured_at DATETIME(3) NOT NULL,',
        'KEY idx_player_time (player_id, captured_at),',
        'KEY idx_session_id (session_id),',
        'KEY idx_type_time (signal_type, captured_at)',
        ') ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci'
    );
    PREPARE stmt FROM @sql;
    EXECUTE stmt;
    DEALLOCATE PREPARE stmt;

    SET @sql = CONCAT(
        'CREATE TABLE IF NOT EXISTS `reward_grant_', month_suffix, '` (',
        'grant_id BIGINT PRIMARY KEY,',
        'session_id BIGINT NOT NULL,',
        'player_id BIGINT NOT NULL,',
        'reward_json JSON NOT NULL,',
        'grant_status TINYINT NOT NULL DEFAULT 0,',
        'idempotency_key VARCHAR(64) NOT NULL,',
        'granted_at DATETIME(3) NULL,',
        'UNIQUE KEY uk_idempotency (idempotency_key),',
        'KEY idx_player_status (player_id, grant_status),',
        'KEY idx_session_id (session_id)',
        ') ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci'
    );
    PREPARE stmt FROM @sql;
    EXECUTE stmt;
    DEALLOCATE PREPARE stmt;

    SET @sql = CONCAT(
        'CREATE TABLE IF NOT EXISTS `battle_crash_report_', month_suffix, '` (',
        'report_id BIGINT PRIMARY KEY,',
        'session_id BIGINT NOT NULL,',
        'player_id BIGINT NOT NULL,',
        'crash_type VARCHAR(32) NOT NULL,',
        'client_version VARCHAR(32) NOT NULL,',
        'device_hash CHAR(64) NULL,',
        'stack_digest CHAR(64) NULL,',
        'reported_at DATETIME(3) NOT NULL,',
        'KEY idx_session_id (session_id),',
        'KEY idx_player_time (player_id, reported_at)',
        ') ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci'
    );
    PREPARE stmt FROM @sql;
    EXECUTE stmt;
    DEALLOCATE PREPARE stmt;

    INSERT INTO battle_month_registry (archive_month, created_at)
    VALUES (month_suffix, CURRENT_TIMESTAMP(3))
    ON DUPLICATE KEY UPDATE created_at = created_at;
END $$

CALL create_battle_month_tables(DATE_FORMAT(CURRENT_DATE(), '%Y%m')) $$
CALL create_battle_month_tables(DATE_FORMAT(DATE_ADD(CURRENT_DATE(), INTERVAL 1 MONTH), '%Y%m')) $$
DROP PROCEDURE create_battle_month_tables $$

DELIMITER ;
