#pragma once
#include <libremidi/backends/winuwp/config.hpp>
#include <libremidi/detail/midi_in.hpp>

namespace libremidi
{

class midi_in_winuwp final : public midi_in_default<midi_in_winuwp>
{
public:
  static const constexpr auto backend = "UWP";
  explicit midi_in_winuwp(std::string_view)
      : midi_in_default{nullptr}
  {
    winrt_init();
  }

  ~midi_in_winuwp() override
  {
    if (port_)
      port_.Close();
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::WINDOWS_UWP; }

  void open_port(unsigned int portNumber, std::string_view) override
  {
    if (connected_)
    {
      warning("midi_in_winuwp::open_port: a valid connection already exists!");
      return;
    }

    const auto id = get_port_id(portNumber);
    if (!id.empty())
    {
      port_ = get(MidiInPort::FromIdAsync(id));
      if (port_)
      {
        port_.MessageReceived([=](auto&, auto args) {
          const auto& msg = args.Message();

          auto reader = DataReader::FromBuffer(msg.RawData());
          array_view<uint8_t> bs;
          reader.ReadBytes(bs);

          double t = static_cast<double>(msg.Timestamp().count());
          inputData_.on_message_received(libremidi::message{{bs.begin(), bs.end()}, t});
        });
      }
    }
  }

  void close_port() override
  {
    if (connected_)
    {
      if (port_)
      {
        port_.Close();
      }
    }
  }

  unsigned int get_port_count() override
  {
    auto& observer = observer_winuwp::get_internal_in_port_observer();
    return observer.get_port_count();
  }

  std::string get_port_name(unsigned int portNumber) override
  {
    auto& observer = observer_winuwp::get_internal_in_port_observer();
    return observer.get_port_name(portNumber);
  }

private:
  hstring get_port_id(unsigned int portNumber)
  {
    auto& observer = observer_winuwp::get_internal_in_port_observer();
    return observer.get_port_id(portNumber);
  }

private:
  winrt::Windows::Devices::Midi::MidiInPort port_{nullptr};
};

}
