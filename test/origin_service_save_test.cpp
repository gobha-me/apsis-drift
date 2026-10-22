#include "apsis_drift/origin_service_save.hpp"

#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>

using namespace apsis_drift;
namespace {
unsigned checks{}, failures{};
auto check(bool result, const char* label) -> void {
  ++checks;
  if (!result) {
    ++failures;
    std::cerr << "FAIL " << label << '\n';
  }
}
} // namespace
auto main() -> int {
  const auto owner = generate_physical_origin_system({42}, 2);
  if (!owner) return 1;
  const auto source = initialize_origin_service_craft(*owner, 0);
  if (!source) return 1;
  const auto encoded = encode_origin_service_state_json(*owner, *source);
  check(encoded.has_value(), "genuine stationed responder encodes");
  if (!encoded) return 1;
  const auto restored = decode_origin_service_state_json(*owner, *encoded);
  check(restored && *restored == *source,
        "complete stationed responder round trip");
  const auto second = restored
                          ? encode_origin_service_state_json(*owner, *restored)
                          : std::expected<std::string, SaveSchemaError>{
                                std::unexpected(SaveSchemaError{})};
  check(second && *second == *encoded, "canonical byte-stable round trip");
  const auto foreign = generate_physical_origin_system({43}, 2);
  check(foreign && !decode_origin_service_state_json(*foreign, *encoded),
        "foreign origin refused");
  auto altered = *source;
  altered.resources.cargo_quanta--;
  check(!encode_origin_service_state_json(*owner, altered),
        "unreceipted cargo loss cannot encode");
  altered = *source;
  altered.body.position_metres.x = std::numeric_limits<double>::quiet_NaN();
  check(!encode_origin_service_state_json(*owner, altered),
        "nonfinite state cannot encode");
  auto base = nlohmann::ordered_json::parse(*encoded);
  auto rejects = [&](nlohmann::ordered_json value, const char* label) {
    check(!decode_origin_service_state_json(*owner, value.dump()), label);
  };
  auto changed = base;
  changed["version"] = 2U;
  rejects(changed, "unknown version");
  changed = base;
  changed["unknown"] = true;
  rejects(changed, "unknown top field");
  changed = base;
  changed["resources"].erase("tick");
  rejects(changed, "missing resource clock");
  changed = base;
  changed["owner"]["seed"] = 43U;
  rejects(changed, "foreign selected seed");
  changed = base;
  changed["resources"]["jump_charges"] = 4294967296ULL;
  rejects(changed, "charge narrowing overflow");
  changed = base;
  changed["resources"]["jump_charges"] = -1;
  rejects(changed, "signed negative charge");
  changed = base;
  changed["resources"]["flight_quanta"] = kFreedomFlightCapacityQuanta + 1;
  rejects(changed, "overfull propulsion pool");
  changed = base;
  changed["resources"]["tick"] = 1U;
  rejects(changed, "body/resource clock mismatch");
  changed = base;
  changed["resources"]["tick"] = std::numeric_limits<std::uint64_t>::max();
  rejects(changed, "clock overflow");
  changed = base;
  changed["attached"] = false;
  changed["delivery"] = nlohmann::ordered_json::object();
  rejects(changed, "malformed partial receipt");
  auto duplicate = *encoded;
  const auto first = duplicate.find('{');
  duplicate.insert(first + 1, "\"version\":1,");
  const auto dup_result = decode_origin_service_state_json(*owner, duplicate);
  check(!dup_result &&
            dup_result.error().code == SaveSchemaErrorCode::duplicate_key,
        "duplicate key refused");
  check(!decode_origin_service_state_json(*owner, std::string(33, '[') +
                                                      std::string(33, ']')),
        "deep nesting refused before parsing");
  std::string padded = *encoded;
  padded.resize(kMaximumOriginServiceStateBytes, ' ');
  check(decode_origin_service_state_json(*owner, padded).has_value(),
        "exact byte boundary accepts valid whitespace");
  padded.push_back(' ');
  const auto large = decode_origin_service_state_json(*owner, padded);
  check(!large && large.error().code == SaveSchemaErrorCode::document_too_large,
        "one byte over boundary refused");
  check(!decode_origin_service_state_json(*owner, "{}x"),
        "trailing malformed input refused");
  check(*source == *restored, "failed reads never mutate source state");
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
