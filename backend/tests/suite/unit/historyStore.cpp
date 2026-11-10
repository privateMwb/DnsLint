// Unit HistoryStore Test Suite
// Verifies the persisted check history: recording runs, reading them back
// newest first, limits, isolation between domains, and surviving a restart.
//
// Covers:
// - A recorded run reads back with its domain, timestamp and checks array
// - Runs come back newest first, and the limit is honored
// - One domain's history never includes another's
// - History written by one store is read by a new store on the same file
// - Run ids keep increasing after a restart (nothing is overwritten)
// - An unopened store degrades to empty results instead of crashing

#include <support/framework.h>
#include <support/tempDir.h>

#include "database/HistoryStore.h"

#include <cstdint>
#include <string>

namespace {

using DnsCheckup::HistoryStore;
using MiniDB::Common::Status;

JsonPro::Json checksWith(const std::string& marker) {
    JsonPro::Json row = JsonPro::Json::ObjectType{};
    row["name"] = marker;
    row["status"] = std::string("pass");
    row["message"] = std::string("ok");

    JsonPro::Json::ArrayType rows;
    rows.push_back(row);
    return JsonPro::Json(rows);
}

} // namespace

// Verifies one run reads back unchanged.
static void record_and_read_back() {
    TempDir dir;
    HistoryStore store;
    CHK(store.open(dir.file("history.json")) == Status::OK);
    CHK(store.recordCheck("example.com", 1000, checksWith("a")) == Status::OK);

    const auto runs = store.getHistory("example.com", 10);
    CHK(runs.size() == 1);
    CHK(runs[0].domain == "example.com");
    CHK(runs[0].checkedAtEpochSeconds == 1000);
    CHK(runs[0].results.isArray());
    CHK(runs[0].results.size() == 1);
}

// Verifies newest-first ordering and the limit.
static void newest_first_and_limit() {
    TempDir dir;
    HistoryStore store;
    CHK(store.open(dir.file("history.json")) == Status::OK);
    for (std::uint64_t t = 1; t <= 5; ++t)
        CHK(store.recordCheck("example.com", t * 100, checksWith("x")) == Status::OK);

    const auto runs = store.getHistory("example.com", 3);
    CHK(runs.size() == 3);
    CHK(runs[0].checkedAtEpochSeconds == 500);
    CHK(runs[1].checkedAtEpochSeconds == 400);
    CHK(runs[2].checkedAtEpochSeconds == 300);
}

// Verifies domains do not leak into each other's history.
static void domains_are_isolated() {
    TempDir dir;
    HistoryStore store;
    CHK(store.open(dir.file("history.json")) == Status::OK);
    CHK(store.recordCheck("a.example.com", 1, checksWith("a")) == Status::OK);
    CHK(store.recordCheck("b.example.com", 2, checksWith("b")) == Status::OK);

    CHK(store.getHistory("a.example.com", 10).size() == 1);
    CHK(store.getHistory("b.example.com", 10).size() == 1);
    CHK(store.getHistory("c.example.com", 10).size() == 0);
}

// Verifies a second store on the same file sees what the first one wrote.
static void history_survives_restart() {
    TempDir dir;
    const std::string path = dir.file("history.json");
    {
        HistoryStore store;
        CHK(store.open(path) == Status::OK);
        CHK(store.recordCheck("example.com", 111, checksWith("a")) == Status::OK);
    }

    HistoryStore reopened;
    CHK(reopened.open(path) == Status::OK);
    const auto runs = reopened.getHistory("example.com", 10);
    CHK(runs.size() == 1);
    CHK(runs[0].checkedAtEpochSeconds == 111);
}

// Verifies runs recorded after a restart are added, not overwriting earlier ones.
static void ids_keep_increasing_after_restart() {
    TempDir dir;
    const std::string path = dir.file("history.json");
    {
        HistoryStore store;
        CHK(store.open(path) == Status::OK);
        CHK(store.recordCheck("example.com", 100, checksWith("a")) == Status::OK);
        CHK(store.recordCheck("example.com", 200, checksWith("b")) == Status::OK);
    }

    HistoryStore reopened;
    CHK(reopened.open(path) == Status::OK);
    CHK(reopened.recordCheck("example.com", 300, checksWith("c")) == Status::OK);

    const auto runs = reopened.getHistory("example.com", 10);
    CHK(runs.size() == 3);
    CHK(runs[0].checkedAtEpochSeconds == 300);
    CHK(runs[2].checkedAtEpochSeconds == 100);
}

// Verifies a store that was never opened returns nothing and reports an error status.
static void unopened_store_degrades() {
    HistoryStore store;
    CHK(store.recordCheck("example.com", 1, checksWith("a")) != Status::OK);
    CHK(store.getHistory("example.com", 10).size() == 0);
}

static void run_tests() {
    RUN(record_and_read_back);
    RUN(newest_first_and_limit);
    RUN(domains_are_isolated);
    RUN(history_survives_restart);
    RUN(ids_keep_increasing_after_restart);
    RUN(unopened_store_degrades);
}

REGISTER_TEST_SUITE();
