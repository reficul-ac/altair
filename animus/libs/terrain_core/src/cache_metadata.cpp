#include "animus/terrain_core/cache_metadata.hpp"

#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace animus::terrain_core
{
namespace
{

constexpr int schema_version = 1;

struct SqliteCloser
{
    void operator()(sqlite3 *db) const
    {
        if (db != nullptr)
        {
            sqlite3_close(db);
        }
    }
};

struct StatementCloser
{
    void operator()(sqlite3_stmt *statement) const
    {
        if (statement != nullptr)
        {
            sqlite3_finalize(statement);
        }
    }
};

using SqliteDb = std::unique_ptr<sqlite3, SqliteCloser>;
using SqliteStatement = std::unique_ptr<sqlite3_stmt, StatementCloser>;

SqliteDb open_db(const std::filesystem::path &path, const int flags)
{
    sqlite3 *raw = nullptr;
    if (sqlite3_open_v2(path.string().c_str(), &raw, flags | SQLITE_OPEN_NOMUTEX, nullptr) !=
        SQLITE_OK)
    {
        std::string message = "Failed to open cache metadata database";
        if (raw != nullptr)
        {
            message += ": ";
            message += sqlite3_errmsg(raw);
            sqlite3_close(raw);
        }
        throw std::runtime_error(message);
    }
    return SqliteDb(raw);
}

void exec(sqlite3 *db, const char *sql)
{
    char *error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK)
    {
        std::string message = error == nullptr ? "SQLite exec failed" : error;
        sqlite3_free(error);
        throw std::runtime_error(message);
    }
}

SqliteStatement prepare(sqlite3 *db, const char *sql)
{
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        throw std::runtime_error(sqlite3_errmsg(db));
    }
    return SqliteStatement(statement);
}

std::int64_t now_unix_s()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string encode_coords(const std::vector<geo_core::TileCoord> &coords)
{
    std::ostringstream stream;
    for (std::size_t index = 0; index < coords.size(); ++index)
    {
        const auto coord = coords[index];
        stream << (index == 0U ? "" : ";") << coord.z << ',' << coord.x << ',' << coord.y;
    }
    return stream.str();
}

std::vector<geo_core::TileCoord> decode_coords(const std::string &text)
{
    std::vector<geo_core::TileCoord> coords;
    std::istringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ';'))
    {
        std::istringstream parts(item);
        std::string z;
        std::string x;
        std::string y;
        if (std::getline(parts, z, ',') && std::getline(parts, x, ',') && std::getline(parts, y))
        {
            coords.push_back({std::stoi(z), std::stoi(x), std::stoi(y)});
        }
    }
    return coords;
}

const char *text_column(sqlite3_stmt *statement, const int index)
{
    const auto *text = reinterpret_cast<const char *>(sqlite3_column_text(statement, index));
    return text == nullptr ? "" : text;
}

} // namespace

CacheMetadataStore::CacheMetadataStore(std::filesystem::path cache_root)
    : cache_root_(std::move(cache_root))
{
}

std::filesystem::path CacheMetadataStore::database_path() const
{
    return cache_root_ / "metadata.sqlite3";
}

void CacheMetadataStore::initialize()
{
    std::filesystem::create_directories(cache_root_);
    auto db = open_db(database_path(), SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    exec(db.get(), "PRAGMA journal_mode=WAL");
    exec(db.get(),
         "CREATE TABLE IF NOT EXISTS meta ("
         "key TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL)");
    exec(db.get(),
         "CREATE TABLE IF NOT EXISTS tiles ("
         "layer_identity TEXT NOT NULL,"
         "z INTEGER NOT NULL,"
         "x INTEGER NOT NULL,"
         "y INTEGER NOT NULL,"
         "source_type TEXT NOT NULL,"
         "provenance TEXT NOT NULL,"
         "source_identity TEXT NOT NULL,"
         "created_unix_s INTEGER NOT NULL,"
         "updated_unix_s INTEGER NOT NULL,"
         "byte_size INTEGER NOT NULL,"
         "source_coords TEXT NOT NULL,"
         "validation_status TEXT NOT NULL,"
         "PRIMARY KEY(layer_identity,z,x,y))");
    auto statement =
        prepare(db.get(), "INSERT OR REPLACE INTO meta(key, value) VALUES('schema_version', ?1)");
    sqlite3_bind_text(
        statement.get(), 1, std::to_string(schema_version).c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(statement.get()) != SQLITE_DONE)
    {
        throw std::runtime_error(sqlite3_errmsg(db.get()));
    }
}

void CacheMetadataStore::upsert(const CacheMetadataRecord &record)
{
    initialize();
    auto db = open_db(database_path(), SQLITE_OPEN_READWRITE);
    const std::int64_t now = now_unix_s();
    const std::int64_t created = record.created_unix_s == 0 ? now : record.created_unix_s;
    const std::int64_t updated = record.updated_unix_s == 0 ? now : record.updated_unix_s;
    auto statement =
        prepare(db.get(),
                "INSERT INTO tiles(layer_identity,z,x,y,source_type,provenance,source_identity,"
                "created_unix_s,updated_unix_s,byte_size,source_coords,validation_status) "
                "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12) "
                "ON CONFLICT(layer_identity,z,x,y) DO UPDATE SET "
                "source_type=excluded.source_type, provenance=excluded.provenance, "
                "source_identity=excluded.source_identity, updated_unix_s=excluded.updated_unix_s, "
                "byte_size=excluded.byte_size, source_coords=excluded.source_coords, "
                "validation_status=excluded.validation_status");
    sqlite3_bind_text(statement.get(), 1, record.layer_identity.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement.get(), 2, record.coord.z);
    sqlite3_bind_int(statement.get(), 3, record.coord.x);
    sqlite3_bind_int(statement.get(), 4, record.coord.y);
    const std::string source_type(to_string(record.source_type));
    sqlite3_bind_text(statement.get(), 5, source_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement.get(), 6, record.provenance.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement.get(), 7, record.source_identity.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement.get(), 8, created);
    sqlite3_bind_int64(statement.get(), 9, updated);
    sqlite3_bind_int64(statement.get(), 10, static_cast<sqlite3_int64>(record.byte_size));
    const std::string sources = encode_coords(record.source_coords);
    sqlite3_bind_text(statement.get(), 11, sources.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement.get(), 12, record.validation_status.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(statement.get()) != SQLITE_DONE)
    {
        throw std::runtime_error(sqlite3_errmsg(db.get()));
    }
}

std::optional<CacheMetadataRecord> CacheMetadataStore::read(const std::string &layer_identity,
                                                            const geo_core::TileCoord coord) const
{
    if (!std::filesystem::exists(database_path()))
    {
        return std::nullopt;
    }
    auto db = open_db(database_path(), SQLITE_OPEN_READONLY);
    auto statement = prepare(
        db.get(),
        "SELECT source_type,provenance,source_identity,created_unix_s,updated_unix_s,byte_size,"
        "source_coords,validation_status FROM tiles "
        "WHERE layer_identity=?1 AND z=?2 AND x=?3 AND y=?4");
    sqlite3_bind_text(statement.get(), 1, layer_identity.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement.get(), 2, coord.z);
    sqlite3_bind_int(statement.get(), 3, coord.x);
    sqlite3_bind_int(statement.get(), 4, coord.y);
    if (sqlite3_step(statement.get()) != SQLITE_ROW)
    {
        return std::nullopt;
    }

    CacheMetadataRecord record;
    record.layer_identity = layer_identity;
    record.source_type = TileSourceType::None;
    record.provenance = text_column(statement.get(), 1);
    record.source_identity = text_column(statement.get(), 2);
    record.created_unix_s = sqlite3_column_int64(statement.get(), 3);
    record.updated_unix_s = sqlite3_column_int64(statement.get(), 4);
    record.byte_size = static_cast<std::uint64_t>(sqlite3_column_int64(statement.get(), 5));
    record.coord = coord;
    record.source_coords = decode_coords(text_column(statement.get(), 6));
    record.validation_status = text_column(statement.get(), 7);
    return record;
}

} // namespace animus::terrain_core
