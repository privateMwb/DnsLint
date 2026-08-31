/**
 * @file HistoryStore.cpp
 * @brief HistoryStore implementation.
 *
 * Contains the implementation of HistoryStore member functions and
 * internal implementation details.
 */

// ============================================================
// Implementation for DnsCheckup::HistoryStore.
// ============================================================
//
//  Sections:
//   1. Lifecycle
//   2. Migrations
//   3. Record Bookkeeping
//   4. Writes
//   5. Reads
//
// ============================================================

// clang-format off
#include <database/HistoryStore.h> // HistoryStore (own header)

#include <ArenaPro/Arena.h> // ArenaPro::Arena
#include <ArenaPro/ArenaScope.h> // ArenaPro::ArenaScope

#include <MiniDB/Common/Type.h> // MiniDB::Common::DBConstants

#include <algorithm> // std::max
#include <filesystem> // std::filesystem::exists
#include <span> // std::span
// clang-format on

namespace DnsCheckup {

using MiniDB::Common::ColumnDef;
using MiniDB::Common::ColumnType;
using MiniDB::Common::Op;
using MiniDB::Common::SortOrder;
using MiniDB::Common::Status;
using MiniDB::Core::Record;
using MiniDB::Core::Table;
using MiniDB::Engine::FilterPredicate;
using MiniDB::Engine::SortCondition;
namespace DBConstants = MiniDB::Common::DBConstants;

namespace {
constexpr const char* kCheckRunsTable = "check_runs";
constexpr const char* kMetaTable = "meta";
constexpr std::uint64_t kMetaRecordId = 0;
} // namespace

// ============================================================
//  Section 1 — Lifecycle
// ============================================================

Status HistoryStore::open(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    path_ = path;

    if (!std::filesystem::exists(path_)) {
        Status s = createSchema();
        if (s != Status::OK)
            return s;

        s = db_.save(path_);
        if (s != Status::OK)
            return s;

        ready_ = true;
        return Status::OK;
    }

    Status loadStatus = db_.load(path_);
    if (loadStatus != Status::OK)
        return loadStatus; // Genuine failure (corrupt/unreadable file) -- ready_ stays false.

    Table* meta = db_.getTable(kMetaTable);
    if (meta == nullptr)
        return Status::TABLE_NOT_FOUND; // Unexpected: a file this store wrote always has `meta`.

    Record metaRecord;
    Status s = meta->getRecord(kMetaRecordId, metaRecord);
    if (s != Status::OK)
        return s;

    auto storedVersion =
        static_cast<std::uint64_t>(metaRecord.getField("schemaVersion").asNumber());
    if (storedVersion < kCurrentSchemaVersion) {
        runMigrations(storedVersion);
        // runMigrations() mutates db_'s in-memory tables directly; persist
        // the migrated result so this doesn't re-run on every future boot.
        s = db_.save(path_);
        if (s != Status::OK)
            return s;
    }

    ready_ = true;
    resyncNextRunId();
    return Status::OK;
}

Status HistoryStore::createSchema() {
    Status s = db_.createTable(kCheckRunsTable, VectorPro::Vector<ColumnDef>{
                                                    ColumnDef{"domain", ColumnType::STRING, false},
                                                    ColumnDef{"checkedAt", ColumnType::INT, false},
                                                });
    if (s != Status::OK)
        return s;

    s = db_.createTable(kMetaTable, VectorPro::Vector<ColumnDef>{
                                        ColumnDef{"schemaVersion", ColumnType::INT, false},
                                    });
    if (s != Status::OK)
        return s;

    Table* meta = db_.getTable(kMetaTable);
    if (meta == nullptr)
        return Status::TABLE_NOT_FOUND;

    Record metaRecord(kMetaRecordId);
    s = metaRecord.setField("schemaVersion",
                            JsonPro::Json(static_cast<double>(kCurrentSchemaVersion)));
    if (s != Status::OK)
        return s;

    return meta->insertRecord(metaRecord);
}

// ============================================================
//  Section 2 — Migrations
// ============================================================

void HistoryStore::runMigrations(std::uint64_t storedVersion) {
    // Nothing to do yet -- kCurrentSchemaVersion is 1, and there is no
    // version 0 to migrate from. See this function's doc comment in
    // HistoryStore.h for exactly how to add a migration step here when
    // check_runs' declared schema needs a change its undeclared-field
    // passthrough can't absorb for free.
    (void)storedVersion;
}

// ============================================================
//  Section 3 — Record Bookkeeping
// ============================================================

void HistoryStore::resyncNextRunId() {
    Table* table = db_.getTable(kCheckRunsTable);
    if (table == nullptr)
        return;

    ArenaPro::Arena<> arena(DBConstants::ARENA_SIZE);
    MiniDB::Engine::QueryEngine qe(arena);
    MiniDB::Engine::QueryResult result = qe.selectAll(*table);

    for (const Record& record : result.records)
        nextRunId_ = std::max(nextRunId_, record.id + 1);
}

// ============================================================
//  Section 4 — Writes
// ============================================================

Status HistoryStore::recordCheck(const std::string& domain, std::uint64_t checkedAtEpochSeconds,
                                 const JsonPro::Json& results) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ready_)
        return Status::TABLE_NOT_FOUND;

    Table* table = db_.getTable(kCheckRunsTable);
    if (table == nullptr)
        return Status::TABLE_NOT_FOUND;

    Record record(nextRunId_);
    Status s = record.setField("domain", JsonPro::Json(domain));
    if (s != Status::OK)
        return s;

    s = record.setField("checkedAt", JsonPro::Json(static_cast<double>(checkedAtEpochSeconds)));
    if (s != Status::OK)
        return s;

    // `results` is deliberately NOT declared in check_runs' schema (see
    // HistoryStore.h's class doc comment) -- setField() has no schema
    // check of its own, so this attaches the real nested Json array as
    // a plain field with no stringify/parse round trip.
    s = record.setField("results", results);
    if (s != Status::OK)
        return s;

    s = table->insertRecord(record);
    if (s != Status::OK)
        return s;

    ++nextRunId_;
    return db_.save(path_);
}

// ============================================================
//  Section 5 — Reads
// ============================================================

VectorPro::Vector<CheckRun> HistoryStore::getHistory(const std::string& domain, std::size_t limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    VectorPro::Vector<CheckRun> runs;
    if (!ready_)
        return runs;

    Table* table = db_.getTable(kCheckRunsTable);
    if (table == nullptr)
        return runs;

    ArenaPro::Arena<> arena(DBConstants::ARENA_SIZE);
    MiniDB::Engine::QueryEngine qe(arena);

    FilterPredicate domainFilter{"domain", Op::EQ, JsonPro::Json(domain)};
    SortCondition newestFirst{"checkedAt", SortOrder::DESC};

    MiniDB::Engine::QueryResult result =
        qe.select(*table, std::span<const FilterPredicate>(&domainFilter, 1), &newestFirst, limit);

    runs.reserve(result.records.size());
    for (const Record& record : result.records) {
        CheckRun run;
        run.domain = record.getField("domain").asString();
        run.checkedAtEpochSeconds =
            static_cast<std::uint64_t>(record.getField("checkedAt").asNumber());
        run.results = record.getField("results");
        runs.push_back(std::move(run));
    }

    return runs;
}

} // namespace DnsCheckup
