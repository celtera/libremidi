#pragma once
#include <libremidi/detail/midi_api.hpp>
#include <libremidi/output_configuration.hpp>

#include <string_view>

namespace libremidi
{

class midi_out_api : public midi_api
{
public:
  virtual void send_message(const unsigned char* message, size_t size) = 0;
};

template <typename T, typename Arg>
std::unique_ptr<midi_out_api> make(libremidi::output_configuration&& conf, Arg&& arg)
{
  return std::make_unique<T>(std::move(conf), std::move(arg));
}
}
