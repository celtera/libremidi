#if !defined(LIBREMIDI_HEADER_ONLY)
  #include <libremidi/libremidi.hpp>
#endif

#include <libremidi/backends.hpp>
#include <libremidi/detail/ump_endpoint_api.hpp>
#include <libremidi/error_handler.hpp>
#include <libremidi/ump_endpoint.hpp>

#include <cassert>

namespace libremidi
{

//=============================================================================
// Backend Selection Helpers
//=============================================================================

namespace ump_endpoint_backends
{

// Helper to create a UMP endpoint from backend-specific configurations
static LIBREMIDI_INLINE std::unique_ptr<ump_endpoint_api>
make_ump_endpoint(auto base_conf, auto api_conf, auto backends)
{
  std::unique_ptr<ump_endpoint_api> ptr;

  auto from_api = [&]<typename Backend>(Backend& /*backend*/) mutable {
    if constexpr (!std::is_void_v<typename Backend::midi_endpoint_configuration>)
    {
      if (auto conf = std::get_if<typename Backend::midi_endpoint_configuration>(&api_conf))
      {
        ptr = libremidi::make<typename Backend::midi_endpoint>(
            std::move(base_conf), std::move(*conf));
        return true;
      }
    }
    return false;
  };
  std::apply([&](auto&&... b) { (from_api(b) || ...); }, backends);
  return ptr;
}

// Helper to create a UMP endpoint observer from backend-specific configurations
static LIBREMIDI_INLINE std::unique_ptr<ump_endpoint_observer_api>
make_ump_observer(auto base_conf, auto api_conf, auto backends)
{
  std::unique_ptr<ump_endpoint_observer_api> ptr;

  auto from_api = [&]<typename Backend>(Backend& /*backend*/) mutable {
    if constexpr (!std::is_void_v<typename Backend::midi_endpoint_observer_configuration>)
    {
      if (auto conf
          = std::get_if<typename Backend::midi_endpoint_observer_configuration>(&api_conf))
      {
        ptr = libremidi::make<typename Backend::midi_endpoint_observer>(
            std::move(base_conf), std::move(*conf));
        return true;
      }
    }
    return false;
  };
  std::apply([&](auto&&... b) { (from_api(b) || ...); }, backends);
  return ptr;
}

// Find the default UMP endpoint API
static LIBREMIDI_INLINE std::unique_ptr<ump_endpoint_api>
make_default_ump_endpoint(const remote_ump_endpoint_configuration& base_conf)
{
  for (const auto& api : available_ump_apis())
  {
    try
    {
      auto impl = make_ump_endpoint(
          base_conf, midi_endpoint_configuration_for(api), midi2::available_backends);
      if (impl && impl->is_client_open() == stdx::error{})
        return impl;
    }
    catch (const std::exception&)
    {
    }
  }
  return nullptr;
}

}

LIBREMIDI_INLINE
ump_endpoint::ump_endpoint(const remote_ump_endpoint_configuration& config) noexcept
    : m_impl{ump_endpoint_backends::make_default_ump_endpoint(config)}
{
}

LIBREMIDI_INLINE
ump_endpoint::ump_endpoint(
    const remote_ump_endpoint_configuration& config, const endpoint_api_configuration& api_config)
    : m_impl{
          ump_endpoint_backends::make_ump_endpoint(config, api_config, midi2::available_backends)}
{
  if (!m_impl)
  {
    error_handler e;
    e.libremidi_handle_error(config, "Could not create UMP endpoint for the given api");
  }
}

LIBREMIDI_INLINE
ump_endpoint::ump_endpoint(ump_endpoint&& other) noexcept
    : m_impl{std::move(other.m_impl)}
{
}

LIBREMIDI_INLINE
ump_endpoint& ump_endpoint::operator=(ump_endpoint&& other) noexcept
{
  m_impl = std::move(other.m_impl);
  return *this;
}

LIBREMIDI_INLINE
ump_endpoint::~ump_endpoint() = default;

LIBREMIDI_INLINE
libremidi::API ump_endpoint::get_current_api() const noexcept
{
  return m_impl ? m_impl->get_current_api() : libremidi::API::UNSPECIFIED;
}
/*
LIBREMIDI_INLINE
stdx::error ump_endpoint::open(const ump_endpoint_info& endpoint_info, std::string_view local_name)
{
  if (!m_impl)
    return std::errc::not_connected;

  if (auto err = m_impl->is_client_open(); err != stdx::error{})
    return err;

  if (m_impl->is_port_open())
    return std::errc::operation_not_supported;

  return m_impl->open(endpoint_info, local_name);
}

LIBREMIDI_INLINE
stdx::error ump_endpoint::open_virtual(std::string_view port_name)
{
  if (!m_impl)
    return std::errc::not_connected;

  if (auto err = m_impl->is_client_open(); err != stdx::error{})
    return err;

  if (m_impl->is_port_open())
    return std::errc::operation_not_supported;

  return m_impl->open_virtual(port_name);
}

LIBREMIDI_INLINE
stdx::error ump_endpoint::close()
{
  if (!m_impl)
    return std::errc::not_connected;

  return m_impl->close();
}

LIBREMIDI_INLINE
bool ump_endpoint::is_open() const noexcept
{
  return m_impl && m_impl->is_port_open();
}

LIBREMIDI_INLINE
bool ump_endpoint::is_connected() const noexcept
{
  return m_impl && m_impl->is_connected();
}

LIBREMIDI_INLINE
std::optional<ump_endpoint_info> ump_endpoint::get_endpoint_info() const noexcept
{
  return m_impl ? m_impl->get_endpoint_info() : std::nullopt;
}

//-----------------------------------------------------------------------------
// Sending Messages
//-----------------------------------------------------------------------------

LIBREMIDI_INLINE
stdx::error ump_endpoint::send_ump(const ump& message) const
{
  if (!m_impl)
    return std::errc::not_connected;

  return m_impl->send_ump(message.data, 4);
}

LIBREMIDI_INLINE
stdx::error ump_endpoint::send_ump(std::span<const uint32_t> words) const
{
  if (!m_impl)
    return std::errc::not_connected;

  return m_impl->send_ump(words.data(), words.size());
}

LIBREMIDI_INLINE
stdx::error ump_endpoint::send_ump(uint32_t word0) const
{
  if (!m_impl)
    return std::errc::not_connected;

  return m_impl->send_ump(&word0, 1);
}

LIBREMIDI_INLINE
stdx::error ump_endpoint::send_ump(uint32_t word0, uint32_t word1) const
{
  if (!m_impl)
    return std::errc::not_connected;

  uint32_t words[2] = {word0, word1};
  return m_impl->send_ump(words, 2);
}

LIBREMIDI_INLINE
stdx::error ump_endpoint::send_ump(uint32_t word0, uint32_t word1, uint32_t word2) const
{
  if (!m_impl)
    return std::errc::not_connected;

  uint32_t words[3] = {word0, word1, word2};
  return m_impl->send_ump(words, 3);
}

LIBREMIDI_INLINE
stdx::error
ump_endpoint::send_ump(uint32_t word0, uint32_t word1, uint32_t word2, uint32_t word3) const
{
  if (!m_impl)
    return std::errc::not_connected;

  uint32_t words[4] = {word0, word1, word2, word3};
  return m_impl->send_ump(words, 4);
}

LIBREMIDI_INLINE
stdx::error ump_endpoint::send_umps(std::span<const ump> messages) const
{
  if (!m_impl)
    return std::errc::not_connected;

  for (const auto& msg : messages)
  {
    if (auto err = m_impl->send_ump(msg.data, 4); err.is_set())
      return err;
  }
  return stdx::error{};
}

LIBREMIDI_INLINE
stdx::error ump_endpoint::send_raw(const uint32_t* words, size_t count) const
{
  if (!m_impl)
    return std::errc::not_connected;

  return m_impl->send_ump(words, count);
}

//-----------------------------------------------------------------------------
// Sending with Scheduling
//-----------------------------------------------------------------------------

LIBREMIDI_INLINE
stdx::error ump_endpoint::schedule_ump(int64_t timestamp, const ump& message) const
{
  if (!m_impl)
    return std::errc::not_connected;

  return m_impl->schedule_ump(timestamp, message.data, 4);
}

LIBREMIDI_INLINE
int64_t ump_endpoint::current_time() const noexcept
{
  return m_impl ? m_impl->current_time() : 0;
}

//-----------------------------------------------------------------------------
// Sending to Specific Groups/Function Blocks
//-----------------------------------------------------------------------------

LIBREMIDI_INLINE
stdx::error ump_endpoint::send_to_group(uint8_t group, ump message) const
{
  if (!m_impl)
    return std::errc::not_connected;

  if (group > 15)
    return std::errc::invalid_argument;

  // Set group in message (bits 24-27 of first word)
  message.data[0] = (message.data[0] & 0xF0FFFFFF) | (static_cast<uint32_t>(group) << 24);

  return m_impl->send_ump(message.data, 4);
}

LIBREMIDI_INLINE
stdx::error ump_endpoint::send_to_block(uint8_t block_id, ump message) const
{
  if (!m_impl)
    return std::errc::not_connected;

  auto blocks = m_impl->get_function_blocks();
  for (const auto& block : blocks)
  {
    if (block.block_id == block_id)
    {
      return send_to_group(block.groups.first_group, message);
    }
  }

  return std::errc::invalid_argument;
}

//-----------------------------------------------------------------------------
// Function Block Access
//-----------------------------------------------------------------------------

LIBREMIDI_INLINE
std::vector<function_block_info> ump_endpoint::get_function_blocks() const noexcept
{
  return m_impl ? m_impl->get_function_blocks() : std::vector<function_block_info>{};
}

LIBREMIDI_INLINE
std::optional<function_block_info>
ump_endpoint::get_function_block(uint8_t block_id) const noexcept
{
  if (!m_impl)
    return std::nullopt;

  for (const auto& block : m_impl->get_function_blocks())
  {
    if (block.block_id == block_id)
      return block;
  }
  return std::nullopt;
}

//-----------------------------------------------------------------------------
// Protocol Information
//-----------------------------------------------------------------------------

LIBREMIDI_INLINE
midi_protocol ump_endpoint::get_active_protocol() const noexcept
{
  return m_impl ? m_impl->get_active_protocol() : midi_protocol::midi1;
}

LIBREMIDI_INLINE
stdx::error ump_endpoint::request_protocol(midi_protocol protocol) const
{
  if (!m_impl)
    return std::errc::not_connected;

  return m_impl->request_protocol(protocol);
}

LIBREMIDI_INLINE
bool ump_endpoint::jr_timestamps_enabled() const noexcept
{
  return m_impl && m_impl->jr_timestamps_enabled();
}

//-----------------------------------------------------------------------------
// MIDI-CI Support
//-----------------------------------------------------------------------------

LIBREMIDI_INLINE
stdx::error ump_endpoint::send_ci_discovery() const
{
  if (!m_impl)
    return std::errc::not_connected;

  return m_impl->send_ci_discovery();
}

LIBREMIDI_INLINE
bool ump_endpoint::supports_midi_ci() const noexcept
{
  return m_impl && m_impl->supports_midi_ci();
}

//-----------------------------------------------------------------------------
// Port Naming
//-----------------------------------------------------------------------------

LIBREMIDI_INLINE
stdx::error ump_endpoint::set_port_name(std::string_view name)
{
  if (!m_impl)
    return std::errc::not_connected;

  return m_impl->set_port_name(name);
}
*/
//=============================================================================
// Utility Functions
//=============================================================================

LIBREMIDI_INLINE
bool ump_backend_available() noexcept
{
  bool found = false;
  midi2::for_all_backends([&]<typename Backend>(Backend& backend) {
    // Check if backend supports the new endpoint API
    if constexpr (!std::is_void_v<typename Backend::midi_endpoint>)
    {
      found = true;
    }
  });
  return found;
}

LIBREMIDI_INLINE
libremidi::API default_ump_api() noexcept
{
  auto apis = available_ump_apis();
  for (const auto& api : apis)
  {
    // Check if this API supports the new endpoint API
    bool has_endpoint_support = false;
    midi2::for_backend(api, [&]<typename Backend>(Backend& backend) {
      if constexpr (!std::is_void_v<typename Backend::midi_endpoint>)
      {
        has_endpoint_support = true;
      }
    });
    if (has_endpoint_support)
      return api;
  }
  return libremidi::API::UNSPECIFIED;
}

LIBREMIDI_INLINE
std::vector<libremidi::API> available_ump_endpoint_apis() noexcept
{
  std::vector<libremidi::API> apis;
  midi2::for_all_backends([&]<typename Backend>(Backend& backend) {
    if constexpr (!std::is_void_v<typename Backend::midi_endpoint>)
    {
      apis.push_back(backend.API);
    }
  });
  return apis;
}

} // namespace libremidi
