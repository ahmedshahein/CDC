#include "ir/graph.h"

#include <algorithm>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace opencdc::ir {

static std::string leaf_name(const std::string& hier_name) {
    size_t dot = hier_name.rfind('.');
    if (dot == std::string::npos)
        return hier_name;
    return hier_name.substr(dot + 1);
}

void Graph::set_limits(size_t max_nodes, size_t max_edges) {
    // Node IDs feed a 32-bit edge-key pack; keep the same bound the
    // static_assert enforces for the default constant.
    if (max_nodes > 0) {
        max_nodes_ = max_nodes > UINT32_MAX ? UINT32_MAX : max_nodes;
    }
    if (max_edges > 0)
        max_edges_ = max_edges;
}

uint64_t Graph::add_register(const std::string& hier_name, const std::string& clock_domain,
                             uint32_t width, const SourceLoc& loc, const std::string& module_path,
                             const std::string& module_type) {
    // Return existing node if hier_name already present — prevents orphaned
    // nodes when multiple always blocks assign to the same register.
    // Update empty fields so re-declaration with clock info wins over placeholders.
    auto existing = name_to_idx_.find(hier_name);
    if (existing != name_to_idx_.end()) {
        Node& n = nodes_[existing->second];
        if (n.clock_domain.empty() && !clock_domain.empty())
            n.clock_domain = clock_domain;
        if (n.module_path.empty() && !module_path.empty())
            n.module_path = module_path;
        if (n.module_type.empty() && !module_type.empty())
            n.module_type = module_type;
        if (n.width == 1 && width != 1)
            n.width = width;
        ++generation_;
        dirty_ = true;
        return n.id;
    }

    if (nodes_.size() >= max_nodes_) {
        truncated_ = true;
        return 0;
    }
    if (next_id_ == 0) {
        truncated_ = true;
        return 0;
    }
    uint64_t id = next_id_++;
    size_t idx = nodes_.size();
    Node node;
    node.id = id;
    node.hier_name = hier_name;
    node.short_name = leaf_name(hier_name);
    node.module_path = module_path;
    node.module_type = module_type;
    node.kind = NodeKind::Register;
    node.width = width;
    node.clock_domain = clock_domain;
    node.loc = loc;
    nodes_.push_back(node);
    id_to_idx_[id] = idx;
    name_to_idx_[hier_name] = idx;
    ++generation_;
    dirty_ = true;
    return id;
}

uint64_t Graph::add_port(const std::string& hier_name, uint32_t width, const SourceLoc& loc,
                         const std::string& module_path, const std::string& module_type) {
    auto existing = name_to_idx_.find(hier_name);
    if (existing != name_to_idx_.end()) {
        return nodes_[existing->second].id;
    }

    if (nodes_.size() >= max_nodes_) {
        truncated_ = true;
        return 0;
    }
    if (next_id_ == 0) {
        truncated_ = true;
        return 0;
    }
    uint64_t id = next_id_++;
    size_t idx = nodes_.size();
    Node node;
    node.id = id;
    node.hier_name = hier_name;
    node.short_name = leaf_name(hier_name);
    node.module_path = module_path;
    node.module_type = module_type;
    node.kind = NodeKind::Port;
    node.width = width;
    node.loc = loc;
    nodes_.push_back(node);
    id_to_idx_[id] = idx;
    name_to_idx_[hier_name] = idx;
    ++generation_;
    dirty_ = true;
    return id;
}

uint64_t Graph::add_net(const std::string& hier_name, uint32_t width, const SourceLoc& loc,
                        const std::string& module_path, const std::string& module_type) {
    auto existing = name_to_idx_.find(hier_name);
    if (existing != name_to_idx_.end()) {
        return nodes_[existing->second].id;
    }

    if (nodes_.size() >= max_nodes_) {
        truncated_ = true;
        return 0;
    }
    if (next_id_ == 0) {
        truncated_ = true;
        return 0;
    }
    uint64_t id = next_id_++;
    size_t idx = nodes_.size();
    Node node;
    node.id = id;
    node.hier_name = hier_name;
    node.short_name = leaf_name(hier_name);
    node.module_path = module_path;
    node.module_type = module_type;
    node.kind = NodeKind::Net;
    node.width = width;
    node.loc = loc;
    nodes_.push_back(node);
    id_to_idx_[id] = idx;
    name_to_idx_[hier_name] = idx;
    ++generation_;
    dirty_ = true;
    return id;
}

uint64_t Graph::add_combinational(const std::string& hier_name, LogicType logic_type,
                                  const std::vector<uint64_t>& inputs, uint32_t width,
                                  const SourceLoc& loc, const std::string& module_path,
                                  const std::string& module_type) {
    auto existing = name_to_idx_.find(hier_name);
    if (existing != name_to_idx_.end()) {
        return nodes_[existing->second].id;
    }

    if (nodes_.size() >= max_nodes_) {
        truncated_ = true;
        return 0;
    }
    if (next_id_ == 0) {
        truncated_ = true;
        return 0;
    }
    uint64_t id = next_id_++;
    size_t idx = nodes_.size();
    Node node;
    node.id = id;
    node.hier_name = hier_name;
    node.short_name = leaf_name(hier_name);
    node.module_path = module_path;
    node.module_type = module_type;
    node.kind = NodeKind::Combinational;
    node.width = width;
    node.loc = loc;
    node.logic_type = logic_type;
    node.logic_inputs = inputs;
    nodes_.push_back(node);
    id_to_idx_[id] = idx;
    name_to_idx_[hier_name] = idx;
    ++generation_;
    dirty_ = true;

    for (uint64_t input_id : inputs) {
        add_edge(input_id, id);
    }

    return id;
}

EdgeRole Graph::edge_role(uint64_t from_id, uint64_t to_id) const {
    if (from_id > UINT32_MAX || to_id > UINT32_MAX)
        return EdgeRole::Data;
    auto it = edge_roles_.find((from_id << 32) | to_id);
    return it == edge_roles_.end() ? EdgeRole::Data : it->second;
}

void Graph::add_edge(uint64_t from_id, uint64_t to_id) {
    add_edge(from_id, to_id, EdgeRole::Data);
}

void Graph::add_edge(uint64_t from_id, uint64_t to_id, EdgeRole role) {
    if (from_id == to_id)
        return;  // no self-loops
    if (!find_node(from_id) || !find_node(to_id))
        return;
    if (edges_.size() >= max_edges_) {
        truncated_ = true;
        return;
    }

    // O(1) dedup using packed edge key instead of O(N) std::find.
    // IDs beyond 32 bits cannot pack safely — fall back to linear check.
    if (from_id > UINT32_MAX || to_id > UINT32_MAX) {
        for (const auto& e : edges_) {
            if (e.from_id == from_id && e.to_id == to_id)
                return;
        }
    } else {
        uint64_t edge_key = (from_id << 32) | to_id;
        if (!edge_set_.insert(edge_key).second) {
            // An existing edge that is also data is a Data edge.
            if (role == EdgeRole::Data && edge_roles_.erase(edge_key)) {
                for (auto& e : edges_) {
                    if (e.from_id == from_id && e.to_id == to_id)
                        e.role = EdgeRole::Data;
                }
                ++generation_;
            }
            return;
        }
        if (role != EdgeRole::Data)
            edge_roles_[edge_key] = role;
    }

    edges_.push_back({from_id, to_id, role});
    adj_[from_id].push_back(to_id);
    radj_[to_id].push_back(from_id);
    ++generation_;
    dirty_ = true;
}

const Node* Graph::find_node(uint64_t id) const {
    auto it = id_to_idx_.find(id);
    if (it == id_to_idx_.end())
        return nullptr;
    return &nodes_[it->second];
}

void Graph::remove_nodes(const std::vector<uint64_t>& dead_ids) {
    if (dead_ids.empty())
        return;
    std::unordered_set<uint64_t> dead(dead_ids.begin(), dead_ids.end());

    // Drop dead nodes; surviving node ids are unchanged (only their vector
    // index shifts), so every id reference in Node fields stays valid.
    std::vector<Node> kept;
    kept.reserve(nodes_.size());
    for (auto& n : nodes_) {
        if (dead.count(n.id))
            continue;
        kept.push_back(std::move(n));
    }
    nodes_ = std::move(kept);

    // Rebuild node indices.
    id_to_idx_.clear();
    name_to_idx_.clear();
    id_to_idx_.reserve(nodes_.size());
    name_to_idx_.reserve(nodes_.size());
    for (size_t i = 0; i < nodes_.size(); ++i) {
        id_to_idx_[nodes_[i].id] = i;
        name_to_idx_[nodes_[i].hier_name] = i;
    }

    // Drop edges incident to dead nodes and rebuild adjacency + dedup set.
    std::vector<Edge> kept_edges;
    kept_edges.reserve(edges_.size());
    for (const auto& e : edges_) {
        if (dead.count(e.from_id) || dead.count(e.to_id))
            continue;
        kept_edges.push_back(e);
    }
    edges_ = std::move(kept_edges);
    adj_.clear();
    radj_.clear();
    edge_set_.clear();
    edge_roles_.clear();
    for (const auto& e : edges_) {
        adj_[e.from_id].push_back(e.to_id);
        radj_[e.to_id].push_back(e.from_id);
        if (e.from_id <= UINT32_MAX && e.to_id <= UINT32_MAX) {
            edge_set_.insert((e.from_id << 32) | e.to_id);
            if (e.role != EdgeRole::Data)
                edge_roles_[(e.from_id << 32) | e.to_id] = e.role;
        }
    }
    mark_dirty();
}

Node* Graph::find_node_mutable(uint64_t id) {
    auto it = id_to_idx_.find(id);
    if (it == id_to_idx_.end())
        return nullptr;
    return &nodes_[it->second];
}

const Node* Graph::find_node_by_name(const std::string& hier_name) const {
    auto it = name_to_idx_.find(hier_name);
    if (it == name_to_idx_.end())
        return nullptr;
    return &nodes_[it->second];
}

std::vector<uint64_t> Graph::successors(uint64_t id) const {
    auto it = adj_.find(id);
    if (it == adj_.end())
        return {};
    return it->second;
}

std::vector<uint64_t> Graph::predecessors(uint64_t id) const {
    auto it = radj_.find(id);
    if (it == radj_.end())
        return {};
    return it->second;
}

size_t Graph::register_count() const {
    return std::count_if(nodes_.begin(), nodes_.end(),
                         [](const Node& n) { return n.kind == NodeKind::Register; });
}

std::vector<uint64_t> Graph::register_predecessors(uint64_t id, bool traverse_combinational,
                                                   bool data_only) const {
    std::vector<uint64_t> result;
    std::vector<std::pair<uint64_t, uint64_t>> stack;  // (node, reached from)
    for (uint64_t x : predecessors(id))
        stack.push_back({x, id});
    std::unordered_set<uint64_t> visited;
    visited.insert(id);

    while (!stack.empty()) {
        auto [cur, from] = stack.back();
        stack.pop_back();
        if (data_only && edge_role(cur, from) != EdgeRole::Data)
            continue;
        if (visited.count(cur))
            continue;
        visited.insert(cur);

        const Node* n = find_node(cur);
        if (!n)
            continue;

        if (n->kind == NodeKind::Register) {
            result.push_back(cur);
        } else if (is_data_node(n->kind) &&
                   (traverse_combinational || n->kind != NodeKind::Combinational)) {
            for (uint64_t x : predecessors(cur)) {
                if (!visited.count(x))
                    stack.push_back({x, cur});
            }
        }
    }
    return result;
}

std::vector<uint64_t> Graph::register_successors(uint64_t id, bool traverse_combinational,
                                                 bool data_only) const {
    std::vector<uint64_t> result;
    std::vector<std::pair<uint64_t, uint64_t>> stack;  // (node, reached from)
    for (uint64_t x : successors(id))
        stack.push_back({x, id});
    std::unordered_set<uint64_t> visited;
    visited.insert(id);

    while (!stack.empty()) {
        auto [cur, from] = stack.back();
        stack.pop_back();
        if (data_only && edge_role(from, cur) != EdgeRole::Data)
            continue;
        if (visited.count(cur))
            continue;
        visited.insert(cur);

        const Node* n = find_node(cur);
        if (!n)
            continue;

        if (n->kind == NodeKind::Register) {
            result.push_back(cur);
        } else if (is_data_node(n->kind) &&
                   (traverse_combinational || n->kind != NodeKind::Combinational)) {
            for (uint64_t x : successors(cur)) {
                if (!visited.count(x))
                    stack.push_back({x, cur});
            }
        }
    }
    return result;
}

Graph::PathTraversalResult Graph::find_register_paths(uint64_t src_reg_id, size_t max_paths) const {
    PathTraversalResult result;
    result.max_paths = max_paths;

    // Rope-style traversal: each Frame carries an O(1) hash-set for cycle
    // detection instead of an O(D) vector scan.  The full path is materialised
    // only at register-to-register endpoints by walking a compact parent-pointer
    // array (rope), so we avoid copying the whole path vector per frame.
    struct RopeNode {
        uint64_t node_id;
        int parent_idx;  // index into rope[], -1 for root
    };
    std::vector<RopeNode> rope;
    rope.push_back({src_reg_id, -1});

    // Each data node is expanded once and each destination recorded once,
    // which also breaks every cycle: a frame carries only its depth.
    struct Frame {
        uint64_t node_id;
        int rope_idx;  // index of this node in rope[]
        size_t depth;  // nodes on the path, the source included
    };

    // Breadth-first: with each node expanded once, the recorded path to a
    // destination is a shortest one, and MAX_PATH_DEPTH bounds its length.
    std::deque<Frame> stack;
    stack.push_back({src_reg_id, 0, 1});
    result.visited_nodes = 1;

    // Local per-call edge tracking (not a member — must stay local)
    std::unordered_set<uint64_t> visited_edges;
    // Each data node is expanded once and each destination recorded once:
    // enumerating every simple path is exponential in the combinational
    // fan-in/fan-out (a CPU core hit max_paths from one register). The edges
    // the expansion meets are kept, so the properties of *every* path to a
    // destination are computed afterwards (below), not those of the path met
    // first.
    std::unordered_set<uint64_t> expanded;
    std::unordered_set<uint64_t> reached;
    std::vector<std::pair<uint64_t, uint64_t>> cone_edges;
    bool stop = false;

    while (!stack.empty() && !stop) {
        Frame f = std::move(stack.front());
        stack.pop_front();
        if (!expanded.insert(f.node_id).second)
            continue;

        // E3: Depth limit
        if (f.depth > MAX_PATH_DEPTH) {
            result.truncated = true;
            result.truncated_by_depth = true;
            continue;
        }

        for (uint64_t succ : successors(f.node_id)) {
            const Node* sn = find_node(succ);
            if (!sn)
                continue;
            if ((sn->kind == NodeKind::Register && succ != src_reg_id) || is_data_node(sn->kind))
                cone_edges.emplace_back(f.node_id, succ);
            if (succ == src_reg_id)
                continue;  // back to the source

            result.visited_nodes++;

            // Add successor to rope
            int succ_rope_idx = static_cast<int>(rope.size());
            rope.push_back({succ, f.rope_idx});

            uint64_t edge_key =
                (static_cast<uint64_t>(f.node_id) << 32) | static_cast<uint64_t>(succ);
            if (visited_edges.insert(edge_key).second) {
                result.visited_edges++;
            }

            if (sn->kind == NodeKind::Register) {
                if (!reached.insert(succ).second)
                    continue;
                // Materialise path only at register endpoints
                RegPath rp;
                rp.src_reg_id = src_reg_id;
                rp.dst_reg_id = succ;
                // Walk rope backward to build node_ids in order
                rp.node_ids.reserve(f.depth + 1);
                int idx = succ_rope_idx;
                while (idx >= 0) {
                    rp.node_ids.push_back(rope[static_cast<size_t>(idx)].node_id);
                    idx = rope[static_cast<size_t>(idx)].parent_idx;
                }
                std::reverse(rp.node_ids.begin(), rp.node_ids.end());
                result.paths.push_back(std::move(rp));
                if (result.paths.size() >= max_paths) {
                    result.truncated = true;
                    stop = true;
                    break;
                }
            } else if (is_data_node(sn->kind) && !expanded.count(succ)) {
                Frame nf;
                nf.node_id = succ;
                nf.rope_idx = succ_rope_idx;
                nf.depth = f.depth + 1;
                stack.push_back(std::move(nf));
            }
        }
    }

    // A truncated traversal has not seen every path: no node is known to be
    // on all of them, and any of them may pass logic.
    if (result.truncated) {
        for (auto& rp : result.paths) {
            rp.common_node_ids = {rp.src_reg_id, rp.dst_reg_id};
            rp.any_path_through_logic = true;
        }
        return result;
    }

    // The cone: the source, the expanded data nodes, the reached registers.
    std::unordered_map<uint64_t, size_t> index;
    std::vector<uint64_t> cone{src_reg_id};
    index.emplace(src_reg_id, 0);
    auto add_to_cone = [&](uint64_t id) {
        if (index.emplace(id, cone.size()).second)
            cone.push_back(id);
    };
    for (const auto& [from, to] : cone_edges) {
        add_to_cone(from);
        if (expanded.count(to) || reached.count(to))
            add_to_cone(to);
    }
    const size_t n = cone.size();
    std::vector<std::vector<size_t>> succs(n), preds(n);
    for (const auto& [from, to] : cone_edges) {
        auto t = index.find(to);
        if (t == index.end())
            continue;
        size_t u = index.at(from);
        succs[u].push_back(t->second);
        preds[t->second].push_back(u);
    }

    // Postorder numbers from the source (iterative DFS).
    std::vector<int> post(n, -1);
    {
        std::vector<bool> seen(n, false);
        std::vector<std::pair<size_t, size_t>> dfs{{0, 0}};
        seen[0] = true;
        int counter = 0;
        while (!dfs.empty()) {
            auto& [v, next] = dfs.back();
            if (next < succs[v].size()) {
                size_t w = succs[v][next++];
                if (!seen[w]) {
                    seen[w] = true;
                    dfs.emplace_back(w, 0);
                }
            } else {
                post[v] = counter++;
                dfs.pop_back();
            }
        }
    }
    std::vector<size_t> rpo;
    rpo.reserve(n);
    for (size_t v = 0; v < n; ++v)
        if (post[v] >= 0)
            rpo.push_back(v);
    std::sort(rpo.begin(), rpo.end(), [&](size_t a, size_t b) { return post[a] > post[b]; });

    // Immediate dominators (Cooper, Harvey, Kennedy, "A Simple, Fast
    // Dominance Algorithm"): every path from the source to v passes idom[v].
    const size_t none = n;
    std::vector<size_t> idom(n, none);
    idom[0] = 0;
    auto intersect = [&](size_t a, size_t b) {
        while (a != b) {
            while (post[a] < post[b])
                a = idom[a];
            while (post[b] < post[a])
                b = idom[b];
        }
        return a;
    };
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t v : rpo) {
            if (v == 0)
                continue;
            size_t new_idom = none;
            for (size_t p : preds[v]) {
                if (idom[p] == none)
                    continue;
                new_idom = new_idom == none ? p : intersect(p, new_idom);
            }
            if (new_idom != idom[v]) {
                idom[v] = new_idom;
                changed = true;
            }
        }
    }

    // Nodes reached from logic (a data node other than a net or a port).
    std::vector<bool> after_logic(n, false);
    {
        std::vector<size_t> work;
        for (size_t v = 1; v < n; ++v) {
            const Node* node = find_node(cone[v]);
            if (node && is_data_node(node->kind) && node->kind != NodeKind::Net &&
                node->kind != NodeKind::Port) {
                after_logic[v] = true;
                work.push_back(v);
            }
        }
        while (!work.empty()) {
            size_t v = work.back();
            work.pop_back();
            for (size_t w : succs[v]) {
                if (!after_logic[w]) {
                    after_logic[w] = true;
                    work.push_back(w);
                }
            }
        }
    }

    for (auto& rp : result.paths) {
        size_t d = index.at(rp.dst_reg_id);
        rp.any_path_through_logic = after_logic[d];
        for (size_t v = d; idom[v] != none; v = idom[v]) {
            rp.common_node_ids.push_back(cone[v]);
            if (v == 0)
                break;
        }
        std::reverse(rp.common_node_ids.begin(), rp.common_node_ids.end());
    }

    return result;
}

ValidationResult Graph::validate() const {
    ValidationResult result;

    for (const auto& [id, index] : id_to_idx_) {
        if (index >= nodes_.size() || nodes_[index].id != id) {
            result.errors.push_back("Invalid node index for id " + std::to_string(id));
        }
    }

    std::unordered_set<std::string> names;
    for (const auto& node : nodes_) {
        if (node.width == 0) {
            result.errors.push_back("Node '" + node.hier_name + "' has zero width");
        }
        if (!names.insert(node.hier_name).second) {
            result.errors.push_back("Duplicate node name '" + node.hier_name + "'");
        }
    }

    for (const auto& edge : edges_) {
        if (!find_node(edge.from_id) || !find_node(edge.to_id)) {
            result.errors.push_back("Dangling edge " + std::to_string(edge.from_id) + " -> " +
                                    std::to_string(edge.to_id));
        }
    }

    return result;
}

}  // namespace opencdc::ir
