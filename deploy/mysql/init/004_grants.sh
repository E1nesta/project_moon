#!/usr/bin/env bash

mysql --protocol=socket -uroot -p"${MYSQL_ROOT_PASSWORD}" <<SQL
GRANT ALL PRIVILEGES ON \`${ACCOUNT_MYSQL_DATABASE:-account_db}\`.* TO '${ACCOUNT_MYSQL_USER:-game}'@'%';
GRANT ALL PRIVILEGES ON \`${PLAYER_MYSQL_DATABASE:-player_db}\`.* TO '${PLAYER_MYSQL_USER:-game}'@'%';
GRANT ALL PRIVILEGES ON \`${BATTLE_MYSQL_DATABASE:-battle_db}\`.* TO '${BATTLE_MYSQL_USER:-game}'@'%';
FLUSH PRIVILEGES;
SQL
