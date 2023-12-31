#if !defined(LIBREMIDI_HEADER_ONLY)
  #include <libremidi/libremidi.hpp>
#endif

#include <libremidi/backends.hpp>
#include <libremidi/detail/midi_api.hpp>

#include <cassert>

namespace libremidi
{
LIBREMIDI_INLINE auto make_midi_in(auto base_conf, std::any api_conf, auto backends)
{
  std::unique_ptr<midi_in_api> ptr;

  assert(base_conf.on_message);

  auto from_api = [&]<typename T>(T& /*backend*/) mutable {
    if (auto conf = std::any_cast<typename T::midi_in_configuration>(&api_conf))
    {
      ptr = libremidi::make<typename T::midi_in>(std::move(base_conf), std::move(*conf));
      return true;
    }
    return false;
  };
  std::apply([&](auto&&... b) { (from_api(b) || ...); }, backends);
  return ptr;
}

LIBREMIDI_INLINE midi_in::midi_in(const input_configuration& base_conf) noexcept
{
  for (const auto& api : available_apis())
  {
    try
    {
      impl_ = make_midi_in(base_conf, midi_in_configuration_for(api), midi1::available_backends);
    }
    catch (const std::exception& e)
    {
    }

    if (impl_)
      return;
  }
  if (!impl_)
    impl_ = std::make_unique<midi_in_dummy>(input_configuration{}, dummy_configuration{});
}

LIBREMIDI_INLINE
midi_in::midi_in(input_configuration base_conf, std::any api_conf)
    : impl_{make_midi_in(base_conf, api_conf, midi1::available_backends)}
{
  if (!impl_)
    throw midi_exception("Could not open midi in for the given api");
}

LIBREMIDI_INLINE midi_in::midi_in(ump_input_configuration base_conf) noexcept
{
  for (const auto& api : available_apis())
  {
    try
    {
      impl_ = make_midi_in(base_conf, midi_in_configuration_for(api), midi2::available_backends);
    }
    catch (const std::exception& e)
    {
    }

    if (impl_)
      return;
  }
  if (!impl_)
    impl_ = std::make_unique<midi_in_dummy>(input_configuration{}, dummy_configuration{});
}

LIBREMIDI_INLINE
midi_in::midi_in(ump_input_configuration base_conf, std::any api_conf)
    : impl_{make_midi_in(base_conf, api_conf, midi2::available_backends)}
{
  if (!impl_)
    throw midi_exception("Could not open midi in for the given api");
}

LIBREMIDI_INLINE midi_in::~midi_in() = default;

LIBREMIDI_INLINE midi_in::midi_in(midi_in&& other) noexcept
    : impl_{std::move(other.impl_)}
{
  other.impl_
      = std::make_unique<libremidi::midi_in_dummy>(input_configuration{}, dummy_configuration{});
}

LIBREMIDI_INLINE
void midi_in::set_port_name(std::string_view portName)
{
  impl_->set_port_name(portName);
}

LIBREMIDI_INLINE midi_in& midi_in::operator=(midi_in&& other) noexcept
{
  this->impl_ = std::move(other.impl_);
  other.impl_
      = std::make_unique<libremidi::midi_in_dummy>(input_configuration{}, dummy_configuration{});
  return *this;
}

LIBREMIDI_INLINE
libremidi::API midi_in::get_current_api() const noexcept
{
  return impl_->get_current_api();
}

LIBREMIDI_INLINE
void midi_in::open_port(const input_port& port, std::string_view portName)
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
void midi_in::open_virtual_port(std::string_view portName)
{
  if (impl_->is_port_open())
    return;

  if (impl_->open_virtual_port(portName))
  {
    impl_->port_open_ = true;
  }
}

LIBREMIDI_INLINE
void midi_in::close_port()
{
  impl_->close_port();

  impl_->connected_ = false;
  impl_->port_open_ = false;
}

LIBREMIDI_INLINE
bool midi_in::is_port_open() const noexcept
{
  return impl_->is_port_open();
}

LIBREMIDI_INLINE
bool midi_in::is_port_connected() const noexcept
{
  return impl_->is_port_connected();
}
}
