CREATE TABLE IF NOT EXISTS account (
    account_id BIGINT PRIMARY KEY AUTO_INCREMENT,
    account_name VARCHAR(64) NOT NULL UNIQUE,
    password_hash VARCHAR(255) NOT NULL,
    status TINYINT NOT NULL DEFAULT 1,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS player (
    player_id BIGINT PRIMARY KEY AUTO_INCREMENT,
    account_id BIGINT NOT NULL,
    name VARCHAR(64) NOT NULL,
    level INT NOT NULL DEFAULT 1,
    exp INT NOT NULL DEFAULT 0,
    last_login_at TIMESTAMP NULL DEFAULT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_player_account FOREIGN KEY (account_id) REFERENCES account(account_id)
);

CREATE TABLE IF NOT EXISTS player_asset (
    player_id BIGINT PRIMARY KEY,
    stamina INT NOT NULL DEFAULT 120,
    gold BIGINT NOT NULL DEFAULT 0,
    diamond BIGINT NOT NULL DEFAULT 0,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    CONSTRAINT fk_asset_player FOREIGN KEY (player_id) REFERENCES player(player_id)
);

CREATE TABLE IF NOT EXISTS player_item (
    id BIGINT PRIMARY KEY AUTO_INCREMENT,
    player_id BIGINT NOT NULL,
    item_id INT NOT NULL,
    item_count INT NOT NULL DEFAULT 0,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY uk_player_item (player_id, item_id),
    CONSTRAINT fk_item_player FOREIGN KEY (player_id) REFERENCES player(player_id)
);

CREATE TABLE IF NOT EXISTS player_dungeon (
    id BIGINT PRIMARY KEY AUTO_INCREMENT,
    player_id BIGINT NOT NULL,
    dungeon_id INT NOT NULL,
    best_star INT NOT NULL DEFAULT 0,
    is_first_clear TINYINT NOT NULL DEFAULT 0,
    last_clear_at TIMESTAMP NULL DEFAULT NULL,
    UNIQUE KEY uk_player_dungeon (player_id, dungeon_id),
    CONSTRAINT fk_dungeon_player FOREIGN KEY (player_id) REFERENCES player(player_id)
);

CREATE TABLE IF NOT EXISTS dungeon_battle (
    battle_id VARCHAR(64) PRIMARY KEY,
    player_id BIGINT NOT NULL,
    dungeon_id INT NOT NULL,
    status TINYINT NOT NULL DEFAULT 0,
    cost_stamina INT NOT NULL DEFAULT 0,
    enter_confirmed TINYINT NOT NULL DEFAULT 0,
    remain_stamina_after INT NOT NULL DEFAULT 0,
    settlement_recorded TINYINT NOT NULL DEFAULT 0,
    first_clear TINYINT NOT NULL DEFAULT 0,
    gold_reward BIGINT NOT NULL DEFAULT 0,
    diamond_reward BIGINT NOT NULL DEFAULT 0,
    start_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    finish_at TIMESTAMP NULL DEFAULT NULL,
    INDEX idx_player_status (player_id, status)
);

CREATE TABLE IF NOT EXISTS reward_log (
    id BIGINT PRIMARY KEY AUTO_INCREMENT,
    player_id BIGINT NOT NULL,
    battle_id VARCHAR(64) NOT NULL,
    reward_type VARCHAR(32) NOT NULL,
    reward_json JSON NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uk_reward (player_id, battle_id, reward_type)
);

CREATE TABLE IF NOT EXISTS player_action_log (
    operation_key VARCHAR(128) PRIMARY KEY,
    player_id BIGINT NOT NULL,
    operation_type VARCHAR(32) NOT NULL,
    dungeon_id INT NOT NULL DEFAULT 0,
    stamina_delta INT NOT NULL DEFAULT 0,
    remain_stamina_after INT NOT NULL DEFAULT 0,
    gold_delta BIGINT NOT NULL DEFAULT 0,
    diamond_delta BIGINT NOT NULL DEFAULT 0,
    best_star INT NOT NULL DEFAULT 0,
    first_clear TINYINT NOT NULL DEFAULT 0,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    KEY idx_player_operation_type (player_id, operation_type)
);
