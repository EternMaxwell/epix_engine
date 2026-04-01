export module epix.shader:shader_composer;

import :shader;
import std;

namespace epix::shader {

// ─── ComposeError ──────────────────────────────────────────────────────────
export struct ComposeError {
    struct ImportNotFound {
        std::string import_name;
    };
    struct ParseError {
        std::string module_name;
        std::string details;
    };
    struct CircularImport {
        std::vector<std::string> cycle_chain;
    };

    std::variant<ImportNotFound, ParseError, CircularImport> data;
};

// ─── ShaderComposer ────────────────────────────────────────────────────────
// A simple WGSL preprocessor that resolves #import directives and handles
// #ifdef / #ifndef / #if / #else / #endif conditional blocks based on
// ShaderDefVal sets.
export struct ShaderComposer {
    // Add a module that can be #imported by other shaders.
    // module_name should match what appears after '#import' in WGSL source.
    // defs are the module's base-level always-active defines (from Shader::shader_defs).
    std::expected<void, ComposeError> add_module(const std::string& module_name,
                                                 std::string_view source,
                                                 std::span<const ShaderDefVal> defs);

    // Remove a registered module. Silently ignores unknown names.
    void remove_module(const std::string& module_name);

    // Returns true if module_name is registered.
    bool contains_module(const std::string& module_name) const;

    // Compose source: resolve all #import directives (inline registered modules),
    // apply additional_defs to #ifdef / #ifndef / #if guards.
    // Returns the flat, self-contained WGSL string ready for createShaderModule.
    std::expected<std::string, ComposeError> compose(std::string_view source,
                                                     std::string_view file_path,
                                                     std::span<const ShaderDefVal> additional_defs);

   private:
    struct ModuleEntry {
        std::string source;                   // original source (with directives)
        std::vector<ShaderDefVal> base_defs;  // the module's own always-active defs
    };

    std::unordered_map<std::string, ModuleEntry> modules_;

    // Build a name→value lookup for quick evaluation of #ifdef conditions.
    static std::unordered_map<std::string, const ShaderDefVal*> build_def_map(std::span<const ShaderDefVal> a,
                                                                              std::span<const ShaderDefVal> b = {});

    // Evaluate a single #if / #ifdef / #ifndef condition against defs.
    // Returns true if the block should be included.
    static bool eval_condition(std::string_view directive,
                               std::string_view expr,
                               const std::unordered_map<std::string, const ShaderDefVal*>& defs);

    // Internal recursive compose.
    // visiting: set of module names currently being inlined (cycle detection).
    std::expected<std::string, ComposeError> compose_internal(
        std::string_view source,
        std::string_view context_name,
        const std::unordered_map<std::string, const ShaderDefVal*>& defs,
        std::vector<std::string>& visiting);
};

}  // namespace epix::shader
