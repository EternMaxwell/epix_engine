export module epix.shader:shader_composer;

import :shader;
import std;

namespace epix::shader {

/** @brief Error returned while composing WGSL source. */
export struct ComposeError {
    /** @brief A `#import ...` name was used but no module with that name was registered. */
    struct ImportNotFound {
        std::string import_name;
    };
    /** @brief The composer could not parse part of the source. */
    struct ParseError {
        std::string module_name;
        std::string details;
    };
    /** @brief Imports formed a cycle such as `a -> b -> a`. */
    struct CircularImport {
        std::vector<std::string> cycle_chain;
    };

    /** @brief The active error value. */
    std::variant<ImportNotFound, ParseError, CircularImport> data;
};

/** @brief WGSL composer used to expand `#import` and simple conditional blocks.
 *
 * In practice this means:
 *
 * - if your shader contains `#import lighting`, the composer replaces it with
 *   the WGSL module previously added as `lighting`.
 * - if your shader contains `#ifdef USE_FOG`, the block is kept only when the
 *   active definitions contain `USE_FOG`.
 *
 * This is only for WGSL. Slang uses its own import system.
 */
export struct ShaderComposer {
    /** @brief Register one WGSL module.
     *
     * `module_name` must match what appears in source, for example
     * `#import lighting` looks for a module added as `lighting`.
     * `defs` are definitions that are always active for this module.
     */
    std::expected<void, ComposeError> add_module(const std::string& module_name,
                                                 std::string_view source,
                                                 std::span<const ShaderDefVal> defs);

    /** @brief Remove one registered module.
     *
     * If the module does not exist, nothing happens.
     */
    void remove_module(const std::string& module_name);

    /** @brief Returns `true` when a module with this name is registered. */
    bool contains_module(const std::string& module_name) const;

    /** @brief Compose one WGSL source string.
     *
     * This expands all `#import ...` directives and evaluates simple
     * conditionals like `#ifdef`, `#ifndef`, `#if`, `#else`, and `#endif`.
     * The returned string is the final WGSL text ready to pass to shader
     * module creation.
     */
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
