#include "apsis_drift/save_file.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "save_file_internal.hpp"

namespace apsis_drift {
namespace {

std::atomic<std::uint64_t> temporary_sequence{};

[[nodiscard]] auto file_failure(SaveFileErrorCode code,
                                const std::filesystem::path& path,
                                std::string detail) -> SaveFileError {
  return SaveFileError{code, path, std::move(detail), std::nullopt};
}

[[nodiscard]] auto system_detail(std::string_view action, int error)
    -> std::string {
  return std::format("{}: {}", action,
                     std::system_category().message(error));
}

[[nodiscard]] auto parent_directory(const std::filesystem::path& path)
    -> std::filesystem::path {
  const auto parent = path.parent_path();
  return parent.empty() ? std::filesystem::path{"."} : parent;
}

[[nodiscard]] auto valid_destination(const std::filesystem::path& path)
    -> bool {
  return !path.empty() && !path.filename().empty() &&
         path.filename() != "." && path.filename() != "..";
}

class FileDescriptor {
 public:
  explicit FileDescriptor(int value = -1) noexcept : m_value(value) {}
  FileDescriptor(const FileDescriptor&) = delete;
  auto operator=(const FileDescriptor&) -> FileDescriptor& = delete;
  FileDescriptor(FileDescriptor&& other) noexcept
      : m_value(std::exchange(other.m_value, -1)) {}
  auto operator=(FileDescriptor&& other) noexcept -> FileDescriptor& {
    if (this == &other) return *this;
    reset();
    m_value = std::exchange(other.m_value, -1);
    return *this;
  }
  ~FileDescriptor() { reset(); }

  [[nodiscard]] auto get() const noexcept -> int { return m_value; }
  [[nodiscard]] auto valid() const noexcept -> bool { return m_value >= 0; }
  [[nodiscard]] auto release() noexcept -> int {
    return std::exchange(m_value, -1);
  }
  auto reset(int value = -1) noexcept -> void {
    if (m_value >= 0) {
      (void)::close(m_value);
    }
    m_value = value;
  }

 private:
  int m_value{-1};
};

class TemporarySave {
 public:
  explicit TemporarySave(std::filesystem::path path)
      : m_path(std::move(path)) {}
  TemporarySave(const TemporarySave&) = delete;
  auto operator=(const TemporarySave&) -> TemporarySave& = delete;
  TemporarySave(TemporarySave&& other) noexcept
      : m_path(std::move(other.m_path)),
        m_committed(std::exchange(other.m_committed, true)) {}
  auto operator=(TemporarySave&& other) noexcept -> TemporarySave& {
    if (this == &other) return *this;
    if (!m_committed && !m_path.empty()) {
      std::error_code ignored;
      std::filesystem::remove(m_path, ignored);
    }
    m_path = std::move(other.m_path);
    m_committed = std::exchange(other.m_committed, true);
    return *this;
  }
  ~TemporarySave() {
    if (!m_committed && !m_path.empty()) {
      std::error_code ignored;
      std::filesystem::remove(m_path, ignored);
    }
  }

  [[nodiscard]] auto path() const -> const std::filesystem::path& {
    return m_path;
  }
  auto commit() noexcept -> void { m_committed = true; }

 private:
  std::filesystem::path m_path;
  bool m_committed{};
};

struct CreatedTemporary {
  TemporarySave file;
  FileDescriptor descriptor;
};

[[nodiscard]] auto create_temporary(const std::filesystem::path& destination)
    -> std::expected<CreatedTemporary, SaveFileError> {
  const auto directory = parent_directory(destination);
  constexpr std::uint64_t maximum_attempts{128};
  for (std::uint64_t attempt = 0; attempt < maximum_attempts; ++attempt) {
    const auto sequence = temporary_sequence.fetch_add(1);
    const auto filename = std::format(".{}.tmp.{}.{}",
                                      destination.filename().string(),
                                      static_cast<long long>(::getpid()),
                                      sequence);
    auto temporary_path = directory / filename;
    const int descriptor = ::open(temporary_path.c_str(),
                                  O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                                  S_IRUSR | S_IWUSR);
    if (descriptor >= 0) {
      return CreatedTemporary{TemporarySave{std::move(temporary_path)},
                              FileDescriptor{descriptor}};
    }
    const int error = errno;
    if (error == EEXIST) continue;
    return std::unexpected{file_failure(
        SaveFileErrorCode::temporary_file_failed, destination,
        system_detail("cannot create a temporary save in the destination directory",
                      error))};
  }
  return std::unexpected{file_failure(
      SaveFileErrorCode::temporary_file_failed, destination,
      "cannot allocate a unique temporary save name after 128 attempts")};
}

[[nodiscard]] auto write_all(int descriptor, std::string_view bytes,
                             const std::filesystem::path& destination)
    -> std::expected<void, SaveFileError> {
  std::size_t offset{};
  while (offset < bytes.size()) {
    const auto count =
        ::write(descriptor, bytes.data() + offset, bytes.size() - offset);
    if (count > 0) {
      offset += static_cast<std::size_t>(count);
      continue;
    }
    if (count < 0 && errno == EINTR) continue;
    const int error = count < 0 ? errno : EIO;
    return std::unexpected{file_failure(
        SaveFileErrorCode::write_failed, destination,
        system_detail("cannot write the complete temporary save", error))};
  }
  return {};
}

[[nodiscard]] auto close_checked(FileDescriptor& descriptor,
                                 const std::filesystem::path& path,
                                 SaveFileErrorCode code,
                                 std::string_view action)
    -> std::expected<void, SaveFileError> {
  const int value = descriptor.release();
  if (value < 0) return {};
  if (::close(value) < 0) {
    const int error = errno;
    if (error == EINTR) return {};
    return std::unexpected{
        file_failure(code, path, system_detail(action, error))};
  }
  return {};
}

[[nodiscard]] auto synchronize_directory(
    const std::filesystem::path& directory,
    const std::filesystem::path& destination)
    -> std::expected<void, SaveFileError> {
  FileDescriptor descriptor{
      ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
  if (!descriptor.valid()) {
    const int error = errno;
    return std::unexpected{file_failure(
        SaveFileErrorCode::directory_sync_failed, destination,
        system_detail("save was replaced but the destination directory cannot be opened for synchronization",
                      error))};
  }
  if (::fsync(descriptor.get()) < 0) {
    const int error = errno;
    return std::unexpected{file_failure(
        SaveFileErrorCode::directory_sync_failed, destination,
        system_detail("save was replaced but the destination directory cannot be synchronized",
                      error))};
  }
  return close_checked(descriptor, destination,
                       SaveFileErrorCode::directory_sync_failed,
                       "save was replaced but the destination directory cannot be closed");
}

[[nodiscard]] auto write_atomically(
    const std::filesystem::path& path, const SaveDocument& document,
    detail::AtomicSaveTestInterruption interruption)
    -> std::expected<void, SaveFileError> {
  if (!valid_destination(path)) {
    return std::unexpected{file_failure(
        SaveFileErrorCode::invalid_path, path,
        "save path must name a file inside an existing directory")};
  }

  const auto encoded = encode_save_document_json(document);
  if (!encoded) {
    return std::unexpected{SaveFileError{
        SaveFileErrorCode::invalid_document, path,
        "authoritative state cannot be encoded in the current save format",
        encoded.error()}};
  }

  auto created = create_temporary(path);
  if (!created) return std::unexpected{created.error()};
  auto temporary = std::move(created->file);
  auto descriptor = std::move(created->descriptor);

  if (auto written = write_all(descriptor.get(), *encoded, path); !written) {
    return written;
  }
  if (::fsync(descriptor.get()) < 0) {
    const int error = errno;
    return std::unexpected{file_failure(
        SaveFileErrorCode::sync_failed, path,
        system_detail("cannot synchronize the temporary save", error))};
  }
  if (auto closed = close_checked(descriptor, path,
                                  SaveFileErrorCode::sync_failed,
                                  "cannot close the temporary save");
      !closed) {
    return closed;
  }

  if (interruption == detail::AtomicSaveTestInterruption::before_replace) {
    return std::unexpected{file_failure(
        SaveFileErrorCode::replace_failed, path,
        "simulated interruption before atomic replacement")};
  }
  if (::rename(temporary.path().c_str(), path.c_str()) < 0) {
    const int error = errno;
    return std::unexpected{file_failure(
        SaveFileErrorCode::replace_failed, path,
        system_detail("cannot atomically replace the destination save",
                      error))};
  }
  temporary.commit();

  return synchronize_directory(parent_directory(path), path);
}

}  // namespace

auto make_new_game_document(Seed universe_seed,
                            NewGameOnboardingChoice onboarding)
    -> SaveDocument {
  return make_new_game_document(NewGameOptions{
      .universe_seed = universe_seed,
      .penalty_mode = IntersystemRuleProfile::assisted,
      .onboarding = onboarding,
  });
}

auto make_new_game_document(const NewGameOptions& options) -> SaveDocument {
  auto document = make_legacy_signal_run_document(options.universe_seed);
  document.state.onboarding = initial_onboarding_progress(options.onboarding);
  document.state.intersystem_contract =
      initial_intersystem_contract_state(options.universe_seed,
                                         options.penalty_mode);
  return document;
}

auto make_legacy_signal_run_document(Seed universe_seed) -> SaveDocument {
  const auto binding = generate_home_signal_contract(universe_seed);
  return SaveDocument{
      .recipe = make_save_recipe(universe_seed),
      .state =
          SaveMutableState{
              .location = OriginLocation::docked_at_origin,
              .first_objective = FirstObjectiveStatus::offered,
              .first_objective_contract = binding.contract,
              .first_objective_target = binding.target,
              .flight = std::nullopt,
              .system_flight = std::nullopt,
              .origin_station_flight = std::nullopt,
              .discoveries = {},
              .world_deltas = {},
              .intersystem_contract = std::nullopt,
          },
  };
}

auto load_save_file(const std::filesystem::path& path)
    -> std::expected<SaveDocument, SaveFileError> {
  if (!valid_destination(path)) {
    return std::unexpected{file_failure(
        SaveFileErrorCode::invalid_path, path,
        "load path must name a save file")};
  }

  FileDescriptor descriptor{::open(path.c_str(), O_RDONLY | O_CLOEXEC)};
  if (!descriptor.valid()) {
    const int error = errno;
    return std::unexpected{file_failure(
        error == ENOENT ? SaveFileErrorCode::not_found
                        : SaveFileErrorCode::open_failed,
        path, system_detail(error == ENOENT ? "save file does not exist"
                                            : "cannot open the save file",
                            error))};
  }

  std::string contents;
  contents.reserve(kMaximumSaveDocumentBytes);
  std::array<char, 16U * 1024U> buffer{};
  while (contents.size() <= kMaximumSaveDocumentBytes) {
    const std::size_t remaining =
        kMaximumSaveDocumentBytes + 1U - contents.size();
    const auto count = ::read(descriptor.get(), buffer.data(),
                              std::min(buffer.size(), remaining));
    if (count > 0) {
      contents.append(buffer.data(), static_cast<std::size_t>(count));
      continue;
    }
    if (count == 0) break;
    if (errno == EINTR) continue;
    const int error = errno;
    return std::unexpected{file_failure(
        SaveFileErrorCode::read_failed, path,
        system_detail("cannot read the save file", error))};
  }
  if (auto closed = close_checked(descriptor, path,
                                  SaveFileErrorCode::read_failed,
                                  "cannot close the save file");
      !closed) {
    return std::unexpected{closed.error()};
  }
  if (contents.size() > kMaximumSaveDocumentBytes) {
    return std::unexpected{file_failure(
        SaveFileErrorCode::document_too_large, path,
        std::format("save exceeds the {}-byte format limit",
                    kMaximumSaveDocumentBytes))};
  }

  auto decoded = decode_save_document_json(contents);
  if (!decoded) {
    return std::unexpected{SaveFileError{
        SaveFileErrorCode::invalid_document, path,
        "save file is malformed, unsupported, or incompatible",
        decoded.error()}};
  }
  return *decoded;
}

auto write_save_file_atomically(const std::filesystem::path& path,
                                const SaveDocument& document)
    -> std::expected<void, SaveFileError> {
  return write_atomically(path, document,
                          detail::AtomicSaveTestInterruption::none);
}

auto save_file_error_message(const SaveFileError& error) -> std::string {
  std::string message = std::format("{}: {}", error.path.string(), error.detail);
  if (error.schema_error) {
    message += std::format(" ({}: {})", error.schema_error->path,
                           error.schema_error->detail);
  }
  return message;
}

namespace detail {

auto write_save_file_atomically_for_test(
    const std::filesystem::path& path, const SaveDocument& document,
    AtomicSaveTestInterruption interruption)
    -> std::expected<void, SaveFileError> {
  return write_atomically(path, document, interruption);
}

}  // namespace detail

}  // namespace apsis_drift
