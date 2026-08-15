#include "apsis_drift/seed.hpp"

#include <array>
#include <cstddef>
#include <type_traits>

namespace apsis_drift {
namespace {

inline constexpr std::uint64_t kFnvOffsetBasis{14695981039346656037ULL};
inline constexpr std::uint64_t kFnvPrime{1099511628211ULL};
inline constexpr std::array<std::uint8_t, 16> kSeedNamespace{
    0x41, 0x50, 0x53, 0x49, 0x53, 0x2D, 0x44, 0x52,
    0x49, 0x46, 0x54, 0x2D, 0x53, 0x45, 0x45, 0x44,
};

auto hash_byte(std::uint64_t& hash, std::uint8_t byte) noexcept -> void {
  hash ^= byte;
  hash *= kFnvPrime;
}

template <typename Integer>
auto hash_little_endian(std::uint64_t& hash, Integer value) noexcept -> void {
  using Unsigned = std::make_unsigned_t<Integer>;
  auto remaining = static_cast<Unsigned>(value);
  for (std::size_t byte = 0; byte < sizeof(Unsigned); ++byte) {
    hash_byte(hash, static_cast<std::uint8_t>(remaining & 0xFFU));
    remaining >>= 8U;
  }
}

}  // namespace

auto derive_seed(Seed parent, SeedDomain domain,
                 std::uint64_t ordinal) noexcept -> Seed {
  auto hash = kFnvOffsetBasis;
  for (const auto byte : kSeedNamespace) hash_byte(hash, byte);
  hash_little_endian(hash, kSeedDerivationVersion);
  hash_little_endian(hash, parent.value);
  hash_little_endian(hash, static_cast<std::uint64_t>(domain));
  hash_little_endian(hash, ordinal);
  return Seed{hash};
}

}  // namespace apsis_drift
