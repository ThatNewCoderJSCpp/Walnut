# database

`database` is an embedded database: typed tables, hash and ordered indexes, a query planner, transactions, a write-ahead log on disk, and graph searches over edge tables. It is built on Fizmo's database library.

```walnut
import module.database
```

Everything is in the namespace `std::database`. Errors (a missing table, a wrong type, a `NOT NULL` or `UNIQUE` violation) are thrown as runtime errors, which `catch (...)` catches.

## A first example

```walnut
import module.io
import module.collections
import module.database

using std::database::Value;
using std::database::Record;
using std::database::Query;
using std::database::Schema;

function main() -> int {
    std::database::Database: db;
    Schema: s;
    s.add_integer("id").primary_key().auto_increment();
    s.add_text("title").not_null();
    s.add_float("rating");
    db.create_table("recipes", s);
    db.create_index("recipes", std::collections::Vector<string>{"rating"});

    Record: r;
    r.set("title", Value("Soup")).set("rating", Value(4.5));
    db.insert("recipes", r);

    auto: best = db.run(Query("recipes").where(std::database::column("rating").gt(Value(4.0))).order_by("rating", true));
    std::io::println(best.get(0, "title").as_text(), " ", best.plan());
    return 0;
} end function main
```

This prints `Soup INDEX RANGE ix_recipes_rating | ...`.

## Values

`Value` holds NULL, a `bool`, a `long int`, a `short float`, a `string` or a blob. Make one with `Value(5)`, `Value(4.5)`, `Value("text")`, `Value(true)`, `Value::null()` or `Value::from_blob(bytes)`, and read it with `as_int()`, `as_float()`, `as_text()`, `as_bool()`, `as_blob()`, or `int_or(fallback)` and the other `_or` methods. `is_null()`, `type()`, `to_string()` and `to_json()` work on any value. Integers and floats compare by value, so `Value(3) == Value(3.0)`.

## Tables

A `Schema` lists the columns, and each option applies to the column added last:

| Method | Does |
|---|---|
| `add_integer(name)`, `add_float(name)`, `add_text(name)`, `add_blob(name)`, `add_bool(name)` | adds a column |
| `primary_key()` | makes it the primary key (not null, unique, with a hash index) |
| `auto_increment()` | an integer that counts up when it's left out |
| `not_null()`, `nullable()` | whether NULL is allowed |
| `unique()` | no two rows may share a value (NULLs don't count) |
| `default_value(v)` | used when the column is left out |

`Database` has `create_table(name, schema)`, `drop_table`, `has_table`, `tables()`, `create_index(table, columns, kind, unique)` and `drop_index`. `kind` is `"ordered"` (the default; for ranges, `LIKE 'ab%'`, sorting and `min`/`max`) or `"hash"` (for `=` and `is_in`).

A `Record` is a row to insert or a set of changes: `set(column, value)`, `set_null(column)`, `get`, `has`, `columns()`.

| Method | Does |
|---|---|
| `insert(table, record)` | adds a row and returns its row ID |
| `insert_many(table, records)` | adds them all in one transaction |
| `update(table, where, changes)` | changes every matching row; returns how many |
| `remove(table, where)` | deletes every matching row; returns how many |
| `get(table, key)` | the row with that primary key, as a `Result` |
| `find(table, where)`, `all(table)` | matching rows |
| `count(table)`, `count_where(table, where)` | how many rows |

## Conditions

`std::database::column("name")` starts a condition; `std::database::param(i)` is the `i`th value passed to `run_with` or a prepared statement.

| Method | Means |
|---|---|
| `eq(v)`, `ne(v)`, `lt(v)`, `le(v)`, `gt(v)`, `ge(v)` | `=`, `!=`, `<`, `<=`, `>`, `>=` against a value |
| `eq_expr(e)` and the other `_expr` forms | the same against another expression, such as `param(0)` |
| `between(low, high)` | `low <= x <= high` |
| `is_in(values)`, `not_in(values)` | one of a `Vector<Value>` |
| `is_null()`, `is_not_null()` | |
| `like(pattern)`, `ilike(pattern)`, `starts_with(prefix)` | `%` is any run of characters, `_` any one; `ilike` ignores case |
| `both(e)`, `either(e)`, `negate()` | AND, OR, NOT (also `std::database::all_of(a, b)` and `any_of(a, b)`) |
| `plus`, `minus`, `times`, `divided_by`, `modulo`, `lower`, `upper`, `length`, `absolute`, `coalesce(v)` | values worked out from columns |

Comparisons with NULL are unknown, as in SQL, so `column("x").eq(Value::null())` matches nothing; use `is_null()`. Values are always passed separately from the condition, never pasted into text, so there is nothing to inject.

## Queries

`Query("table")` builds a query; every method returns the query, so they chain:

| Method | Does |
|---|---|
| `where(condition)` | keeps matching rows (calling it again ANDs the conditions) |
| `select(columns)`, `select_column(name)`, `select_as(expr, name)` | which columns come back; all of them by default |
| `join(table, left_column, right_column)`, `left_join(...)`, `join_as(table, alias, ...)` | joins another table; name columns `"table.column"` when both tables have them |
| `count(name)`, `count_column(column, name)`, `count_distinct`, `sum`, `avg`, `min`, `max` | aggregates, named `name` in the result |
| `group_by(column)`, `having(condition)` | groups, and conditions on the aggregates by their names |
| `order_by(column, descending)`, `order_by_expr(expr, descending)` | sorting; NULLs come first |
| `limit(n)`, `offset(n)`, `distinct()` | |
| `no_cache()` | skips the result cache for this query |

`db.run(query)` returns a `Result`: `size()`, `columns()`, `get(row, column)`, `at(row, index)`, `row(i)` and `column(name)` (as `Vector<Value>`), `scalar()` (the first value), `to_json()`, `to_text()`, and how it ran: `plan()`, `used_index()`, `cache_hit()`, `rows_examined()`, `elapsed_us()`. `db.explain(query)` gives the plan without the rows, and `db.prepare(query)` gives a `Statement` to `run(params)` many times.

## Speed

The engine stores each column as a typed array, filters 64 rows at a time with bitmasks, and skips blocks of 1,024 rows whose minimum and maximum can't match. Hash indexes answer `=` and `is_in` directly; ordered indexes (B-trees) answer ranges, prefixes, `order_by` with `limit` (it stops after `limit` rows), and `count` and `min`/`max` without reading rows. The planner counts how many rows each index would give before choosing one, can merge two indexes for an `either`, and joins through an index or a hash table. A repeated query comes from a cache until one of its tables changes. With 200,000 rows, a hash lookup took 8 µs against 270 µs for a scan, and `order_by("price").limit(10)` took 6 µs from an index against 3.8 ms without one.

## Transactions

```walnut
bool: done = db.run_transaction([](std::database::Transaction: tx) -> bool {
    std::database::Record: change;
    change.set("balance", std::database::Value(40));
    tx.update("accounts", std::database::column("id").eq(std::database::Value(1)), change);
    return true;
});
```

Returning `true` commits, `false` rolls back, and a thrown error rolls back and keeps going up. `db.begin()` gives a `Transaction` to `commit()` or `rollback()` by hand. A transaction has `insert`, `update`, `remove`, `clear`, `create_table`, `create_index`, `drop_table` and `run`, and sees its own changes. One writer runs at a time; readers run together.

## Files

`Database::open("data/app")` keeps the database in two files: `data/app.fzdb`, a checksummed snapshot, and `data/app.wal`, a log of committed transactions. Opening replays the log, and a crash loses at most the transaction being written. `checkpoint()` writes a new snapshot and empties the log (it also happens on its own when the log grows past 64 MB). `Database::open_durable(path)` waits for the disk on every commit. `Database()` alone keeps everything in memory.

## Graphs

`db.graph_of(table, from_column, to_column, weight_column, directed)` reads an edge table as a graph, and is rebuilt only after the table changes:

| Method | Does |
|---|---|
| `bfs(start, max_depth)`, `bfs_depths(start, max_depth)` | nodes in breadth-first order, and how far each is |
| `dfs(start, max_depth)` | nodes in depth-first order |
| `shortest_path(source, target)`, `path_cost(source, target)` | fewest steps, or the lowest total weight when there is a weight column |
| `reachable(source, target)` | |
| `neighbors(node)`, `incoming(node)` | |
| `component_count()`, `component(i)` | connected groups, largest first |
| `topological_order()`, `has_cycle()` | an order where every edge points forward (empty when there is a cycle) |

Other things: `maintain()` tidies internal statistics after many changes, `set_cache(entries, rows)` sizes the result cache (`set_cache(0, 0)` turns it off), and `stats_json()` reports rows, memory, cache hits and commits.
