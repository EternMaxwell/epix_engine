module;
#include <epix/shader.hpp>

export module epix.shader;

export namespace epix::shader {
using epix::shader::CachedPipelineId;
using epix::shader::ComposeError;
using epix::shader::Shader;
using epix::shader::ShaderCache;
using epix::shader::ShaderCacheError;
using epix::shader::ShaderCacheSource;
using epix::shader::ShaderComposer;
using epix::shader::ShaderData;
using epix::shader::ShaderDefVal;
using epix::shader::ShaderImport;
using epix::shader::ShaderLoader;
using epix::shader::ShaderLoaderError;
using epix::shader::ShaderPlugin;
using epix::shader::ShaderProcessor;
using epix::shader::ShaderProcessorSettings;
using epix::shader::ShaderRef;
using epix::shader::ShaderSettings;
using epix::shader::Source;
using epix::shader::ValidateShader;
} // namespace epix::shader
