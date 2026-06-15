# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Architecture

This repository is a DuckDB extension that embeds a browser-based UI. It has two main parts:

1. **C++ Extension** (`src/`) — Starts an embedded HTTP server (`http_server.cpp`) that:
   - Serves UI assets proxied from a remote origin (default: `https://ui.duckdb.org`, configurable)
   - Exposes SQL execution endpoints: `POST /ddb/run`, `POST /ddb/interrupt`, `POST /ddb/tokenize`
   - Exposes info endpoints: `GET /info`, `GET /localEvents` (SSE for catalog updates), `GET /localToken`

2. **TypeScript Workspace** (`ts/`) — A pnpm workspace with four packages under `ts/pkgs/`:
   - `@duckdb/ui-client` — HTTP client for the extension server (`DuckDBUIClient`, `DuckDBUIClientConnection`)
   - `@duckdb/data-reader` — Utilities for reading tabular data returned by DuckDB
   - `@duckdb/data-types` — DuckDB type representations
   - `@duckdb/data-values` — DuckDB value representations

The C++ build uses the [DuckDB extension template](https://github.com/duckdb/extension-template) via `extension-ci-tools/` (included as a submodule).

## C++ Build & Test

```sh
make              # release build — produces build/release/duckdb, build/release/test/unittest, build/release/extension/ui/ui.duckdb_extension
make debug        # debug build
make test         # run SQLLogicTests (release)
make test_debug   # run SQLLogicTests (debug)
```

Run the shell with UI loaded:
```sh
./build/release/duckdb -ui
# or from within duckdb: call start_ui();
```

SQL tests live in `test/sql/ui.test` (SQLLogicTest format).

## TypeScript Build & Test

All TypeScript commands run from within `ts/` using **pnpm** (not npm or yarn).

```sh
# From ts/
pnpm install      # install deps and build src for all packages
pnpm build        # build all packages (src + test)
pnpm check        # check formatting and lint across all packages
pnpm test         # run tests for all packages

# From a specific package directory (e.g. ts/pkgs/duckdb-ui-client/)
pnpm build:watch  # rebuild on file change
pnpm test:watch   # rerun tests on file change
pnpm format:write # auto-fix formatting
pnpm lint         # lint only
```

Tests use [vitest](https://vitest.dev/) (some packages use browser mode with Chrome).

## Updating DuckDB Version

When bumping DuckDB:
- Update `./duckdb` submodule to the new tagged release
- Update `./extension-ci-tools` to the matching versioned branch (e.g. `v1.5.0`)
- Update `duckdb_version` inputs in `.github/workflows/MainDistributionPipeline.yml`

See `docs/UPDATING.md` for details on handling C++ API breakage.
