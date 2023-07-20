#pragma once
#include <libremidi/backends/winmm/config.hpp>
#include <libremidi/backends/winmm/helpers.hpp>
#include <libremidi/detail/midi_out.hpp>

namespace libremidi
{

class midi_out_winmm final
    : public midi_out_api
    , public error_handler
{
public:
  struct
      : output_configuration
      , winmm_output_configuration
  {
  } configuration;

  midi_out_winmm(output_configuration&& conf, winmm_output_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    // We'll issue a warning here if no devices are available but not
    // throw an error since the user can plug something in later.
    unsigned int nDevices = get_port_count();
    if (nDevices == 0)
    {
      warning(
          configuration,
          "midi_out_winmm::initialize: no MIDI output devices currently "
          "available.");
    }
  }

  ~midi_out_winmm() override
  {
    // Close a connection if it exists.
    midi_out_winmm::close_port();
  }

  void open_virtual_port(std::string_view) override
  {
    warning(configuration, "midi_out_winmm: open_virtual_port unsupported");
  }
  void set_client_name(std::string_view) override
  {
    warning(configuration, "midi_out_winmm: set_client_name unsupported");
  }
  void set_port_name(std::string_view) override
  {
    warning(configuration, "midi_out_winmm: set_port_name unsupported");
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::WINDOWS_MM; }

  void open_port(unsigned int portNumber, std::string_view) override
  {
    if (connected_)
    {
      warning(configuration, "midi_out_winmm::open_port: a valid connection already exists!");
      return;
    }

    unsigned int nDevices = midiOutGetNumDevs();
    if (nDevices < 1)
    {
      error<no_devices_found_error>(
          configuration, "midi_out_winmm::open_port: no MIDI output destinations found!");
      return;
    }

    if (portNumber >= nDevices)
    {
      error<invalid_parameter_error>(
          configuration, "midi_out_winmm::open_port: invalid 'portNumber' argument: "
                             + std::to_string(portNumber));
      return;
    }

    MMRESULT result = midiOutOpen(&this->outHandle, portNumber, 0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR)
    {
      error<driver_error>(
          configuration,
          "midi_out_winmm::open_port: error creating Windows MM MIDI output "
          "port.");
      return;
    }

    connected_ = true;
  }

  void close_port() override
  {
    if (connected_)
    {
      midiOutClose(this->outHandle);
      this->outHandle = nullptr;
      connected_ = false;
    }
  }

  unsigned int get_port_count() const override { return midiOutGetNumDevs(); }

  std::string get_port_name(unsigned int portNumber) const override
  {
    unsigned int nDevices = midiOutGetNumDevs();
    if (portNumber >= nDevices)
    {
      error<invalid_parameter_error>(
          configuration, "midi_out_winmm::get_port_name: invalid 'portNumber' argument: "
                             + std::to_string(portNumber));
      return {};
    }

    MIDIOUTCAPS deviceCaps{};

    midiOutGetDevCaps(portNumber, &deviceCaps, sizeof(MIDIOUTCAPS));
    std::string stringName = ConvertToUTF8(deviceCaps.szPname);

#ifndef LIBREMIDI_DO_NOT_ENSURE_UNIQUE_PORTNAMES
    MakeUniqueOutPortName(stringName, portNumber);
#endif

    return stringName;
  }

  void send_message(const unsigned char* message, size_t size) override
  {
    if (!connected_)
      return;

    if (size == 0)
    {
      warning(configuration, "midi_out_winmm::send_message: message argument is empty!");
      return;
    }

    MMRESULT result;
    if (message[0] == 0xF0)
    { // Sysex message

      buffer.assign(message, message + size);

      // FIXME this can be made asynchronous... see Chrome source.
      // But need to know whe buffers are freed.

      // Create and prepare MIDIHDR structure.
      MIDIHDR sysex{};
      sysex.lpData = (LPSTR)buffer.data();
      sysex.dwBufferLength = size;
      sysex.dwFlags = 0;
      result = midiOutPrepareHeader(this->outHandle, &sysex, sizeof(MIDIHDR));
      if (result != MMSYSERR_NOERROR)
      {
        error<driver_error>(
            configuration, "midi_out_winmm::send_message: error preparing sysex header.");
        return;
      }

      // Send the message.
      result = midiOutLongMsg(this->outHandle, &sysex, sizeof(MIDIHDR));
      if (result != MMSYSERR_NOERROR)
      {
        error<driver_error>(
            configuration, "midi_out_winmm::send_message: error sending sysex message.");
        return;
      }

      // Unprepare the buffer and MIDIHDR.
      // FIXME yuck
      while (MIDIERR_STILLPLAYING
             == midiOutUnprepareHeader(this->outHandle, &sysex, sizeof(MIDIHDR)))
        Sleep(1);
    }
    else
    { // Channel or system message.

      // Make sure the message size isn't too big.
      if (size > 3)
      {
        warning(
            configuration,
            "midi_out_winmm::send_message: message size is greater than 3 bytes "
            "(and not sysex)!");
        return;
      }

      // Pack MIDI bytes into double word.
      DWORD packet;
      std::copy_n(message, size, (unsigned char*)&packet);

      // Send the message immediately.
      result = midiOutShortMsg(this->outHandle, packet);
      if (result != MMSYSERR_NOERROR)
      {
        error<driver_error>(
            configuration, "midi_out_winmm::send_message: error sending MIDI message.");
      }
    }
  }

private:
  HMIDIOUT outHandle; // Handle to Midi Output Device
  std::vector<char> buffer;
};

}
