// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "pv/category/morphism.hpp"
#include "pv/core/world.hpp"
#include "pv/domain/package.hpp"
#include "pv/rule/rule_engine.hpp"
#include "pv/runtime/transaction.hpp"

namespace pv {
class Repository;
}

namespace pv::cli {

class TransactionSink {
public:
    virtual ~TransactionSink() = default;

    [[nodiscard]] virtual World& world() = 0;
    [[nodiscard]] virtual const World& world() const = 0;
    [[nodiscard]] virtual CommitResult commit(Transaction tx) = 0;
    [[nodiscard]] virtual bool reset_world(std::string_view name) = 0;
};

class WorldTransactionSink final : public TransactionSink {
public:
    WorldTransactionSink(World& world, Verifier& verifier);

    [[nodiscard]] World& world() override;
    [[nodiscard]] const World& world() const override;
    [[nodiscard]] CommitResult commit(Transaction tx) override;
    [[nodiscard]] bool reset_world(std::string_view name) override;

private:
    World& world_;
    Verifier& verifier_;
};

class RepositoryTransactionSink final : public TransactionSink {
public:
    RepositoryTransactionSink(Repository& repository, std::string branch, Verifier& verifier);

    [[nodiscard]] World& world() override;
    [[nodiscard]] const World& world() const override;
    [[nodiscard]] CommitResult commit(Transaction tx) override;
    [[nodiscard]] bool reset_world(std::string_view name) override;

private:
    Repository& repository_;
    std::string branch_;
    Verifier& verifier_;
};

class ScriptEngine {
public:
    explicit ScriptEngine(World& world);
    ScriptEngine(Repository& repository, std::string branch);

    bool run_stream(std::istream& input, std::ostream& output, bool interactive = false);
    bool run_file(const std::string& path, std::ostream& output);
    bool execute_line(const std::string& line, std::ostream& output);

private:
    Verifier verifier_;
    RuleEngine rule_engine_;
    DomainRegistry domains_;
    RuleBuilder rule_builder_;
    std::unique_ptr<TransactionSink> sink_;
    std::unordered_map<std::string, std::shared_ptr<const Morphism>> morphisms_;
};

}  // namespace pv::cli
