#!/usr/bin/env bash

demo_account_id="${DEMO_ACCOUNT_ID:-10001}"
demo_account_name="${DEMO_ACCOUNT_NAME:-demo}"
if [[ -n "${DEMO_PASSWORD_HASH:-}" ]]; then
  demo_password_hash="${DEMO_PASSWORD_HASH}"
else
  demo_password_hash='pbkdf2_sha256$100000$737461727465722d64656d6f2d73616c74$6f24c15d816cb5760860dcdf75b60d557913253234b3e59620bc715c864c88e2'
fi
demo_password_salt="${DEMO_PASSWORD_SALT:-starter-demo-salt}"
demo_player_id="${DEMO_PLAYER_ID:-20001}"
demo_player_name="${DEMO_PLAYER_NAME:-hero_demo}"
demo_player_level="${DEMO_PLAYER_LEVEL:-10}"
demo_player_stamina="${DEMO_PLAYER_STAMINA:-120}"
demo_player_gold="${DEMO_PLAYER_GOLD:-1000}"
demo_player_diamond="${DEMO_PLAYER_DIAMOND:-100}"
demo_player_main_stage_id="${DEMO_PLAYER_MAIN_STAGE_ID:-${DEMO_STAGE_ID:-1001}}"
demo_player_fight_power="${DEMO_PLAYER_FIGHT_POWER:-1200}"
demo_player_server_id="${DEMO_PLAYER_SERVER_ID:-1}"
player_shard="$(printf '%02x' $(( demo_player_id & 0x0f )))"

mysql --protocol=socket -uroot -p"${MYSQL_ROOT_PASSWORD}" <<SQL
INSERT INTO account_db.account (
  account_id,
  account_name,
  password_hash,
  salt,
  status,
  register_channel,
  register_time,
  last_login_time,
  created_at,
  updated_at
) VALUES (
  ${demo_account_id},
  '${demo_account_name}',
  '${demo_password_hash}',
  '${demo_password_salt}',
  1,
  'demo',
  CURRENT_TIMESTAMP(3),
  CURRENT_TIMESTAMP(3),
  CURRENT_TIMESTAMP(3),
  CURRENT_TIMESTAMP(3)
)
ON DUPLICATE KEY UPDATE
  account_name = VALUES(account_name),
  password_hash = VALUES(password_hash),
  salt = VALUES(salt),
  status = 1,
  updated_at = CURRENT_TIMESTAMP(3);

INSERT INTO player_db.player_profile_${player_shard} (
  player_id,
  account_id,
  server_id,
  nickname,
  level,
  exp,
  energy,
  stamina_recover_at,
  main_stage_id,
  fight_power,
  created_at,
  updated_at
) VALUES (
  ${demo_player_id},
  ${demo_account_id},
  ${demo_player_server_id},
  '${demo_player_name}',
  ${demo_player_level},
  0,
  ${demo_player_stamina},
  NULL,
  ${demo_player_main_stage_id},
  ${demo_player_fight_power},
  CURRENT_TIMESTAMP(3),
  CURRENT_TIMESTAMP(3)
)
ON DUPLICATE KEY UPDATE
  account_id = VALUES(account_id),
  server_id = VALUES(server_id),
  nickname = VALUES(nickname),
  level = VALUES(level),
  exp = VALUES(exp),
  energy = VALUES(energy),
  stamina_recover_at = VALUES(stamina_recover_at),
  main_stage_id = VALUES(main_stage_id),
  fight_power = VALUES(fight_power),
  updated_at = CURRENT_TIMESTAMP(3);

DELETE FROM player_db.player_currency_${player_shard}
WHERE player_id = ${demo_player_id};

INSERT INTO player_db.player_currency_${player_shard} (
  player_id,
  currency_type,
  amount,
  version,
  updated_at
) VALUES
  (${demo_player_id}, 'gold', ${demo_player_gold}, 1, CURRENT_TIMESTAMP(3)),
  (${demo_player_id}, 'diamond', ${demo_player_diamond}, 1, CURRENT_TIMESTAMP(3));

DELETE FROM player_db.player_role_${player_shard}
WHERE player_id = ${demo_player_id};

INSERT INTO player_db.player_role_${player_shard} (
  player_id,
  role_id,
  level,
  star,
  breakthrough,
  skill_state_json,
  equip_state_json,
  updated_at
) VALUES
  (${demo_player_id}, 1001, ${demo_player_level}, 1, 0, NULL, NULL, CURRENT_TIMESTAMP(3)),
  (${demo_player_id}, 1002, ${demo_player_level}, 1, 0, NULL, NULL, CURRENT_TIMESTAMP(3)),
  (${demo_player_id}, 1003, ${demo_player_level}, 1, 0, NULL, NULL, CURRENT_TIMESTAMP(3));
SQL
