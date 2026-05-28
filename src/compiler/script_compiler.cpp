// SPDX-License-Identifier: Apache-2.0
#include "pv/compiler/script_compiler.hpp"

namespace pv {

Program ScriptCompiler::compile_object(const WorldSnapshot& snapshot, std::string_view name, std::string_view type) const {
    ProgramBuilder builder;
    builder.import_symbols(snapshot);
    (void)builder.create_object(name, type);
    return builder.build();
}

Program ScriptCompiler::compile_link(
    const WorldSnapshot& snapshot,
    std::string_view from,
    std::string_view to,
    std::string_view relation,
    double weight,
    CausalRole role) const {
    ProgramBuilder builder;
    builder.import_symbols(snapshot);
    (void)builder.create_pointer(
        builder.object_by_name(from),
        builder.object_by_name(to),
        relation,
        weight,
        role,
        "core");
    return builder.build();
}

Program ScriptCompiler::compile_delta(const WorldSnapshot& snapshot, const Delta& delta) const {
    ProgramBuilder builder;
    builder.import_symbols(snapshot);
    builder.append_delta(snapshot, delta);
    return builder.build();
}

}  // namespace pv
