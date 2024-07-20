#include "duckdb.hpp"

#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "postgres_scanner.hpp"
#include "duckdb/main/database_manager.hpp"
#include "duckdb/main/attached_database.hpp"
#include "storage/postgres_catalog.hpp"
#include "storage/postgres_transaction.hpp"

namespace duckdb {

static unique_ptr<FunctionData> PGQueryBind(ClientContext &context, TableFunctionBindInput &input,
                                            vector<LogicalType> &return_types, vector<string> &names) {
	auto result = make_uniq<PostgresBindData>();

	if (input.inputs[0].IsNull() || input.inputs[1].IsNull()) {
		throw BinderException("Parameters to postgres_query cannot be NULL");
	}

	// look up the database to query
	auto db_name = input.inputs[0].GetValue<string>();
	auto &db_manager = DatabaseManager::Get(context);
	auto db = db_manager.GetDatabase(context, db_name);
	if (!db) {
		throw BinderException("Failed to find attached database \"%s\" referenced in postgres_query", db_name);
	}
	auto &catalog = db->GetCatalog();
	if (catalog.GetCatalogType() != "postgres") {
		throw BinderException("Attached database \"%s\" does not refer to a Postgres database", db_name);
	}
	auto &pg_catalog = catalog.Cast<PostgresCatalog>();
	auto &transaction = Transaction::Get(context, catalog).Cast<PostgresTransaction>();
	auto sql = input.inputs[1].GetValue<string>();
	// strip any trailing semicolons
    vector<string> sqls;
	StringUtil::RTrim(sql);
    size_t pos = 0;
    while ((pos = sql.find(';')) != string::npos) {
        auto s = sql.substr(0, pos);
        StringUtil::RTrim(s);
        if (!s.empty()) {
            sqls.push_back(std::move(s));
        }
        sql = sql.substr(pos+1, string::npos);
        StringUtil::LTrim(sql);
    }
    if (!sql.empty()) {
        sqls.push_back(std::move(sql));
    }
    sql = sqls[0]; // use the first query to fetch schema info
    auto estimated_cardinality = input.inputs[2].GetValue<idx_t>();

	auto &con = transaction.GetConnection();
	auto conn = con.GetConn();
	// prepare execution of the query to figure out the result types and names
	auto prepared = PQprepare(conn, "", sql.c_str(), 0, nullptr);
	PostgresResult prepared_wrapper(prepared);
	if (!prepared) {
		throw BinderException("Failed to prepare query \"%s\" (no result returned): %s", sql, PQerrorMessage(conn));
	}
	if (PQresultStatus(prepared) != PGRES_COMMAND_OK) {
		throw BinderException("Failed to prepare query \"%s\": %s", sql, PQresultErrorMessage(prepared));
	}
	// use describe_prepared
	auto describe_prepared = PQdescribePrepared(conn, "");
	PostgresResult describe_wrapper(describe_prepared);
	if (!describe_prepared || PQresultStatus(describe_prepared) != PGRES_COMMAND_OK) {
		auto extended_err = describe_prepared ? PQresultErrorMessage(describe_prepared) : PQerrorMessage(conn);
		throw BinderException("Failed to describe prepared statement: %s", extended_err);
	}
	auto nfields = PQnfields(describe_prepared);
	if (nfields <= 0) {
		throw BinderException("No fields returned by query \"%s\" - the query must be a SELECT statement that returns "
		                      "at least one column",
		                      sql);
	}
	for (idx_t c = 0; c < nfields; c++) {
		PostgresType postgres_type;
		postgres_type.oid = PQftype(describe_prepared, c);
		PostgresTypeData type_data;
		type_data.type_name = PostgresUtils::PostgresOidToName(postgres_type.oid);
		type_data.type_modifier = PQfmod(describe_prepared, c);
		auto converted_type = PostgresUtils::TypeToLogicalType(nullptr, nullptr, type_data, postgres_type);
		result->postgres_types.push_back(postgres_type);
		return_types.emplace_back(converted_type);
		names.emplace_back(PQfname(describe_prepared, c));
	}

	// set up the bind data
	result->SetCatalog(pg_catalog);
	result->dsn = con.GetDSN();
	result->types = return_types;
	result->names = names;
	result->read_only = (sqls.size() > 1); // read only otherwise cannot enable parallelism in `PostgresOptimizer::Optimize`
	result->SetTablePages(0);
    result->max_threads = MaxValue<idx_t>(sqls.size(), 1);
    result->sqls = std::move(sqls);
    result->estimated_cardinality = estimated_cardinality;
    result->requires_materialization = false; // materialize only use single thread
	return std::move(result);
}

unique_ptr<NodeStatistics> PQQueryCardinality(ClientContext &context, const FunctionData *bind_data_p) {
	auto &bind_data = bind_data_p->Cast<PostgresBindData>();
	return make_uniq<NodeStatistics>(bind_data.estimated_cardinality);
}

PostgresQueryFunction::PostgresQueryFunction()
    : TableFunction("postgres_query", {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::INTEGER}, nullptr, PGQueryBind) {
	PostgresScanFunction scan_function;
	init_global = scan_function.init_global;
	init_local = scan_function.init_local;
	function = scan_function.function;
    cardinality = PQQueryCardinality;
	projection_pushdown = true;
}
} // namespace duckdb
