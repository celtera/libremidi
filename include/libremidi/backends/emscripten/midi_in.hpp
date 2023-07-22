#pragma once
#include <libremidi/backends/emscripten/config.hpp>
#include <libremidi/backends/emscripten/helpers.hpp>
#include <libremidi/detail/midi_in.hpp>

namespace libremidi
{
class midi_in_emscripten final
    : public midi_in_api
    , public error_handler
{
public:
  struct
      : input_configuration
      , emscripten_input_configuration
  {
  } configuration;

  midi_in_emscripten(input_configuration&& conf, emscripten_input_configuration&& apiconf);
  ~midi_in_emscripten() override;

  libremidi::API get_current_api() const noexcept override;

  void open_port(unsigned int portNumber, std::string_view) override;
  void open_virtual_port(std::string_view) override;
  void close_port() override;

  void set_client_name(std::string_view clientName) override;
  void set_port_name(std::string_view portName) override;

  void on_input(libremidi::message msg);

  unsigned int get_port_count() const override;
  std::string get_port_name(unsigned int portNumber) const override;

private:
  int portNumber_{};
};
}
