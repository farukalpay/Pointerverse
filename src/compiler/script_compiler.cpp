// SPDX-License-Identifier: Apache-2.0
#include "pv/compiler/script_compiler.hpp"

namespace pv {

Program ScriptCompiler::compile_object(
    const WorldSnapshot& snapshot,
    std::string_view name,
    std::string_view type,
    const std::vector<Attribute>& attributes) const {
    ProgramBuilder builder;
    builder.import_symbols(snapshot);
    const auto object = builder.create_object(name, type);
    for (const auto& attribute : attributes) {
        builder.set_object_attribute(object, attribute.key, attribute.value);
    }
    return builder.build();
}

Program ScriptCompiler::compile_link(
    const WorldSnapshot& snapshot,
    std::string_view from,
    std::string_view to,
    std::string_view relation,
    double weight,
    CausalRole role,
    const std::vector<Attribute>& attributes) const {
    ProgramBuilder builder;
    builder.import_symbols(snapshot);
    const auto pointer = builder.create_pointer(
        builder.object_by_name(from),
        builder.object_by_name(to),
        relation,
        weight,
        role,
        "core");
    for (const auto& attribute : attributes) {
        builder.set_pointer_attribute(pointer, attribute.key, attribute.value);
    }
    return builder.build();
}

Program ScriptCompiler::compile_delta(const WorldSnapshot& snapshot, const Delta& delta) const {
    ProgramBuilder builder;
    builder.import_symbols(snapshot);
    builder.append_delta(snapshot, delta);
    return builder.build();
}

}  // namespace pv
