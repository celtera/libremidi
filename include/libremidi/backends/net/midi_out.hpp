#pragma once
#include <libremidi/backends/net/config.hpp>
#include <libremidi/backends/net/helpers.hpp>
#include <libremidi/detail/midi_out.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/endian.hpp>

namespace libremidi::net
{

class midi1_out_impl final
    : public midi1::out_api
    , public error_handler
{
public:
  struct
      : output_configuration
      , net_dgram_output_configuration
  {
  } configuration;

  midi1_out_impl(output_configuration&& conf, net_dgram_output_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
      , ctx{new boost::asio::io_context}
  {
    m_socket.open(boost::asio::ip::udp::v4());
    m_socket.set_option(boost::asio::ip::udp::socket::reuse_address(true));
    m_socket.set_option(boost::asio::socket_base::broadcast(true));
  }

  ~midi_out_impl() override
  {
    close_port();

    disconnect(*this);
  }

  stdx::error open_port(const output_port& port, std::string_view portName) override
  {
    const char* host = "127.0.0.1:1234";
    const char* port = "/midi";
    /*
    if (auto err = create_local_port(*this, portName, JackPortIsOutput); err != stdx::error{})
      return err;
    
    // Connecting to the output
    if (int err = jack_connect(this->client, jack_port_name(this->port), port.port_name.c_str());
        err != 0 && err != EEXIST)
    {
      libremidi_handle_error(
          configuration, "could not connect to port" + port.port_name);
      return from_errc(err);
    }
    */

    return stdx::error{};
  }

  stdx::error open_virtual_port(std::string_view portName) override
  {
    // Random arbitrary limit to avoid abuse
    if (portName.size() >= 512)
      return std::errc::invalid_argument;

    int i = 0;
    for (; i < portName.size(); i++)
      if (portName.data()[i] != 0)
        bytes.data[i] = portName.data()[i];
      else
        break;
    bytes.data[i] = 0;
    while (i % 4 != 3)
    {
      ++i;
      bytes.data[i] = 0;
    }

    bytes.data[++i] = ',';
    bytes.data[++i] = 'm';
    bytes.data[++i] = 0;
    bytes.data[++i] = 0;
    osc_header_size = i;

    return stdx::error{};
  }

  stdx::error close_port() override { osc_header_size = 0; }

  stdx::error send_message(const unsigned char* message, size_t size) override
  {
    if (osc_header_size == 0)
      return std::errc::not_connected;

    int ret = 0;
    if (size > 65507)
      return std::errc::message_size;

    auto time = std::chrono::system_clock::now();

    std::memcpy(this->msg.data + osc_header_size, message, size);
    bytes.byte_size = size + osc_header_size;

    return from_errc(ret);
  }

  struct msg
  {
    char bundle_text[8] = "\0#bundle";
    boost::endian::big_int64_t timestamp = 0;
    boost::endian::big_int32_t byte_size = 0;
    char data[65535];
  };
  static_assert(sizeof(msg) == 8 + 8 + 4 + 65535);

  stdx::error schedule_message(int64_t ts, const unsigned char* message, size_t size) override
  {
    int ret = 0;
    return from_errc(ret);
  }

  boost::asio::io_context* ctx{};
  boost::asio::ip::udp::socket sock;
  msg bytes;
  int osc_header_size{};
};

}
