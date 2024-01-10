#if !defined(LIBREMIDI_HEADER_ONLY)
  #include <libremidi/libremidi.hpp>
#endif

#include <libremidi/backends.hpp>
#include <libremidi/detail/midi_api.hpp>

#include <array>
#include <cassert>

namespace libremidi
{
LIBREMIDI_INLINE auto make_midi_out(auto base_conf, std::any api_conf)
{
  std::unique_ptr<midi_out_api> ptr;
  auto from_api = [&]<typename T>(T& /*backend*/) mutable {
    if (auto conf = std::any_cast<typename T::midi_out_configuration>(&api_conf))
    {
      ptr = libremidi::make<typename T::midi_out>(std::move(base_conf), std::move(*conf));
      return true;
    }
    return false;
  };
  std::apply([&](auto&&... b) { (from_api(b) || ...); }, midi1::available_backends);
  std::apply([&](auto&&... b) { (from_api(b) || ...); }, midi2::available_backends);
  return ptr;
}

LIBREMIDI_INLINE midi_out::midi_out(const output_configuration& base_conf) noexcept
{
  for (const auto& api : available_apis())
  {
    try
    {
      impl_ = make_midi_out(base_conf, midi_out_configuration_for(api));
    }
    catch (const std::exception& e)
    {
    }

    if (impl_)
      return;
  }
  if (!impl_)
    impl_ = std::make_unique<midi_out_dummy>(output_configuration{}, dummy_configuration{});
}

LIBREMIDI_INLINE
midi_out::midi_out(output_configuration base_conf, std::any api_conf)
    : impl_{make_midi_out(base_conf, api_conf)}
{
  if (!impl_)
    throw midi_exception("Could not open midi out for the given api");
}

LIBREMIDI_INLINE midi_out::~midi_out() = default;

LIBREMIDI_INLINE midi_out::midi_out(midi_out&& other) noexcept
    : impl_{std::move(other.impl_)}
{
  other.impl_
      = std::make_unique<libremidi::midi_out_dummy>(output_configuration{}, dummy_configuration{});
}
LIBREMIDI_INLINE midi_out& midi_out::operator=(midi_out&& other) noexcept
{
  this->impl_ = std::move(other.impl_);
  other.impl_
      = std::make_unique<libremidi::midi_out_dummy>(output_configuration{}, dummy_configuration{});
  return *this;
}

LIBREMIDI_INLINE
void midi_out::set_port_name(std::string_view portName) const
{
  impl_->set_port_name(portName);
}

LIBREMIDI_INLINE
libremidi::API midi_out::get_current_api() const noexcept
{
  return impl_->get_current_api();
}

LIBREMIDI_INLINE
void midi_out::open_port(const output_port& port, std::string_view portName) const
{
  if (impl_->is_port_open())
    return;

  if (impl_->open_port(port, portName))
  {
    impl_->connected_ = true;
    impl_->port_open_ = true;
  }
}

LIBREMIDI_INLINE
void midi_out::open_virtual_port(std::string_view portName) const
{
  if (impl_->is_port_open())
    return;

  if (impl_->open_virtual_port(portName))
  {
    impl_->port_open_ = true;
  }
}

LIBREMIDI_INLINE
void midi_out::close_port() const
{
  impl_->close_port();
  impl_->connected_ = false;
  impl_->port_open_ = false;
}

LIBREMIDI_INLINE
bool midi_out::is_port_open() const noexcept
{
  return impl_->is_port_open();
}

LIBREMIDI_INLINE
bool midi_out::is_port_connected() const noexcept
{
  return impl_->is_port_connected();
}

LIBREMIDI_INLINE
void midi_out::send_message(const libremidi::message& message) const
{
  send_message(message.bytes.data(), message.bytes.size());
}

LIBREMIDI_INLINE
void midi_out::send_message(std::span<const unsigned char> message) const
{
  send_message(message.data(), message.size());
}

LIBREMIDI_INLINE
void midi_out::send_message(unsigned char b0) const
{
  send_message(&b0, 1);
}

LIBREMIDI_INLINE
void midi_out::send_message(unsigned char b0, unsigned char b1) const
{
  send_message(std::to_array({b0, b1}));
}

LIBREMIDI_INLINE
void midi_out::send_message(unsigned char b0, unsigned char b1, unsigned char b2) const
{
  send_message(std::to_array({b0, b1, b2}));
}

LIBREMIDI_INLINE
void midi_out::send_message(const unsigned char* message, size_t size) const
{
#if defined(LIBREMIDI_ASSERTIONS)
  assert(size > 0);
#endif

  impl_->send_message(message, size);
}

LIBREMIDI_INLINE
void midi_out::send_ump(const uint32_t* message, size_t size) const
{
#if defined(LIBREMIDI_ASSERTIONS)
  assert(size > 0);
  assert(size <= 4);
#endif

  impl_->send_ump(message, size);
}
LIBREMIDI_INLINE
void midi_out::send_ump(const libremidi::ump& message) const
{
  send_ump(message.bytes, message.size());
}

LIBREMIDI_INLINE
void midi_out::send_ump(std::span<const uint32_t> message) const
{
  send_ump(message.data(), message.size());
}

LIBREMIDI_INLINE
void midi_out::send_ump(uint32_t b0) const
{
  send_ump(&b0, 1);
}

LIBREMIDI_INLINE
void midi_out::send_ump(uint32_t b0, uint32_t b1) const
{
  send_ump(std::to_array({b0, b1}));
}

LIBREMIDI_INLINE
void midi_out::send_ump(uint32_t b0, uint32_t b1, uint32_t b2) const
{
  send_ump(std::to_array({b0, b1, b2}));
}

LIBREMIDI_INLINE
void midi_out::send_ump(uint32_t b0, uint32_t b1, uint32_t b2, uint32_t b3) const
{
  send_ump(std::to_array({b0, b1, b2, b3}));
}

}
