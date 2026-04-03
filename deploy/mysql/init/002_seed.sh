#!/usr/bin/env bash
set -euo pipefail

mysql --protocol=socket -uroot -p"${MYSQL_ROOT_PASSWORD}" "${MYSQL_DATABASE}" <<SQL
INSERT INTO account (account_id, account_name, password_hash)
VALUES (${DEMO_ACCOUNT_ID:-10001}, '${DEMO_ACCOUNT_NAME:-demo}', '${DEMO_PASSWORD_HASH:-}')
ON DUPLICATE KEY UPDATE
  account_name = VALUES(account_name),
  password_hash = VALUES(password_hash);

INSERT INTO player (player_id, account_id, name, level, exp, last_login_at)
VALUES (${DEMO_PLAYER_ID:-20001}, ${DEMO_ACCOUNT_ID:-10001}, '${DEMO_PLAYER_NAME:-hero_demo}', ${DEMO_PLAYER_LEVEL:-10}, 0, NOW())
ON DUPLICATE KEY UPDATE
  account_id = VALUES(account_id),
  name = VALUES(name),
  level = VALUES(level),
  exp = VALUES(exp),
  last_login_at = VALUES(last_login_at);

INSERT INTO player_asset (player_id, stamina, gold, diamond)
VALUES (${DEMO_PLAYER_ID:-20001}, ${DEMO_PLAYER_STAMINA:-120}, ${DEMO_PLAYER_GOLD:-1000}, ${DEMO_PLAYER_DIAMOND:-100})
ON DUPLICATE KEY UPDATE
  stamina = VALUES(stamina),
  gold = VALUES(gold),
  diamond = VALUES(diamond);
SQL
