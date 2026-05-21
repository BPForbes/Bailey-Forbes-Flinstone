P2 identity modules:
  user_db.c        — SQLite accounts (password_hash + salt_hex columns)
  path_property.c  — per-directory .flmeta/properties.json (owner, requires_elevation)
  elevation.c      — P2-4 sudo elevation tokens (TTL via timekeeping)
  session.c        — login, sudo scope, jail vs path elevation split
  Default users: flinstone/flinstone (normal), root/root (elevated).
  DB path: userland/shell/fl_users.db (override with FL_USERS_DB_PATH for tests).
