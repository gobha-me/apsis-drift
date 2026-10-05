#pragma once
#include "apsis_drift/freedom_journey_save.hpp"
#include "apsis_drift/operating_motion.hpp"
namespace apsis_drift {
inline constexpr std::uint32_t kFreedomStartingAssemblySaveFormatVersion{21};
// One supported starting assembly, validated against fixed application pins.
// It records hardware selection, not actor seating or permission to move it.
struct NativeStartingAssemblySelection {
  std::uint32_t version{1};
  std::string preset{"wayfarer-stowed-01"};
  std::string operating_model_sha256{
      "a9a8104a0ea8b5c22e4149861a76b5ab3911a9f77ba871b446c08bf9ed56621c"};
  std::string stowed_model_sha256{
      "d286dfd5174940ccd7e3db0cb023d3571ad460c6b22609c6f81208e16f06e2fc"};
  std::string frame_sha256{
      "f98c50f69d71ecd38ca1d43d010f7b43ca300178dc25d40e170fd45fe5bc88a6"};
  std::string contact_sha256{
      "62b4d493f2d74f59b81fd089873360b99e3bae9d17a6d90a00066e9440e44c4a"};
  OperatingProgress hardware{1, 1, 1, 0};
  friend auto operator==(const NativeStartingAssemblySelection&,
                         const NativeStartingAssemblySelection&)
      -> bool = default;
};
// The complete version20 journey remains the nested voyage/actor authority.
struct FreedomStartingAssemblySaveDocument {
  FreedomJourneySaveDocument journey;
  NativeStartingAssemblySelection starting_assembly;
  friend auto operator==(const FreedomStartingAssemblySaveDocument&,
                         const FreedomStartingAssemblySaveDocument&)
      -> bool = default;
};
[[nodiscard]] auto make_freedom_starting_assembly_new_game_document(Seed)
    -> std::expected<FreedomStartingAssemblySaveDocument, SaveSchemaError>;
[[nodiscard]] auto validate_freedom_starting_assembly_document(
    const FreedomStartingAssemblySaveDocument&)
    -> std::expected<void, SaveSchemaError>;
[[nodiscard]] auto encode_freedom_starting_assembly_document_json(
    const FreedomStartingAssemblySaveDocument&)
    -> std::expected<std::string, SaveSchemaError>;
[[nodiscard]] auto decode_freedom_starting_assembly_document_json(
    std::string_view)
    -> std::expected<FreedomStartingAssemblySaveDocument, SaveSchemaError>;
} // namespace apsis_drift
