#include "watcher.hpp"

#include <duckdb/main/attached_database.hpp>

#include "utils/helpers.hpp"
#include "utils/md_helpers.hpp"
#include "http_server.hpp"
#include "settings.hpp"

namespace duckdb {
namespace ui {

Watcher::Watcher(HttpServer &_server)
    : should_run(false), server(_server), watched_database(nullptr) {}

bool WasCatalogUpdated(DatabaseInstance &db, Connection &connection,
                       CatalogState &last_state) {
  bool has_change = false;
  auto &context = *connection.context;
  connection.BeginTransaction();

  const auto &databases = db.GetDatabaseManager().GetDatabases(context);
  std::set<idx_t> db_oids;

  // Check currently attached databases
  for (const auto &db_ref : databases) {
#if DUCKDB_VERSION_AT_MOST(1, 3, 2)
    auto &db_instance = db_ref.get();
#else
    auto &db_instance = *db_ref;
#endif
    if (db_instance.IsTemporary()) {
      continue; // ignore temp databases
    }

    db_oids.insert(db_instance.oid);
    auto &catalog = db_instance.GetCatalog();
    try {
      auto current_version = catalog.GetCatalogVersion(context);
      auto last_version_it =
          last_state.db_to_catalog_version.find(db_instance.oid);
      if (last_version_it == last_state.db_to_catalog_version.end() // first time
          || !(last_version_it->second == current_version)) {       // updated
        has_change = true;
        last_state.db_to_catalog_version[db_instance.oid] = current_version;
      }
    } catch (const std::exception &) {
      // Remote catalogs (e.g. quack) may not support versioning in a local
      // transaction context. The OID is already tracked above for detachment
      // detection; skip version tracking for this database.
    }
  }

  // Now check if any databases have been detached
  for (auto it = last_state.db_to_catalog_version.begin();
       it != last_state.db_to_catalog_version.end();) {
    if (db_oids.find(it->first) == db_oids.end()) {
      has_change = true;
      it = last_state.db_to_catalog_version.erase(it);
    } else {
      ++it;
    }
  }

  connection.Rollback();
  return has_change;
}

void Watcher::Watch() {
  CatalogState last_state;
  bool is_md_connected = false;
  while (should_run) {
    auto db = server.LockDatabaseInstance();
    if (!db) {
      break; // DB went away, nothing to watch
    }

    if (watched_database == nullptr) {
      watched_database = db.get();
    } else if (watched_database != db.get()) {
      break; // DB changed, stop watching, will be restarted
    }

    duckdb::Connection con{*db};
    auto polling_interval = GetPollingInterval(*con.context);
    if (polling_interval == 0) {
      return; // Disable watcher
    }

    try {
      if (WasCatalogUpdated(*db, con, last_state)) {
        server.event_dispatcher->SendCatalogChangedEvent();
      }

      if (!is_md_connected && IsMDConnected(con)) {
        is_md_connected = true;
        server.event_dispatcher->SendConnectedEvent(GetMDToken(con));
      }
    } catch (std::exception &ex) {
      // Log the error but keep the watcher running — transient failures
      // (e.g. a remote quack catalog being temporarily unreachable) should
      // not permanently break catalog change detection for local databases.
      std::cerr << "Warning in watcher (will retry): " << ex.what() << std::endl;
    }

    {
      std::unique_lock<std::mutex> lock(mutex);
      cv.wait_for(lock, std::chrono::milliseconds(polling_interval));
    }
  }
}

void Watcher::Start() {
  {
    std::lock_guard<std::mutex> guard(mutex);
    should_run = true;
  }

  if (!thread) {
    thread = make_uniq<std::thread>(&Watcher::Watch, this);
  }
}

void Watcher::Stop() {
  if (!thread) {
    return;
  }

  {
    std::lock_guard<std::mutex> guard(mutex);
    should_run = false;
  }
  cv.notify_all();
  thread->join();
  thread.reset();
}

} // namespace ui
} // namespace duckdb
