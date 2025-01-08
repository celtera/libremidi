#if !defined(LIBREMIDI_HEADER_ONLY)
  #include <libremidi/libremidi.hpp>
#endif

#include <libremidi/backends.hpp>
#include <libremidi/detail/midi_api.hpp>

namespace libremidi
{

static LIBREMIDI_INLINE std::unique_ptr<observer_api> make_observer(auto base_conf)
{
  for (const auto& api : available_apis())
  {
    try
    {
      if (auto impl_ = make_observer(base_conf, observer_configuration_for(api)))
        return impl_;
    }
    catch (const std::exception& e)
    {
    }
  }

  for (const auto& api : available_ump_apis())
  {
    try
    {
      if (auto impl_ = make_observer(base_conf, observer_configuration_for(api)))
        return impl_;
    }
    catch (const std::exception& e)
    {
    }
  }

  return std::make_unique<observer_dummy>(observer_configuration{}, dummy_configuration{});
}

LIBREMIDI_INLINE auto make_observer_impl(auto base_conf, observer_api_configuration api_conf)
{
  std::unique_ptr<observer_api> ptr;
  auto from_api = [&]<typename T>(T& /*backend*/) mutable {
    if (auto conf = std::get_if<typename T::midi_observer_configuration>(&api_conf))
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

LIBREMIDI_INLINE std::unique_ptr<observer_api>
make_observer(auto base_conf, observer_api_configuration api_conf)
{
  if (std::get_if<unspecified_configuration>(&api_conf))
  {
    return make_observer(base_conf);
  }
  else if (auto api_p = std::get_if<libremidi::API>(&api_conf))
  {
    if (*api_p == libremidi::API::UNSPECIFIED)
    {
      return make_observer(base_conf);
    }
    else
    {
      return make_observer_impl(base_conf, observer_configuration_for(*api_p));
    }
  }
  else
  {
    if (auto api = libremidi::midi_api(api_conf); api == libremidi::API::UNSPECIFIED)
      return {};
    else
      return make_observer_impl(base_conf, api_conf);
  }
}

LIBREMIDI_INLINE observer::observer(const observer_configuration& base_conf) noexcept
    : impl_{make_observer(base_conf)}
{
}

LIBREMIDI_INLINE
observer::observer(observer_configuration base_conf, observer_api_configuration api_conf)
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
