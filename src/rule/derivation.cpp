// SPDX-License-Identifier: Apache-2.0
#include "pv/rule/derivation.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include <fmt/format.h>

namespace pv {
namespace {

bool active_at(const PointerSnapshot& pointer, Epoch epoch) noexcept {
    return pointer.born_at <= epoch && (!pointer.expires_at.has_value() || epoch < *pointer.expires_at);
}

std::string strip_comment(std::string_view line) {
    const auto marker = line.find('#');
    auto value = marker == std::string_view::npos ? line : line.substr(0, marker);
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return std::string{value.substr(begin, end - begin)};
}

// Parse "link FROM_VAR -> TO_VAR : RELATION" from a stream positioned after the
// leading keyword (from/make). Leaves the stream positioned after RELATION so the
// caller can read trailing key=value options.
void parse_atom(std::istringstream& stream, std::string& from, std::string& relation, std::string& to) {
    std::string link;
    std::string arrow;
    std::string colon;
    stream >> link >> from >> arrow >> to >> colon >> relation;
    if (link != "link" || from.empty() || arrow != "->" || to.empty() || colon != ":" || relation.empty()) {
        throw std::invalid_argument("usage: link FROM_VAR -> TO_VAR : RELATION");
    }
}

bool body_binds(const Derivation& derivation, const std::string& var) {
    for (const auto& atom : derivation.body) {
        if (atom.from_var == var || atom.to_var == var) {
            return true;
        }
    }
    return false;
}

// Find an existing relation by name, otherwise allocate the next id and emit an
// intern op into the delta, caching the allocation for repeated heads.
class RelationInterner {
public:
    RelationInterner(const WorldSnapshot& snapshot, Delta& delta) : snapshot_(snapshot), delta_(delta) {
        for (const auto& [id, name] : snapshot_.relation_names) {
            next_ = std::max(next_, id + 1);
        }
    }

    RelationType intern(const std::string& name) {
        for (const auto& [id, candidate] : snapshot_.relation_names) {
            if (candidate == name) {
                return RelationType{id};
            }
        }
        if (const auto iter = cache_.find(name); iter != cache_.end()) {
            return iter->second;
        }
        const RelationType relation{next_++};
        delta_.append_intern_relation(name, relation);
        cache_.emplace(name, relation);
        return relation;
    }

private:
    const WorldSnapshot& snapshot_;
    Delta& delta_;
    std::unordered_map<std::string, RelationType> cache_;
    std::uint32_t next_{1};
};

using FactIndex = std::map<std::string, std::set<std::pair<ObjectId, ObjectId>>>;

// Recursively join the body atoms on shared variables. The visitor sees one
// (from, to) pair per fully bound head. The fact index is never mutated here, so
// it is safe even when a head relation equals a body relation (transitive closure).
void evaluate_body(
    const Derivation& derivation,
    std::size_t atom_index,
    std::unordered_map<std::string, ObjectId>& bindings,
    const FactIndex& facts,
    const std::function<void(ObjectId, ObjectId)>& visit_head) {
    if (atom_index == derivation.body.size()) {
        const auto from_iter = bindings.find(derivation.head.from_var);
        const auto to_iter = bindings.find(derivation.head.to_var);
        if (from_iter != bindings.end() && to_iter != bindings.end()) {
            visit_head(from_iter->second, to_iter->second);
        }
        return;
    }

    const auto& atom = derivation.body[atom_index];
    const auto facts_iter = facts.find(atom.relation);
    if (facts_iter == facts.end()) {
        return;
    }

    for (const auto& [from, to] : facts_iter->second) {
        std::vector<std::string> added;
        auto try_bind = [&](const std::string& var, ObjectId id) {
            const auto iter = bindings.find(var);
            if (iter == bindings.end()) {
                bindings.emplace(var, id);
                added.push_back(var);
                return true;
            }
            return iter->second == id;
        };
        if (try_bind(atom.from_var, from) && try_bind(atom.to_var, to)) {
            evaluate_body(derivation, atom_index + 1, bindings, facts, visit_head);
        }
        for (const auto& var : added) {
            bindings.erase(var);
        }
    }
}

struct DerivedEdge {
    ObjectId from;
    ObjectId to;
    std::string relation;
    CausalRole role;
    double weight;
};

}  // namespace

bool DerivationBuilder::active() const noexcept {
    return active_;
}

const std::string& DerivationBuilder::name() const noexcept {
    return draft_.name;
}

std::optional<Derivation> DerivationBuilder::consume_line(std::string_view raw_line) {
    const auto line = strip_comment(raw_line);
    if (line.empty()) {
        return std::nullopt;
    }

    std::istringstream stream{line};
    std::string command;
    stream >> command;

    if (command == "derive") {
        if (active_) {
            throw std::invalid_argument(fmt::format("derivation '{}' is incomplete (missing make)", draft_.name));
        }
        std::string name;
        stream >> name;
        if (name.empty()) {
            throw std::invalid_argument("usage: derive NAME");
        }
        draft_ = Derivation{};
        draft_.name = name;
        active_ = true;
        return std::nullopt;
    }

    if (!active_) {
        throw std::invalid_argument("derivation clause found before derive NAME");
    }

    if (command == "from") {
        DeriveAtom atom;
        parse_atom(stream, atom.from_var, atom.relation, atom.to_var);
        draft_.body.push_back(std::move(atom));
        return std::nullopt;
    }

    if (command == "make") {
        if (draft_.body.empty()) {
            throw std::invalid_argument(fmt::format("derivation '{}' needs at least one from clause", draft_.name));
        }
        DeriveHead head;
        parse_atom(stream, head.from_var, head.relation, head.to_var);
        std::string option;
        while (stream >> option) {
            const auto separator = option.find('=');
            if (separator == std::string::npos) {
                continue;
            }
            const auto key = option.substr(0, separator);
            const auto value = option.substr(separator + 1);
            if (key == "weight") {
                head.weight = std::stod(value);
            } else if (key == "role" || key == "causal_role") {
                head.role = causal_role_from_string(value);
            }
        }
        if (!body_binds(draft_, head.from_var)) {
            throw std::invalid_argument(
                fmt::format("derivation '{}' head variable '{}' is not bound by any from clause", draft_.name, head.from_var));
        }
        if (!body_binds(draft_, head.to_var)) {
            throw std::invalid_argument(
                fmt::format("derivation '{}' head variable '{}' is not bound by any from clause", draft_.name, head.to_var));
        }
        draft_.head = std::move(head);
        auto completed = std::move(draft_);
        reset();
        return completed;
    }

    throw std::invalid_argument(fmt::format("unknown derivation clause '{}'", command));
}

void DerivationBuilder::reset() noexcept {
    draft_ = Derivation{};
    active_ = false;
}

bool is_derivation_command(std::string_view command) noexcept {
    return command == "derive" || command == "from" || command == "make";
}

std::vector<Derivation> parse_derivations(std::string_view text) {
    std::vector<Derivation> out;
    DerivationBuilder builder;
    std::istringstream input{std::string{text}};
    std::string line;
    while (std::getline(input, line)) {
        if (auto derivation = builder.consume_line(line); derivation.has_value()) {
            out.push_back(std::move(*derivation));
        }
    }
    if (builder.active()) {
        throw std::invalid_argument(fmt::format("derivation '{}' is incomplete", builder.name()));
    }
    return out;
}

DerivationEvolution::DerivationEvolution(std::vector<Derivation> derivations)
    : derivations_(std::move(derivations)) {}

Delta DerivationEvolution::step(const WorldSnapshot& snapshot, Epoch next) const {
    Delta delta;
    if (derivations_.empty()) {
        return delta;
    }

    // Clear the edges derived on the previous step so the closure is recomputed
    // from the authored (non-derivation) graph and stays idempotent.
    std::size_t cleared = 0;
    for (const auto& pointer : snapshot.pointers) {
        if (pointer.law_domain == "derivation" && active_at(pointer, snapshot.epoch)) {
            delta.append_unlink(PointerRemove{pointer.id});
            cleared += 1;
        }
    }

    // Seed the fact index from the authored graph.
    FactIndex facts;
    for (const auto& pointer : snapshot.pointers) {
        if (!active_at(pointer, snapshot.epoch) || pointer.law_domain == "derivation") {
            continue;
        }
        facts[snapshot.relation_name(pointer.relation)].insert({pointer.from, pointer.to});
    }

    std::vector<DerivedEdge> created;
    const std::size_t cap = 64 + snapshot.objects.size();
    for (std::size_t iteration = 0; iteration < cap; ++iteration) {
        std::vector<DerivedEdge> round;
        for (const auto& derivation : derivations_) {
            std::unordered_map<std::string, ObjectId> bindings;
            evaluate_body(derivation, 0, bindings, facts, [&](ObjectId from, ObjectId to) {
                const auto& known = facts[derivation.head.relation];
                if (known.find({from, to}) == known.end()) {
                    round.push_back(DerivedEdge{from, to, derivation.head.relation, derivation.head.role, derivation.head.weight});
                }
            });
        }
        bool changed = false;
        for (const auto& edge : round) {
            if (facts[edge.relation].insert({edge.from, edge.to}).second) {
                created.push_back(edge);
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }

    std::ranges::sort(created, [](const DerivedEdge& left, const DerivedEdge& right) {
        if (!(left.from == right.from)) {
            return left.from < right.from;
        }
        if (!(left.to == right.to)) {
            return left.to < right.to;
        }
        return left.relation < right.relation;
    });

    RelationInterner interner{snapshot, delta};
    for (const auto& edge : created) {
        const auto relation = interner.intern(edge.relation);
        delta.append_link(PointerCreate{
            ObjectRef{edge.from},
            ObjectRef{edge.to},
            relation,
            edge.role,
            Weight{edge.weight},
            "derivation",
            {}});
    }

    delta.append_event(TraceEvent{
        {},
        "world.derive",
        {{"step_epoch", std::to_string(next.value)}},
        {
            {"derived", static_cast<double>(created.size())},
            {"cleared", static_cast<double>(cleared)}
        }});
    return delta;
}

}  // namespace pv
