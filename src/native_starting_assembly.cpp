#include "apsis_drift/boarding_support_data.hpp"
#include "apsis_drift/cabin_contact_data.hpp"
#include "apsis_drift/lower_cockpit_contact_data.hpp"
#include "apsis_drift/native_craft_binding.hpp"
#include "apsis_drift/operating_motion_recipe.hpp"
#include "apsis_drift/stowed_contact_data.hpp"
namespace apsis_drift {
struct NativeCraftBinding::Data {
  NativeStartingAssemblySelection selection;
  OriginLowerCockpitContact contact;
  OperatingPose pose;
};
auto NativeCraftBinding::selection() const
    -> const NativeStartingAssemblySelection* {
  return data_ ? &data_->selection : nullptr;
}
auto NativeCraftBinding::contact() const -> const OriginLowerCockpitContact* {
  return data_ ? &data_->contact : nullptr;
}
auto NativeCraftBinding::pose() const -> const OperatingPose* {
  return data_ ? &data_->pose : nullptr;
}
auto make_native_starting_assembly_binding(
    const NativeStartingAssemblySelection& selected)
    -> std::expected<NativeCraftBinding, std::string> {
  if (selected != NativeStartingAssemblySelection{})
    return std::unexpected{"Unsupported starting assembly"};
  // Immutable shared application-owned contact; no filesystem assets or caller
  // pose.
  static const auto prepared =
      []() -> std::expected<std::shared_ptr<const NativeCraftBinding::Data>,
                            std::string> {
    auto motion =
        decode_operating_motion_recipe(detail::kOperatingMotionRecipeJson);
    if (!motion) return std::unexpected{motion.error()};
    auto support = decode_origin_boarding_support(
        detail::boarding_support_json(), *motion);
    if (!support) return std::unexpected{support.error()};
    const auto join = [](const auto& chunks) {
      std::string s;
      std::size_t n{};
      for (auto c : chunks)
        n += c.size();
      s.reserve(n);
      for (auto c : chunks)
        s += c;
      return s;
    };
    auto cabin = decode_origin_cabin_seam_contact(
        join(detail::kCabinContactChunks), *support);
    if (!cabin) return std::unexpected{cabin.error()};
    auto base = decode_origin_lower_cockpit_contact(
        join(detail::kLowerContactChunks), detail::kLowerContactPolicyJson,
        *cabin);
    if (!base) return std::unexpected{base.error()};
    auto contact = make_origin_stowed_lower_cockpit_contact(
        *base, join(detail::kStowedContactChunks), detail::kStowedFrameJson);
    if (!contact) return std::unexpected{contact.error()};
    auto pose = evaluate_operating_motion(*motion, kCabinSeamHardware);
    if (!pose) return std::unexpected{pose.error()};
    return std::make_shared<const NativeCraftBinding::Data>(
        NativeCraftBinding::Data{{}, std::move(*contact), *pose});
  }();
  if (!prepared) return std::unexpected{prepared.error()};
  NativeCraftBinding binding;
  binding.data_ = *prepared;
  return binding;
}
} // namespace apsis_drift
