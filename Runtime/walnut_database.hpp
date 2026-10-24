#ifndef WALNUT_DATABASE_HPP
#define WALNUT_DATABASE_HPP

#include "walnut_runtime.hpp"
#include "fizmo/../database.hpp"

#include <cmath>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace walnut_rt {
namespace db {

namespace fd = fizmo::database;

template <typename Fn>
inline auto guarded(Fn&& fn) -> decltype(fn()) {
    try {
        return fn();
    } catch (const runtime_error&) {
        throw;
    } catch (const std::exception& e) {
        fail(e.what());
    }
}

template <typename F>
struct callable_traits : callable_traits<decltype(&F::operator())> {};

template <typename C, typename R, typename... A>
struct callable_traits<R (C::*)(A...) const> {
    using result = R;
    using args   = std::tuple<A...>;
};

template <typename C, typename R, typename... A>
struct callable_traits<R (C::*)(A...)> {
    using result = R;
    using args   = std::tuple<A...>;
};

template <typename R, typename... A>
struct callable_traits<std::function<R(A...)>> {
    using result = R;
    using args   = std::tuple<A...>;
};

template <typename F, std::size_t I>
using arg_of = std::decay_t<std::tuple_element_t<I, typename callable_traits<std::decay_t<F>>::args>>;

template <typename T, typename = void>
struct has_native_member : std::false_type {};

template <typename T>
struct has_native_member<T, std::void_t<decltype(std::declval<T&>().native)>> : std::true_type {};

template <typename T, typename V>
inline T to_walnut(V&& v) {
    if constexpr (std::is_same_v<std::decay_t<V>, T>) return T(std::forward<V>(v));
    else return T(walnut_rt::native_tag{}, std::forward<V>(v));
}

template <typename T, typename V>
inline T from_walnut(V&& v) {
    if constexpr (has_native_member<std::decay_t<V>>::value) return T(v.native);
    else return T(std::forward<V>(v));
}

struct api {};

class value {
public:
    value() = default;
    value(long long v) : m_v(static_cast<std::int64_t>(v)) {}
    value(double v) : m_v(v) {}
    value(const std::string& v) : m_v(v) {}
    value(bool v) : m_v(v) {}
    explicit value(fd::Value v) : m_v(std::move(v)) {}

    static value null() { return value(); }
    static value from_int(long long v) { return value(v); }
    static value from_float(double v) { return value(v); }
    static value from_wide(const walnut_rt::f128& v) { return value(walnut_rt::to_native<double>(v)); }
    static value from_text(const std::string& v) { return value(v); }
    static value from_bool(bool v) { return value(v); }
    static value from_blob(const std::string& bytes) { return value(fd::Value::blob(bytes)); }

    std::string type() const { return fd::to_string(m_v.type()); }
    bool is_null() const { return m_v.is_null(); }
    bool is_int() const { return m_v.is_int(); }
    bool is_float() const { return m_v.is_real(); }
    bool is_number() const { return m_v.is_number(); }
    bool is_text() const { return m_v.is_text(); }
    bool is_bool() const { return m_v.is_bool(); }
    bool is_blob() const { return m_v.is_blob(); }

    long long as_int() const { return guarded([&] { return static_cast<long long>(m_v.as_int()); }); }
    double as_float() const { return guarded([&] { return m_v.as_real(); }); }
    bool as_bool() const { return guarded([&] { return m_v.as_bool(); }); }
    std::string as_text() const { return m_v.is_text() ? m_v.get_text() : m_v.to_string(); }
    std::string as_blob() const { return m_v.is_blob() ? m_v.get_blob().bytes : std::string(); }

    long long int_or(long long fallback) const { return m_v.is_int() ? static_cast<long long>(m_v.get_int()) : fallback; }
    double float_or(double fallback) const { return m_v.is_number() ? m_v.numeric() : fallback; }
    std::string text_or(const std::string& fallback) const { return m_v.is_text() ? m_v.get_text() : fallback; }

    std::string to_string() const { return m_v.to_string(); }
    std::string to_json() const { return m_v.to_json(); }
    long long compare(const value& o) const { return fd::compare(m_v, o.m_v); }
    bool operator==(const value& o) const { return fd::compare(m_v, o.m_v) == 0; }
    bool operator!=(const value& o) const { return fd::compare(m_v, o.m_v) != 0; }
    bool operator<(const value& o) const { return fd::compare(m_v, o.m_v) < 0; }

    const fd::Value& raw() const noexcept { return m_v; }

private:
    fd::Value m_v;
};

template <typename V, typename X>
inline void push_native(V& out, X&& item) {
    using T = typename V::value_type;
    if constexpr (std::is_constructible_v<T, X&&>) out.push_back(T(std::forward<X>(item)));
    else out.push_back(T(walnut_rt::native_tag{}, std::forward<X>(item)));
}

template <typename V>
inline std::vector<fd::Value> raw_values(const V& vs) {
    std::vector<fd::Value> out;
    out.reserve(vs.size());
    for (const auto& v : vs) out.push_back(from_walnut<value>(v).raw());
    return out;
}

template <typename V>
inline void put_values(V& out, const std::vector<fd::Value>& vs) {
    out.clear();
    for (const auto& v : vs) push_native(out, value(v));
}

class expr {
public:
    expr() = default;
    explicit expr(fd::Expr e) : m_e(std::move(e)) {}

    static expr column(const std::string& name) { return guarded([&] { return expr(fd::col(name)); }); }
    static expr literal(const value& v) { return expr(fd::lit(v.raw())); }
    static expr param(long long index) { return expr(fd::param(static_cast<std::size_t>(index < 0 ? 0 : index))); }

    expr eq(const value& v) const { return expr(need() == fd::lit(v.raw())); }
    expr ne(const value& v) const { return expr(need() != fd::lit(v.raw())); }
    expr lt(const value& v) const { return expr(need() < fd::lit(v.raw())); }
    expr le(const value& v) const { return expr(need() <= fd::lit(v.raw())); }
    expr gt(const value& v) const { return expr(need() > fd::lit(v.raw())); }
    expr ge(const value& v) const { return expr(need() >= fd::lit(v.raw())); }
    expr eq_expr(const expr& o) const { return expr(need() == o.need()); }
    expr ne_expr(const expr& o) const { return expr(need() != o.need()); }
    expr lt_expr(const expr& o) const { return expr(need() < o.need()); }
    expr le_expr(const expr& o) const { return expr(need() <= o.need()); }
    expr gt_expr(const expr& o) const { return expr(need() > o.need()); }
    expr ge_expr(const expr& o) const { return expr(need() >= o.need()); }
    expr between(const value& lo, const value& hi) const { return expr(need().between(fd::lit(lo.raw()), fd::lit(hi.raw()))); }
    template <typename V> expr is_in(const V& vs) const { return expr(need().in(raw_values(vs))); }
    template <typename V> expr not_in(const V& vs) const { return expr(need().not_in(raw_values(vs))); }
    expr is_null() const { return expr(need().is_null()); }
    expr is_not_null() const { return expr(need().is_not_null()); }
    expr like(const std::string& pattern) const { return expr(need().like(fd::lit(pattern))); }
    expr ilike(const std::string& pattern) const { return expr(need().ilike(fd::lit(pattern))); }
    expr starts_with(const std::string& prefix) const { return expr(need().starts_with(prefix)); }
    expr both(const expr& o) const { return expr(need() && o.need()); }
    expr either(const expr& o) const { return expr(need() || o.need()); }
    expr negate() const { return expr(!need()); }
    expr plus(const expr& o) const { return expr(need() + o.need()); }
    expr minus(const expr& o) const { return expr(need() - o.need()); }
    expr times(const expr& o) const { return expr(need() * o.need()); }
    expr divided_by(const expr& o) const { return expr(need() / o.need()); }
    expr modulo(const expr& o) const { return expr(need() % o.need()); }
    expr lower() const { return expr(need().lower()); }
    expr upper() const { return expr(need().upper()); }
    expr length() const { return expr(need().length()); }
    expr absolute() const { return expr(need().abs()); }
    expr coalesce(const value& fallback) const { return expr(need().coalesce(fd::lit(fallback.raw()))); }
    bool empty() const { return !m_e.node(); }
    std::string to_string() const { return m_e.to_string(); }

    const fd::Expr& raw() const noexcept { return m_e; }

private:
    fd::Expr m_e;

    const fd::Expr& need() const {
        if (!m_e.node()) fail("this expression is empty; start one with std::database::column(\"name\")");
        return m_e;
    }
};

class record {
public:
    record() = default;

    record& set(const std::string& column, const value& v) {
        for (auto& kv : m_values) {
            if (kv.first == column) { kv.second = v.raw(); return *this; }
        }

        m_values.emplace_back(column, v.raw());
        return *this;
    }

    record& set_null(const std::string& column) { return set(column, value()); }
    bool has(const std::string& column) const {
        for (const auto& kv : m_values) if (kv.first == column) return true;
        return false;
    }

    value get(const std::string& column) const {
        for (const auto& kv : m_values) if (kv.first == column) return value(kv.second);
        return value();
    }

    long long size() const { return static_cast<long long>(m_values.size()); }
    std::vector<std::string> columns() const {
        std::vector<std::string> out;
        for (const auto& kv : m_values) out.push_back(kv.first);
        return out;
    }

    void clear() { m_values.clear(); }
    const fd::NamedValues& raw() const noexcept { return m_values; }

private:
    fd::NamedValues m_values;
};

class schema {
public:
    schema() = default;

    schema& add_integer(const std::string& name) { return add(name, fd::ColumnType::Integer); }
    schema& add_float(const std::string& name) { return add(name, fd::ColumnType::Real); }
    schema& add_text(const std::string& name) { return add(name, fd::ColumnType::Text); }
    schema& add_blob(const std::string& name) { return add(name, fd::ColumnType::Blob); }
    schema& add_bool(const std::string& name) { return add(name, fd::ColumnType::Boolean); }

    schema& primary_key() { last().primary(); return *this; }
    schema& auto_increment() { last().autoincrement(); return *this; }
    schema& not_null() { last().not_null(); return *this; }
    schema& unique() { last().unique_key(); return *this; }
    schema& nullable() { last().nullable = true; return *this; }
    schema& default_value(const value& v) { last().defaults(v.raw()); return *this; }

    long long size() const { return static_cast<long long>(m_columns.size()); }
    std::vector<std::string> column_names() const {
        std::vector<std::string> out;
        for (const auto& c : m_columns) out.push_back(c.name);
        return out;
    }

    fd::Schema build() const { return guarded([&] { return fd::Schema(m_columns); }); }

private:
    std::vector<fd::ColumnDef> m_columns;

    schema& add(const std::string& name, fd::ColumnType type) {
        m_columns.emplace_back(name, type);
        return *this;
    }

    fd::ColumnDef& last() {
        if (m_columns.empty()) fail("add a column (add_integer, add_text, ...) before setting its options");
        return m_columns.back();
    }
};

class query {
public:
    query() = default;
    explicit query(const std::string& table) : m_q(table) {}

    query& alias(const std::string& name) { m_q.as(name); return *this; }
    query& where(const expr& e) { m_q.where(e.raw()); return *this; }
    query& join(const std::string& table, const std::string& left_column, const std::string& right_column) { return add_join(table, left_column, right_column, fd::JoinKind::Inner, std::string()); }
    query& join_as(const std::string& table, const std::string& alias, const std::string& left_column, const std::string& right_column) { return add_join(table, left_column, right_column, fd::JoinKind::Inner, alias); }
    query& left_join(const std::string& table, const std::string& left_column, const std::string& right_column) { return add_join(table, left_column, right_column, fd::JoinKind::Left, std::string()); }
    query& select(const std::vector<std::string>& columns) {
        std::vector<fd::Expr> cols;
        for (const auto& c : columns) cols.push_back(fd::col(c));
        m_q.select(std::move(cols));
        return *this;
    }

    query& select_column(const std::string& column) { m_q.select(fd::col(column), std::string()); return *this; }
    query& select_as(const expr& e, const std::string& name) { m_q.select(e.raw(), name); return *this; }
    query& count(const std::string& name = "count") { m_q.count(name); return *this; }
    query& count_column(const std::string& column, const std::string& name) { m_q.count(fd::col(column), name); return *this; }
    query& count_distinct(const std::string& column, const std::string& name) { m_q.count_distinct(fd::col(column), name); return *this; }
    query& sum(const std::string& column, const std::string& name) { m_q.sum(fd::col(column), name); return *this; }
    query& avg(const std::string& column, const std::string& name) { m_q.avg(fd::col(column), name); return *this; }
    query& min(const std::string& column, const std::string& name) { m_q.min(fd::col(column), name); return *this; }
    query& max(const std::string& column, const std::string& name) { m_q.max(fd::col(column), name); return *this; }
    query& group_by(const std::string& column) { m_q.group_by(fd::col(column)); return *this; }
    query& having(const expr& e) { m_q.having(e.raw()); return *this; }
    query& order_by(const std::string& column, bool descending = false) { m_q.order_by(fd::col(column), descending ? fd::SortOrder::Desc : fd::SortOrder::Asc); return *this; }
    query& order_by_expr(const expr& e, bool descending = false) { m_q.order_by(e.raw(), descending ? fd::SortOrder::Desc : fd::SortOrder::Asc); return *this; }
    query& limit(long long n) { m_q.limit(static_cast<std::size_t>(n < 0 ? 0 : n)); return *this; }
    query& offset(long long n) { m_q.offset(static_cast<std::size_t>(n < 0 ? 0 : n)); return *this; }
    query& distinct() { m_q.distinct(true); return *this; }
    query& no_cache() { m_q.cache(false); return *this; }

    std::string table() const { return m_q.table(); }
    const fd::Query& raw() const noexcept { return m_q; }

private:
    fd::Query m_q;

    query& add_join(const std::string& table, const std::string& left_column, const std::string& right_column, fd::JoinKind kind, const std::string& alias) {
        guarded([&] { m_q.join(table, fd::col(left_column), fd::col(right_column), kind, alias); });
        return *this;
    }
};

class result {
public:
    result() = default;
    explicit result(fd::ResultSet rs) : m_rs(std::make_shared<fd::ResultSet>(std::move(rs))) {}

    long long size() const { return m_rs ? static_cast<long long>(m_rs->rows.size()) : 0; }
    bool empty() const { return size() == 0; }
    long long column_count() const { return m_rs ? static_cast<long long>(m_rs->columns.size()) : 0; }
    std::vector<std::string> columns() const { return m_rs ? m_rs->columns : std::vector<std::string>(); }

    value get(long long row, const std::string& column) const {
        return guarded([&] {
            need_row(row);
            const auto c = m_rs->column_index(column);
            if (!c) fail("the result has no column '" + column + "'");
            return value(m_rs->rows[static_cast<std::size_t>(row)][*c]);
        });
    }

    value at(long long row, long long column) const {
        need_row(row);
        if (column < 0 || column >= column_count()) fail("column " + std::to_string(column) + " is out of range");
        return value(m_rs->rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)]);
    }

    template <typename V>
    void row_into(long long index, V& out) const {
        need_row(index);
        put_values(out, m_rs->rows[static_cast<std::size_t>(index)]);
    }

    template <typename V>
    void column_into(const std::string& name, V& out) const {
        out.clear();
        if (!m_rs) return;
        const auto c = m_rs->column_index(name);
        if (!c) fail("the result has no column '" + name + "'");
        for (const auto& r : m_rs->rows) push_native(out, value(r[*c]));
    }

    value scalar() const { return m_rs ? value(m_rs->scalar()) : value(); }
    std::string to_json() const { return m_rs ? m_rs->to_json() : std::string("[]"); }
    std::string to_text() const { return m_rs ? m_rs->to_text() : std::string(); }
    std::string plan() const { return m_rs ? m_rs->stats.plan : std::string(); }
    bool used_index() const { return m_rs && m_rs->stats.used_index; }
    bool cache_hit() const { return m_rs && m_rs->stats.cache_hit; }
    long long rows_examined() const { return m_rs ? static_cast<long long>(m_rs->stats.rows_examined) : 0; }
    long long elapsed_us() const { return m_rs ? static_cast<long long>(m_rs->stats.elapsed.count()) : 0; }

private:
    std::shared_ptr<const fd::ResultSet> m_rs;

    void need_row(long long row) const {
        if (row < 0 || row >= size()) fail("row " + std::to_string(row) + " is out of range (the result has " + std::to_string(size()) + " rows)");
    }
};

inline fd::IndexKind index_kind(const std::string& kind) {
    if (kind == "hash") return fd::IndexKind::Hash;
    if (kind == "ordered" || kind.empty()) return fd::IndexKind::Ordered;
    fail("index kind must be \"hash\" or \"ordered\", not \"" + kind + "\"");
}

class graph {
public:
    graph() = default;
    explicit graph(std::shared_ptr<const fd::Graph> g) : m_g(std::move(g)) {}

    long long node_count() const { return m_g ? static_cast<long long>(m_g->node_count()) : 0; }
    long long edge_count() const { return m_g ? static_cast<long long>(m_g->edge_count()) : 0; }
    bool contains(const value& node) const { return m_g && m_g->contains(node.raw()); }
    template <typename V> void neighbors_into(const value& node, V& out) const { put_values(out, m_g ? m_g->neighbors(node.raw()) : std::vector<fd::Value>()); }
    template <typename V> void incoming_into(const value& node, V& out) const { put_values(out, m_g ? m_g->incoming(node.raw()) : std::vector<fd::Value>()); }
    template <typename V> void bfs_into(const value& start, long long max_depth, V& out) const { visits(m_g ? m_g->bfs(start.raw(), depth(max_depth)) : std::vector<fd::GraphVisit>(), out); }
    template <typename V> void dfs_into(const value& start, long long max_depth, V& out) const { visits(m_g ? m_g->dfs(start.raw(), depth(max_depth)) : std::vector<fd::GraphVisit>(), out); }

    template <typename V>
    void bfs_depths_into(const value& start, long long max_depth, V& out) const {
        out.clear();
        if (!m_g) return;
        for (const auto& v : m_g->bfs(start.raw(), depth(max_depth))) push_native(out, static_cast<long long>(v.depth));
    }

    template <typename V> void shortest_path_into(const value& from, const value& to, V& out) const { put_values(out, m_g ? m_g->shortest_path(from.raw(), to.raw()).nodes : std::vector<fd::Value>()); }
    double path_cost(const value& from, const value& to) const {
        if (!m_g) return -1.0;
        const auto p = m_g->shortest_path(from.raw(), to.raw());
        return p.found ? p.cost : -1.0;
    }

    bool reachable(const value& from, const value& to) const { return m_g && m_g->reachable(from.raw(), to.raw()); }
    long long component_count() const { return m_g ? static_cast<long long>(m_g->components().size()) : 0; }

    template <typename V>
    void component_into(long long index, V& out) const {
        out.clear();
        if (!m_g) return;
        const auto comps = m_g->components();
        if (index < 0 || index >= static_cast<long long>(comps.size())) fail("component " + std::to_string(index) + " is out of range");
        put_values(out, comps[static_cast<std::size_t>(index)]);
    }

    template <typename V>
    void topological_into(V& out) const {
        out.clear();
        if (!m_g) return;
        guarded([&] {
            const auto order = m_g->topological_order();
            if (order) put_values(out, *order);
        });
    }

    bool has_cycle() const { return m_g && guarded([&] { return m_g->has_cycle(); }); }

private:
    std::shared_ptr<const fd::Graph> m_g;

    static std::size_t depth(long long d) { return d < 0 ? fd::Graph::kUnlimited : static_cast<std::size_t>(d); }

    template <typename V>
    static void visits(const std::vector<fd::GraphVisit>& vs, V& out) {
        out.clear();
        for (const auto& v : vs) push_native(out, value(v.node));
    }
};

class transaction {
public:
    transaction() = default;
    explicit transaction(std::shared_ptr<fd::Transaction> tx) : m_tx(std::move(tx)) {}

    void create_table(const std::string& name, const schema& s) { guarded([&] { tx().create_table(name, s.build()); }); }
    bool drop_table(const std::string& name) { return guarded([&] { return tx().drop_table(name); }); }

    void create_index(const std::string& table, const std::vector<std::string>& columns, const std::string& kind = "ordered", bool unique = false) {
        guarded([&] { tx().create_index(table, fd::IndexDef(std::string(), columns, index_kind(kind), unique)); });
    }

    long long insert(const std::string& table, const record& r) { return guarded([&] { return static_cast<long long>(tx().insert(table, r.raw())); }); }
    long long update(const std::string& table, const expr& where, const record& changes) { return guarded([&] { return static_cast<long long>(tx().update(table, where.raw(), changes.raw())); }); }
    long long remove(const std::string& table, const expr& where) { return guarded([&] { return static_cast<long long>(tx().remove(table, where.raw())); }); }
    long long clear(const std::string& table) { return guarded([&] { return static_cast<long long>(tx().clear(table)); }); }
    result run(const query& q) const { return guarded([&] { return result(tx().query(q.raw())); }); }
    template <typename V> result run_with(const query& q, const V& params) const { return guarded([&] { return result(tx().query(q.raw(), raw_values(params))); }); }

    void commit() { guarded([&] { tx().commit(); }); }
    void rollback() { guarded([&] { tx().rollback(); }); }
    bool active() const { return m_tx && m_tx->active(); }

private:
    std::shared_ptr<fd::Transaction> m_tx;

    fd::Transaction& tx() const {
        if (!m_tx || !m_tx->active()) fail("this transaction has already finished");
        return *m_tx;
    }
};

class statement {
public:
    statement() = default;
    explicit statement(std::shared_ptr<fd::Statement> s) : m_s(std::move(s)) {}

    template <typename V>
    result run(const V& params) const {
        if (!m_s) fail("this statement is empty; make one with Database.prepare(query)");
        return guarded([&] { return result(m_s->run(raw_values(params))); });
    }

private:
    std::shared_ptr<fd::Statement> m_s;
};

class database {
public:
    database() : m_db(std::make_shared<fd::Database>()) {}
    explicit database(std::shared_ptr<fd::Database> db) : m_db(std::move(db)) {}

    static database open(const std::string& path) {
        return guarded([&] {
            fd::DatabaseOptions o;
            o.path = path;
            return database(std::make_shared<fd::Database>(o));
        });
    }

    static database open_durable(const std::string& path) {
        return guarded([&] {
            fd::DatabaseOptions o;
            o.path = path;
            o.sync = fd::SyncMode::Full;
            return database(std::make_shared<fd::Database>(o));
        });
    }

    void create_table(const std::string& name, const schema& s) { guarded([&] { m_db->create_table(name, s.build()); }); }
    bool drop_table(const std::string& name) { return guarded([&] { return m_db->drop_table(name); }); }
    bool has_table(const std::string& name) const { return m_db->has_table(name); }
    std::vector<std::string> tables() const { return m_db->tables(); }

    void create_index(const std::string& table, const std::vector<std::string>& columns, const std::string& kind = "ordered", bool unique = false) {
        guarded([&] { m_db->create_index(table, columns, index_kind(kind), unique); });
    }

    bool drop_index(const std::string& table, const std::string& name) { return guarded([&] { return m_db->drop_index(table, name); }); }

    long long insert(const std::string& table, const record& r) { return guarded([&] { return static_cast<long long>(m_db->insert(table, r.raw())); }); }

    template <typename V>
    long long insert_many(const std::string& table, const V& rows) {
        return guarded([&] {
            return static_cast<long long>(m_db->transaction([&](fd::Transaction& tx) {
                for (const auto& r : rows) tx.insert(table, from_walnut<record>(r).raw());
                return rows.size();
            }));
        });
    }

    long long update(const std::string& table, const expr& where, const record& changes) { return guarded([&] { return static_cast<long long>(m_db->update(table, where.raw(), changes.raw())); }); }
    long long remove(const std::string& table, const expr& where) { return guarded([&] { return static_cast<long long>(m_db->remove(table, where.raw())); }); }

    result run(const query& q) const { return guarded([&] { return result(m_db->query(q.raw())); }); }
    template <typename V> result run_with(const query& q, const V& params) const { return guarded([&] { return result(m_db->query(q.raw(), raw_values(params))); }); }
    result find(const std::string& table, const expr& where) const { return guarded([&] { return result(m_db->select(table, where.raw())); }); }
    result all(const std::string& table) const { return guarded([&] { return result(m_db->select(table)); }); }
    long long count(const std::string& table) const { return guarded([&] { return static_cast<long long>(m_db->count(table)); }); }
    long long count_where(const std::string& table, const expr& where) const { return guarded([&] { return static_cast<long long>(m_db->count(table, where.raw())); }); }

    result get(const std::string& table, const value& key) const {
        return guarded([&] {
            fd::ResultSet rs;
            const fd::Schema s = m_db->schema(table);
            for (const auto& c : s.columns()) rs.columns.push_back(c.name);
            const auto row = m_db->get(table, { key.raw() });
            if (row) rs.rows.push_back(*row);
            return result(std::move(rs));
        });
    }

    std::string explain(const query& q) const { return guarded([&] { return m_db->explain(q.raw()); }); }
    statement prepare(const query& q) { return statement(std::make_shared<fd::Statement>(m_db->prepare(q.raw()))); }

    transaction begin() { return guarded([&] { return transaction(std::make_shared<fd::Transaction>(m_db->begin())); }); }

    template <typename F>
    bool run_transaction(F fn) {
        transaction tx = begin();

        try {
            const bool keep = fn(to_walnut<arg_of<F, 0>>(tx));
            if (!tx.active()) return keep;
            if (keep) tx.commit();
            else tx.rollback();
            return keep;
        } catch (...) {
            if (tx.active()) tx.rollback();
            throw;
        }
    }

    graph graph_of(const std::string& table, const std::string& from_column, const std::string& to_column, const std::string& weight_column = "", bool directed = true) const {
        return guarded([&] {
            fd::GraphOptions o;
            o.from = from_column;
            o.to = to_column;
            o.weight = weight_column;
            o.directed = directed;
            return graph(m_db->graph(table, o));
        });
    }

    void checkpoint() { guarded([&] { m_db->checkpoint(); }); }
    void maintain() { m_db->maintain(); }
    void set_cache(long long entries, long long rows) { m_db->set_cache(static_cast<std::size_t>(entries < 0 ? 0 : entries), static_cast<std::size_t>(rows < 0 ? 0 : rows)); }
    void clear_cache() { m_db->clear_cache(); }
    long long row_count() const { return static_cast<long long>(m_db->stats().rows); }
    long long cache_hits() const { return static_cast<long long>(m_db->stats().cache.hits); }
    long long cache_misses() const { return static_cast<long long>(m_db->stats().cache.misses); }

    std::string stats_json() const {
        const fd::DatabaseStats s = m_db->stats();
        return "{\"tables\":" + std::to_string(s.tables) + ",\"rows\":" + std::to_string(s.rows) + ",\"memory_bytes\":" + std::to_string(s.memory_bytes)
             + ",\"cache_hits\":" + std::to_string(s.cache.hits) + ",\"cache_misses\":" + std::to_string(s.cache.misses) + ",\"wal_bytes\":" + std::to_string(s.wal_bytes)
             + ",\"queries\":" + std::to_string(s.queries) + ",\"commits\":" + std::to_string(s.commits) + ",\"rollbacks\":" + std::to_string(s.rollbacks) + "}";
    }

private:
    std::shared_ptr<fd::Database> m_db;
};

inline bool like_match(const std::string& text, const std::string& pattern) { return fd::like_match(text, pattern, false); }

} // namespace db
} // namespace walnut_rt

#endif // WALNUT_DATABASE_HPP
