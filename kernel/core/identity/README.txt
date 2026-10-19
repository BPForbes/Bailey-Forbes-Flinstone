P2 identity modules:
  user_db.c        — SQLite accounts (password_hash + salt_hex, heritage key_id, created_by, created_at)
  heritage.c       — heritage key ids (contracts/identity/contract_p2_heritage.h): validate, derive, compare
  bridge.c         — bridges between accounts on different branches (table "bridges", same SQLite file)
  path_property.c  — per-directory .flmeta/properties.json (owner, requires_elevation)
  elevation.c      — P2-4 sudo elevation tokens (TTL via timekeeping)
  session.c        — login, sudo scope, jail vs path elevation split, current key id
  Lab accounts (only with FL_USERS_LAB_DEFAULTS=1): Root/root (elevated, key Root),
  Flinstone/flinstone (key Root.Flinstone, the default user), Alternative/alternative
  (key Root.Alternative). Names match case-insensitively, so "login root" still works.
  A new account's key id is its creator's key id plus its name: Flinstone creating Bob
  gives Root.Flinstone.Bob. Stores from before heritage are migrated on load (root -> Root,
  flinstone -> Flinstone, every other account directly under Root).
  DB path: userland/shell/fl_users.db (override with FL_USERS_DB_PATH for tests).
  Model, jail homes and bridges: docs/IDENTITY_HERITAGE.md.
