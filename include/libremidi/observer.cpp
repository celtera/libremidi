#if !defined(LIBREMIDI_HEADER_ONLY)
  #include <libremidi/libremidi.hpp>
#endif

#include <libremidi/backends.hpp>
#include <libremidi/detail/midi_api.hpp>

namespace libremidi
{
LIBREMIDI_INLINE auto make_observer(auto base_conf, std::any api_conf)
{
  std::unique_ptr<observer_api> ptr;
  auto from_api = [&]<typename T>(T& /*backend*/) mutable {
    if (auto conf = std::any_cast<typename T::midi_observer_configuration>(&api_conf))
    {
      ptr = libremidi::make<typename T::midi_observer>(std::move(base_conf), std::move(*conf));
      return true;
    }
    return false;
  };
  std::apply([&](auto&&... b) { (from_api(b) || ...); }, midi1::available_backends);
  std::apply([&](auto&&... b) { (from_api(b) || ...); }, midi2::available_backends);
  return ptr;
}

LIBREMIDI_INLINE observer::observer(const observer_configuration& base_conf) noexcept
{
  for (const auto& api : available_apis())
  {
    try
    {
      impl_ = make_observer(base_conf, observer_configuration_for(api));
    }
    catch (const std::exception& e)
    {
    }

    if (impl_)
      return;
  }

  if (!impl_)
    impl_ = std::make_unique<observer_dummy>(observer_configuration{}, dummy_configuration{});
}

LIBREMIDI_INLINE observer::observer(observer_configuration base_conf, std::any api_conf)
    : impl_{make_observer(base_conf, api_conf)}
{
  if (!impl_)
  {
    error_handler e;
    e.libremidi_handle_error(base_conf, "Could not open observer for the given api");
    impl_ = std::make_unique<observer_dummy>(observer_configuration{}, dummy_configuration{});
  }
}

LIBREMIDI_INLINE observer::observer(observer&& other) noexcept
    : impl_{std::move(other.impl_)}
{
  other.impl_ = std::make_unique<libremidi::observer_dummy>(
      observer_configuration{}, dummy_configuration{});
}

LIBREMIDI_INLINE observer& observer::operator=(observer&& other) noexcept
{
  this->impl_ = std::move(other.impl_);
  other.impl_ = std::make_unique<libremidi::observer_dummy>(
      observer_configuration{}, dummy_configuration{});
  return *this;
}

LIBREMIDI_INLINE
observer::~observer() = default;

LIBREMIDI_INLINE
libremidi::API observer::get_current_api() const noexcept
{
  return impl_->get_current_api();
}

LIBREMIDI_INLINE
std::vector<libremidi::input_port> observer::get_input_ports() const noexcept
{
  return impl_->get_input_ports();
}

LIBREMIDI_INLINE
std::vector<libremidi::output_port> observer::get_output_ports() const noexcept
{
  return impl_->get_output_ports();
}
}
