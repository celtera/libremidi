#pragma once
#define NOMINMAX 1
#define WIN32_LEAN_AND_MEAN 1
#include <rtmidi17/detail/midi_api.hpp>
#include <rtmidi17/rtmidi17.hpp>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Midi.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Storage.Streams.h>

namespace rtmidi
{
inline void winrt_init()
{ static auto _ = [] { winrt::init_apartment(); return 0; }(); }


class observer_winuwp final : public observer_api
{
  public:
    observer_winuwp(observer::callbacks&& c) : observer_api{std::move(c)}
    {
      winrt_init();
    }

    ~observer_winuwp()
    {
    }
};

class midi_in_winuwp final : public midi_in_default<midi_in_winuwp>
{
  public:
    static const constexpr auto backend = "UWP";
    midi_in_winuwp(std::string_view, unsigned int queueSizeLimit)
      : midi_in_default{nullptr, queueSizeLimit}
    {
      winrt_init();
      using namespace winrt;
      using namespace winrt::Windows::Foundation;
      using namespace winrt::Windows::Devices::Midi;
      using namespace winrt::Windows::Devices::Enumeration;
      using namespace winrt::Windows::Storage::Streams;
    }

    ~midi_in_winuwp() override
    {
      if(port_)
        port_.Close();
    }

    rtmidi::API get_current_api() const noexcept override
    {
      return rtmidi::API::WINDOWS_UWP;
    }

    void open_port(unsigned int portNumber, std::string_view) override
    {
      using namespace winrt;
      using namespace winrt::Windows::Foundation;
      using namespace winrt::Windows::Devices::Midi;
      using namespace winrt::Windows::Devices::Enumeration;
      using namespace winrt::Windows::Storage::Streams;
      if (connected_)
      {
        warning("MidiInWinMM::openPort: a valid connection already exists!");
        return;
      }

      auto devs = DeviceInformation::FindAllAsync(MidiInPort::GetDeviceSelector()).get();
      if(portNumber < devs.Size())
      {
        auto p = devs.GetAt(portNumber);
        if(p)
        {
          port_ = MidiInPort::FromIdAsync(p.Id()).get();
          if(port_)
          {
            port_.MessageReceived(
                  [=] (auto&, auto args)
            {
              const auto& msg = args.Message();

              auto reader = DataReader::FromBuffer(msg.RawData());
              array_view<uint8_t> bs;
              reader.ReadBytes(bs);

              double t = msg.Timestamp().count();

              rtmidi::message m{{bs.begin(), bs.end()}, t};
              if(inputData_.userCallback)
              {
                inputData_.userCallback(m);
              }
              else
              {
                if(!inputData_.queue.push(m))
                  std::cerr << "\nmidi_in_winuwp: message queue limit reached!!\n\n";
              }
            });
          }
        }
      }
    }

    void close_port() override
    {
      if (connected_)
      {
        if(port_)
        {
          port_.Close();
        }
      }
    }

    unsigned int get_port_count() override
    {
      using namespace winrt;
      using namespace winrt::Windows::Foundation;
      using namespace winrt::Windows::Devices::Midi;
      using namespace winrt::Windows::Devices::Enumeration;
      using namespace winrt::Windows::Storage::Streams;
      auto devs = DeviceInformation::FindAllAsync(MidiInPort::GetDeviceSelector());
      return devs.get().Size();
    }

    std::string get_port_name(unsigned int portNumber) override
    {
      using namespace winrt;
      using namespace winrt::Windows::Foundation;
      using namespace winrt::Windows::Devices::Midi;
      using namespace winrt::Windows::Devices::Enumeration;
      auto devs = DeviceInformation::FindAllAsync(MidiInPort::GetDeviceSelector()).get();
      if(portNumber < devs.Size())
      {
        auto port = devs.GetAt(portNumber);
        if(port)
        {
          return winrt::to_string(port.Name());
        }
      }

      return {};
    }

  private:
    winrt::Windows::Devices::Midi::MidiInPort port_{nullptr};
};

class midi_out_winuwp final : public midi_out_default<midi_out_winuwp>
{
  public:
    static const constexpr auto backend = "UWP";
    midi_out_winuwp(std::string_view)
    {
      winrt_init();
    }

    ~midi_out_winuwp() override
    {
      close_port();
    }

    rtmidi::API get_current_api() const noexcept override
    {
      return rtmidi::API::WINDOWS_UWP;
    }

    void open_port(unsigned int portNumber, std::string_view) override
    {
      using namespace winrt;
      using namespace winrt::Windows::Foundation;
      using namespace winrt::Windows::Devices::Midi;
      using namespace winrt::Windows::Devices::Enumeration;
      using namespace winrt::Windows::Storage::Streams;
      if (connected_)
      {
        warning("midi_out_winuwp::open_port: a valid connection already exists!");
        return;
      }

      auto devs = DeviceInformation::FindAllAsync(MidiOutPort::GetDeviceSelector()).get();
      if(portNumber < devs.Size())
      {
        auto p = devs.GetAt(portNumber);
        if(p)
        {
          port_ = MidiOutPort::FromIdAsync(p.Id()).get();
        }
      }
    }

    void close_port() override
    {
      if (connected_)
      {
        if(port_)
        {
          port_.Close();
        }
      }
    }

    unsigned int get_port_count() override
    {
      using namespace winrt;
      using namespace winrt::Windows::Foundation;
      using namespace winrt::Windows::Devices::Midi;
      using namespace winrt::Windows::Devices::Enumeration;
      using namespace winrt::Windows::Storage::Streams;
      auto devs = DeviceInformation::FindAllAsync(MidiOutPort::GetDeviceSelector());
      return devs.get().Size();
    }

    std::string get_port_name(unsigned int portNumber) override
    {
      using namespace winrt;
      using namespace winrt::Windows::Foundation;
      using namespace winrt::Windows::Devices::Midi;
      using namespace winrt::Windows::Devices::Enumeration;
      auto devs = DeviceInformation::FindAllAsync(MidiOutPort::GetDeviceSelector()).get();
      if(portNumber < devs.Size())
      {
        auto port = devs.GetAt(portNumber);
        if(port)
        {
          return winrt::to_string(port.Name());
        }
      }

      return {};
    }

    void send_message(const unsigned char* message, size_t size) override
    {
      if (!connected_)
        return;

      using namespace winrt;
      using namespace winrt::Windows::Foundation;
      using namespace winrt::Windows::Storage::Streams;
      InMemoryRandomAccessStream str;
      DataWriter rb(str);
      rb.WriteBytes(winrt::array_view<const uint8_t>{(const uint8_t*)message, (const uint8_t*)message + size});
      port_.SendBuffer(rb.DetachBuffer());
    }

  private:
    winrt::Windows::Devices::Midi::IMidiOutPort port_{nullptr};
};

struct winuwp_backend
{
    using midi_in = midi_in_winuwp;
    using midi_out = midi_out_winuwp;
    using midi_observer = observer_winuwp;
    static const constexpr auto API = rtmidi::API::WINDOWS_UWP;
};
}
