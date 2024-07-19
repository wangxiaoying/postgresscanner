//===----------------------------------------------------------------------===//
//                         DuckDB
//
// postgres_scanner.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb.hpp"
#include "postgres_utils.hpp"
#include "postgres_connection.hpp"
#include "storage/postgres_connection_pool.hpp"

namespace duckdb {
class PostgresCatalog;
struct PostgresLocalState;
struct PostgresGlobalState;
class PostgresTransaction;

struct PostgresBindData : public FunctionData {
	static constexpr const idx_t DEFAULT_PAGES_PER_TASK = 1000;

	PostgresVersion version;
	string schema_name;
	string table_name;
	string sql;
    vector<string> sqls; // queries with the same schema that form a single table
    idx_t estimated_cardinality = 0;
	idx_t pages_approx = 0;

	vector<PostgresType> postgres_types;
	vector<string> names;
	vector<LogicalType> types;

	idx_t pages_per_task = DEFAULT_PAGES_PER_TASK;
	string dsn;

	bool requires_materialization = true;
	bool can_use_main_thread = true;
	bool read_only = true;
	bool emit_ctid = false;
	idx_t max_threads = 1;

public:
	void SetTablePages(idx_t approx_num_pages);

	void SetCatalog(PostgresCatalog &catalog);
	optional_ptr<PostgresCatalog> GetCatalog() const {
		return pg_catalog;
	}

	unique_ptr<FunctionData> Copy() const override {
		throw NotImplementedException("");
	}
	bool Equals(const FunctionData &other_p) const override {
		return false;
	}

private:
	optional_ptr<PostgresCatalog> pg_catalog;
};

class PostgresAttachFunction : public TableFunction {
public:
	PostgresAttachFunction();
};

class PostgresScanFunction : public TableFunction {
public:
	PostgresScanFunction();

	static void PrepareBind(PostgresVersion version, ClientContext &context, PostgresBindData &bind,
	                        idx_t approx_num_pages);
};

class PostgresScanFunctionFilterPushdown : public TableFunction {
public:
	PostgresScanFunctionFilterPushdown();
};

class PostgresClearCacheFunction : public TableFunction {
public:
	PostgresClearCacheFunction();

	static void ClearCacheOnSetting(ClientContext &context, SetScope scope, Value &parameter);
	static void ClearPostgresCaches(ClientContext &context);
};

class PostgresQueryFunction : public TableFunction {
public:
	PostgresQueryFunction();
};

class PostgresExecuteFunction : public TableFunction {
public:
	PostgresExecuteFunction();
};

} // namespace duckdb
