P2 identity modules:
  user_db.c        — JSON lab accounts (username, password, is_elevated)
  path_property.c  — per-directory .flmeta/properties.json (owner, requires_elevation)
  elevation.c      — P2-4 sudo elevation tokens (TTL via timekeeping)
  session.c        — login, current user, elevated-account bypass, FM sync
  Default users: flinstone/flinstone (normal), root/root (elevated).
