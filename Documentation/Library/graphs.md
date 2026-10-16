# graphs

`graphs` has graphs made of nodes and weighted edges, paths through them, and the usual searches: breadth-first and depth-first search (which also finds loops), shortest paths, A*, components, topological order, spanning trees and more.

```walnut
import module.graphs
```

Everything is in the namespace `std::graphs`. The names match Fizmo's graph library, so code moves easily between the two.

## A first example

```walnut
import module.io
import module.graphs

using std::graphs::Graph;

function main() -> int {
    Graph<string>: roads;
    auto: home = roads.add_node("home");
    auto: shop = roads.add_node("shop");
    auto: park = roads.add_node("park");
    auto: work = roads.add_node("work");
    roads.add_edge(home, shop, 4.0);
    roads.add_edge(home, park, 1.0);
    roads.add_edge(park, shop, 2.0);
    roads.add_edge(shop, work, 5.0);

    auto: route = roads.shortest_path(home, work);
    std::io::println(route, " costs ", route.cost());
    std::io::println(roads.breadth_first_search(home).depth_of(work), " hops at least");
    std::io::println("loops? ", roads.depth_first_search(home).has_cycle);
    return 0;
} end function main
```

This prints `0 -> 2 -> 1 -> 3 costs 8`, then `2 hops at least`, then `loops? 1` (home, park and shop form a loop).

## Graphs, nodes and edges

`Graph<T, W>` holds a value of type `T` at each node, and each edge has a weight of type `W` (`float` if left out). A node is named by its id, a `long int` handed out by `add_node` in order from 0.

A `Graph` is undirected unless it is made directed: `Graph<T>(Direction::directed)`, `Graph<T>::directed()` or `std::graphs::directed_graph<T>()`. In an undirected graph `add_edge(a, b)` connects both ways.

| Method | Does |
|---|---|
| `add_node(value)`, `add_nodes(values)` | Adds nodes and gives their ids |
| `add_edge(source, target, weight = 1)`, `connect(...)` | Adds an edge |
| `remove_edge(source, target)` | Removes it, and says whether there was one |
| `set_weight(source, target, weight)` | Changes the weight, adding the edge if needed |
| `has_edge(source, target)`, `weight(source, target)` | Checks an edge, and gives its weight (throws when there is none) |
| `contains(node)` | Whether the id is a node of this graph |
| `node_count()`, `size()`, `edge_count()`, `empty()`, `is_directed()` | Sizes |
| `value(node)`, `g[node]` | The node's value, which can be changed |
| `values()`, `nodes()` | Every value, and every id |
| `find(value)`, `find_first(value)` | The ids holding a value (`find_first` gives -1 when there is none) |
| `edges()`, `edges_from(node)`, `edges_to(node)` | Edges as `Edge<W>` values |
| `neighbors(node)`, `predecessors(node)` | The nodes an edge leads to, and the nodes with an edge leading here |
| `out_degree(node)`, `in_degree(node)`, `degree(node)` | Edge counts |
| `reversed()`, `undirected_copy()` | The graph with every edge turned around, and an undirected copy |
| `clear()`, `to_string()` | Empties it, and lists it as text |

An `Edge<W>` has the fields `source`, `target` and `weight`, and the methods `reversed()` and `to_string()`.

Using an id that isn't in the graph throws a `std::errors::graph_error`.

## Paths

A `Path<W>` is a list of node ids and the total cost of the edges between them. A search that finds nothing gives an empty path, so check with `found()` (or use the path as a condition: `if (p) then { ... }`).

| Method | Gives |
|---|---|
| `found()`, `empty()` | Whether there is a path |
| `nodes()`, `p[i]`, `begin()`/`end()` | The node ids |
| `cost()` | The total weight |
| `node_count()`, `edge_count()`, `size()` | The number of nodes, and of edges (one fewer) |
| `start()`, `goal()`, `front()`, `back()` | The first and last nodes |
| `contains(node)`, `is_simple()`, `is_cycle()` | Whether a node is on it, whether no node repeats, and whether it ends where it starts |
| `extend(node, step_cost)`, `reversed()` | Builds paths |
| `to_string()` | Text such as `0 -> 2 -> 1` |

`g.path_values(path)` gives the node values along a path, and `g.path_is_valid(path)` checks that every step is an edge of `g`.

## Searches

`breadth_first_search(start)` and `depth_first_search(start)` give a `Traversal`:

| Field or method | Holds |
|---|---|
| `order` | The nodes in the order they were reached |
| `depth`, `depth_of(node)` | How many edges from the start each node is in the search tree (-1 when not reached) |
| `parent`, `parent_of(node)` | Each node's parent in the search tree (-1 for the start and unreached nodes) |
| `reached(node)`, `count()` | Whether a node was reached, and how many were |
| `path_to(node)` | The search tree's path from the start, as a list of ids |
| `has_cycle`, `cycle` | Only filled by `depth_first_search`: whether a loop can be reached from the start, and the first one found, as a list of ids that begins and ends at the same node |
| `loop_count()`, `loop(i)`, `loops()` | Only filled by `depth_first_search`: every loop it ran into, one for each edge that leads back to a node still being explored, as lists like `cycle` |

`depth_first_search` keeps track of the nodes it has visited as it goes, so it never visits a node twice and never runs around a loop forever, and it notes each loop it finds while it searches. There is nothing separate to call first: one search gives both the visit order and the loops. `find_cycle()` and `has_cycle()` on the graph are shortcuts that run it for you.

Depth-first search tells loops apart properly in both kinds of graph. In a directed graph an edge back to a node that is still being explored is a loop. In an undirected graph the edge just walked along doesn't count, but a second edge to the same node, or a self-loop, does.

```walnut
import module.io
import module.graphs

function main() -> int {
    auto: g = std::graphs::directed_graph<string>();
    g.add_nodes(std::collections::Vector<string>("a", "b", "c", "d"));
    g.add_edge(0, 1);
    g.add_edge(1, 2);
    g.add_edge(2, 3);
    std::io::println(g.depth_first_search(0).has_cycle);
    g.add_edge(3, 1);
    auto: t = g.depth_first_search(0);
    std::io::println(t.has_cycle, " ", t.cycle.size(), " ", g.find_cycle());
    return 0;
} end function main
```

This prints `0`, then `1 4 1 -> 2 -> 3 -> 1`. With one more edge, `g.add_edge(2, 0)`, the search would also report the loop `0 -> 1 -> 2 -> 0`, and `t.loop_count()` would be `2`.

Both searches also take a function, `(long int: node, long int: depth) -> bool`, called for each node as it is reached. Returning `false` stops the search:

```walnut
import module.io
import module.graphs

function main() -> int {
    std::graphs::Graph<int>: g;
    for (int: i = 0; i < 6; i++) {
        g.add_node(i);
    }
    for (int: i = 1; i < 6; i++) {
        g.add_edge(i - 1, i);
    }
    auto: near = g.breadth_first_search(0, [](long int: node, long int: depth) -> bool { return depth < 2; });
    std::io::println(near.count());
    return 0;
} end function main
```

This prints `3`: the search stops at the first node two edges away.

| Method | Gives |
|---|---|
| `depth_first_postorder(start)` | The nodes in the order the search finished with them |
| `breadth_first_path(start, goal)` | A path with the fewest edges |
| `depth_first_path(start, goal)` | The first path depth-first search finds |
| `reachable(source, target)` | Whether there is any path |
| `hop_distances(start)` | The fewest edges to each node (-1 when unreachable) |

### Parallel breadth-first search

`parallel_breadth_first_search(start)` gives the same kind of `Traversal` as `breadth_first_search`, but splits the work across threads. It goes one level at a time: the nodes found so far are divided among the threads, and each thread follows their edges. When two threads reach the same new node, the first one to get there claims it and the other one drops that path, so every node is found exactly once and nothing races.

```walnut
import module.io
import module.graphs

function main() -> int {
    auto: g = std::graphs::directed_graph<int>();
    for (int: i = 0; i < 1000; i++) {
        g.add_node(i);
    }
    for (int: i = 0; i < 999; i++) {
        g.add_edge(i, i + 1);
        g.add_edge(i, (i * 7 + 3) % 1000);
    }
    auto: plain = g.breadth_first_search(0);
    auto: fast = g.parallel_breadth_first_search(0);
    std::io::println(plain.count(), " ", fast.count(), " ", plain.depth == fast.depth);
    return 0;
} end function main
```

This prints `1000 1000 1`.

- `parallel_breadth_first_search(start, threads)` uses that many threads; the default, `0`, uses one per processor core. A level with fewer than 512 nodes is handled by one thread, because starting threads would cost more than it saves.
- `depth` and `count()` are always the same as `breadth_first_search` gives, so it still finds the fewest edges to every node. `parent` always points to a node one level up, but when several nodes on that level lead to the same node, which one becomes its parent can change from run to run, and so can the order of nodes within one level of `order`.
- `parallel_breadth_first_search(start, visit)` and `(start, visit, threads)` take a function like `breadth_first_search`'s. It is called on the calling thread, one node at a time, so it doesn't need to be thread-safe. When it returns `false`, the search finishes the level it is on and stops there.
- It pays off on large graphs: on a 200,000-node graph with 1.6 million edges, two threads took about 90 ms where `breadth_first_search` took about 145 ms. Each extra core helps further.

## Shortest paths

| Method | Gives |
|---|---|
| `shortest_path(start, goal)` | The path of least total weight. It uses Dijkstra's algorithm, or Bellman–Ford when some weight is negative, and throws if there is a negative loop |
| `shortest_distances(start)` | A `Distances<W>` with `distance`, `reached` and `parent` for every node, `has_negative_cycle`, and the methods `reachable(node)`, `distance_to(node)` and `path_to(node)` |
| `bellman_ford(start)` | The same, always by Bellman–Ford |
| `a_star(start, goal, heuristic)` | The least-weight path, guided by `heuristic`, a function `(long int: node) -> W` that estimates the remaining cost without overestimating it |
| `all_simple_paths(start, goal, max_edges = -1)` | Every path that doesn't repeat a node, optionally limited to `max_edges` edges |

## Structure

| Method | Gives |
|---|---|
| `connected_components()` | The groups of nodes joined by edges, ignoring direction |
| `is_connected()` | Whether there is just one group |
| `strongly_connected_components()` | The groups where every node can reach every other, following direction |
| `topological_sort()` | The nodes of a directed graph with every edge pointing forward. Throws when the graph has a loop |
| `find_cycle()`, `has_cycle()`, `is_acyclic()` | A loop anywhere in the graph (an empty path when there is none), and whether there is one |
| `is_tree()`, `is_bipartite()` | Whether it is connected with no loops, and whether its nodes split into two groups with every edge going between them |
| `minimum_spanning_tree()` | The edges of a minimum spanning tree (a forest, when the graph isn't connected), by Kruskal's algorithm |
