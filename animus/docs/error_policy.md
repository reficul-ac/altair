# Animus Error Policy

Animus errors should be explicit at subsystem boundaries and should preserve
enough context for debugging without coupling layers together.

## Conventions

- Tile source failures report the source identity, requested layer, and whether
  the data was missing or unreadable.
- Decode failures report the decoder/input format and reject malformed rasters
  before they reach renderer-facing code.
- Missing data is a normal tile state, not an exception path. Terrain systems
  should keep fallback coverage visible while replacement data is requested.
- Future GPU upload failures belong at the render-thread boundary and should
  not be created by worker threads.

## Phase B Scope

Phase B documents the policy and creates model/cache contracts only. Concrete
tile source, decoder, worker, renderer, and GPU error types should be added in
the phase where those boundaries are implemented.
