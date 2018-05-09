#pragma once
#define NOMINMAX 1
#define WIN32_LEAN_AND_MEAN 1
#include <rtmidi17/detail/midi_api.hpp>
#include <rtmidi17/rtmidi17.hpp>
#include <ostream>
#include <sstream>
#include <Windows.h>
#include <mmsystem.h>

namespace rtmidi
{
struct UWPMidiData
{
};

class observer_winuwp final : public observer_api
{
  public:
    observer_winuwp(observer::callbacks&& c) : observer_api{std::move(c)}
    {
    }

    ~observer_winuwp()
    {
    }
};

class midi_in_winuwp final : public midi_in_api
{
  public:
    midi_in_winmm(const std::string& clientName, unsigned int queueSizeLimit)
      : midi_in_api(queueSizeLimit)
    {
    }

    ~midi_in_winmm() override
    {
    }

    rtmidi::API get_current_api() const noexcept override
    {
      return rtmidi::API::WINDOWS_UWP;
    }

    void open_port(unsigned int portNumber, const std::string& portName) override
    {
      if (connected_)
      {
        warning("MidiInWinMM::openPort: a valid connection already exists!");
        return;
      }
    }
    void open_virtual_port(const std::string& portName) override
    {
    }

    void close_port() override
    {
      if (connected_)
      {
      }
    }

    void set_client_name(const std::string& clientName) override
    {
    }

    void set_port_name(const std::string& portName) override
    {
    }

    unsigned int get_port_count() override
    {
      return 0;
    }

    std::string get_port_name(unsigned int portNumber) override
    {
      return "";
    }

  private:
    WinMidiData data;
};

class midi_out_winmm final : public midi_out_api
{
  public:
    midi_out_winmm(const std::string& clientName)
    {
    }

    ~midi_out_winmm() override
    {
      midi_out_winmm::close_port();
    }

    rtmidi::API get_current_api() const noexcept override
    {
      return rtmidi::API::WINDOWS_UWP;
    }

    void open_port(unsigned int portNumber, const std::string& portName) override
    {
    }

    void open_virtual_port(const std::string& portName) override
    {
    }

    void close_port() override
    {
      if (connected_)
      {
      }
    }

    void set_client_name(const std::string& clientName) override
    {
    }
    void set_port_name(const std::string& portName) override
    {
    }

    unsigned int get_port_count() override
    {
      return 0;
    }

    std::string get_port_name(unsigned int portNumber) override
    {
      return "";
    }

    void send_message(const unsigned char* message, size_t size) override
    {
      if (!connected_)
        return;
    }

  private:
    WinMidiData data;
};
}
