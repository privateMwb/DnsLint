/**
 * @file            HistoryStore.h
 *
 * @date            2026-8-29
 *
 * @version         1.0.0
 *
 * @copyright       Copyright (c) 2026 privateMwb
 *                  All rights reserved.
 *                  https://github.com/privateMwb/DnsLint
 *
 * @attention       This source is released under the MIT license
 *                  SPDX-License-Identifier: MIT
 *                  <http://opensource.org/licenses/MIT>
 */

#pragma once

// clang-format off
#include <MiniDB/Core/Database.h>       // MiniDB::Core::Database
#include <MiniDB/Core/Record.h>         // MiniDB::Core::Record
#include <MiniDB/Core/Table.h>          // MiniDB::Core::Table
#include <MiniDB/Common/Type.h>         // MiniDB::Common::Status, ColumnDef, ColumnType
#include <MiniDB/Engine/QueryEngine.h>  // MiniDB::Engine::QueryEngine (this project's own QueryEngine
                                        // is DnsCheckup::QueryEngine -- different class, same name,
                                        // disambiguated below by always spelling MiniDB's out in full)

#include <ArenaPro/Arena.h> // ArenaPro::Arena

#include <JsonPro/Json.h> // JsonPro::Json

#include <VectorPro/Vector.h> // VectorPro::Vector

#include <cstdint>  // std::uint64_t
#include <mutex>    // std::mutex, std::lock_guard
#include <string>   // std::string
// clang-format on

/**
 * @brief Persists a record of every check run, backed by MiniDB.
 * @details Design notes worth knowing before touching this file:
 *
 * - **Schema versioning**: a dedicated single-row `meta` table holds
 *   `schemaVersion`, since MiniDB itself has no built-in migration
 *   concept (confirmed by reading its source: `Database`/`Table` have
 *   no version field or migration hook). `open()` compares the stored
 *   version to `kCurrentSchemaVersion` and runs `runMigrations()`
 *   before anything else touches the database.
 *
 * - **Additive nullable columns need no migration code at all.** A
 *   `Record`'s fields live in a free-form `Json` object
 *   (`MiniDB::Core::Record::data`), and `Record::validate()` (verified
 *   by reading its actual implementation, not just its doc comment)
 *   only iterates the table's *declared* schema columns checking
 *   presence/type -- it never inspects what's actually in `data`, so
 *   an undeclared field is invisible to validation entirely. This
 *   store uses that deliberately: `check_runs`' schema only declares
 *   `domain` and `checkedAt` (both required, both validated); the
 *   `results` field -- the actual nested Json array of check outcomes
 *   -- is attached without being declared in the schema at all, so it
 *   needs no stringify/parse round-trip and no schema bump to exist.
 *   A genuine migration is only needed for a *non-nullable* column
 *   addition, a rename, a type change, or a structural table split --
 *   none of which this release needs (`kCurrentSchemaVersion == 1`,
 *   nothing to migrate from). See `runMigrations()` for exactly how to
 *   add one when that day comes.
 *
 * - **Not thread-safe on its own** -- guarded by `mutex_` internally
 *   on every public call, since (unlike `QueryEngine`, currently only
 *   ever called from one route) this can legitimately be hit by
 *   concurrent request threads under FalconHTTP's thread-per-
 *   connection model.
 */

namespace DnsCheckup {

/// @brief One past check run, as returned by `HistoryStore::getHistory()`.
struct CheckRun {
    std::string domain;
    std::uint64_t checkedAtEpochSeconds;
    JsonPro::Json results; ///< The same JSON array shape `POST /api/check`'s
                           ///< response body's "checks" field returns.
};

class HistoryStore {
  public:
    /// @brief Schema version this build of DnsLint expects. Bump this,
    /// and add a case to `runMigrations()`, whenever `check_runs`'
    /// declared schema changes in a way `Record::validate()`'s
    /// undeclared-field passthrough (see class docs) can't absorb for
    /// free.
    static constexpr std::uint64_t kCurrentSchemaVersion = 1;

    /**
     * @brief Opens (or creates) the database at `path`.
     * @param path Destination file path, e.g. "data/dnslint.json".
     * @return `Status::OK` on success. On any failure, this
     * `HistoryStore` is left with no tables and every subsequent call
     * returns `Status::TABLE_NOT_FOUND` rather than crashing -- a
     * history-store failure should degrade the app (no history saved
     * or served), not take down `POST /api/check` itself.
     */
    [[nodiscard]] MiniDB::Common::Status open(const std::string& path);

    /**
     * @brief Records one check run.
     * @param domain Normalized domain the checks ran against (see
     * `CheckRoutes.h::normalizeDomain()`).
     * @param checkedAtEpochSeconds Unix timestamp the run completed at.
     * @param results The same Json array `POST /api/check`'s response
     * builds -- attached to the record without being schema-declared
     * (see class docs).
     * @return `Status::OK` on success.
     */
    [[nodiscard]] MiniDB::Common::Status recordCheck(const std::string& domain,
                                                     std::uint64_t checkedAtEpochSeconds,
                                                     const JsonPro::Json& results);

    /**
     * @brief Returns the most recent runs for `domain`, newest first.
     * @param domain Domain to look up (exact match against what was
     * passed to `recordCheck()` -- callers should normalize first).
     * @param limit Maximum number of runs to return.
     * @return Matching runs, newest first. Empty if none found or the
     * store failed to open.
     */
    [[nodiscard]] VectorPro::Vector<CheckRun> getHistory(const std::string& domain,
                                                         std::size_t limit);

  private:
    MiniDB::Core::Database db_{"dnslint"};
    std::string path_;
    bool ready_ = false;
    std::uint64_t nextRunId_ = 0;
    std::mutex mutex_;

    /// @brief Runs any migrations needed to bring a freshly-loaded
    /// database from `storedVersion` up to `kCurrentSchemaVersion`.
    /// @details Empty today -- kCurrentSchemaVersion is 1 and there is
    /// no version 0 to migrate from. To add one later: bump
    /// `kCurrentSchemaVersion`, then add `if (storedVersion < N) { ... }`
    /// here, each block transforming `db_`'s already-loaded in-memory
    /// tables (e.g. `db_.getTable("check_runs")`, walk every record via
    /// `MiniDB::Engine::QueryEngine::select()` with empty predicates,
    /// `updateRecord()` each with the new field populated) before
    /// `open()` saves the migrated result back out.
    void runMigrations(std::uint64_t storedVersion);

    /// @brief Creates `check_runs` and `meta` fresh, for a first-ever run.
    [[nodiscard]] MiniDB::Common::Status createSchema();

    /// @brief Scans `check_runs` for the highest existing record id, so
    /// `recordCheck()` never reuses an id already on disk after a restart.
    void resyncNextRunId();
};

} // namespace DnsCheckup
