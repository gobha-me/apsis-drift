#pragma once
#include "apsis_drift/freedom_starting_assembly_save.hpp"
#include "apsis_drift/origin_lower_cockpit_contact.hpp"
#include <memory>
namespace apsis_drift {
// Copyable immutable contact/presentation selection. Default construction is
// legacy presentation with unknown hardware and no invented operating pose.
class NativeCraftBinding {
 public:
  NativeCraftBinding() = default;
  [[nodiscard]] auto selection() const
      -> const NativeStartingAssemblySelection*;
  [[nodiscard]] auto contact() const -> const OriginLowerCockpitContact*;
  [[nodiscard]] auto pose() const -> const OperatingPose*;

 private:
  struct Data;
  std::shared_ptr<const Data> data_;
  friend auto make_native_starting_assembly_binding(
      const NativeStartingAssemblySelection&)
      -> std::expected<NativeCraftBinding, std::string>;
};
[[nodiscard]] auto make_native_starting_assembly_binding(
    const NativeStartingAssemblySelection&)
    -> std::expected<NativeCraftBinding, std::string>;
} // namespace apsis_drift
