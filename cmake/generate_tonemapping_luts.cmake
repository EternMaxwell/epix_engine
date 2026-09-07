function(append_embedded_bytes input_path symbol)
  file(READ "${input_path}" bytes HEX)
  file(SIZE "${input_path}" byte_count)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${bytes}")
  file(APPEND "${OUTPUT_FILE}"
    "namespace {\n"
    "const unsigned char ${symbol}_data[${byte_count}] = {${bytes}};\n"
    "}\n"
    "std::span<const std::byte> ${symbol}() noexcept {\n"
    "  return {reinterpret_cast<const std::byte*>(${symbol}_data), sizeof(${symbol}_data)};\n"
    "}\n")
endfunction()

get_filename_component(output_directory "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")
file(WRITE "${OUTPUT_FILE}"
  "// Generated from Bevy 0.18's bundled KTX2 tonemapping LUTs.\n"
  "#include <cstddef>\n"
  "#include <span>\n"
  "namespace epix::core_graph::detail {\n")

append_embedded_bytes("${BLENDER_FILE}" embedded_blender_filmic_ktx2)
append_embedded_bytes("${AGX_FILE}" embedded_agx_ktx2)
append_embedded_bytes("${TONY_FILE}" embedded_tony_mc_mapface_ktx2)

file(APPEND "${OUTPUT_FILE}" "}  // namespace epix::core_graph::detail\n")
