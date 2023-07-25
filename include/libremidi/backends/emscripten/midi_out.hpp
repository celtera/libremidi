#pragma once
#include <libremidi/backends/emscripten/config.hpp>
#include <libremidi/backends/emscripten/helpers.hpp>
#include <libremidi/detail/midi_out.hpp>

namespace libremidi
{
class midi_out_emscripten final
    : public midi_out_api
    , public error_handler
{
public:
  struct
      : output_configuration
      , emscripten_output_configuration
  {
  } configuration;

  midi_out_emscripten(output_configuration&& conf, emscripten_output_configuration&& apiconf);
  ~midi_out_emscripten() override;

  libremidi::API get_current_api() const noexcept override;

  void open_port(unsigned int portNumber, std::string_view) override;
  void open_port(const port_information& p, std::string_view) override;
  void open_virtual_port(std::string_view) override;
  void close_port() override;

  void set_client_name(std::string_view clientName) override;
  void set_port_name(std::string_view portName) override;

  unsigned int get_port_count() const override;
  std::string get_port_name(unsigned int portNumber) const override;

  void send_message(const unsigned char* message, size_t size) override;

private:
  int portNumber_{};
};
}
