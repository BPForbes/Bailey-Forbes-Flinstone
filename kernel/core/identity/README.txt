P2 identity modules:
  user_db.c        — JSON lab user database (users.lab.json)
  path_property.c  — per-directory .flmeta/properties.json (owner, requires_elevation)
  elevation.c      — P2-4 elevation tokens (TTL via timekeeping)
  session.c        — current user + elevation session state
