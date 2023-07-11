#pragma once
#include <libremidi/backends/winmm/config.hpp>
#include <libremidi/detail/midi_out.hpp>

namespace libremidi
{

class midi_out_winmm final : public midi_out_default<midi_out_winmm>
{
public:
  static const constexpr auto backend = "WinMM";
  explicit midi_out_winmm(std::string_view)
  {
    // We'll issue a warning here if no devices are available but not
    // throw an error since the user can plug something in later.
    unsigned int nDevices = get_port_count();
    if (nDevices == 0)
    {
      warning(
          "midi_out_winmm::initialize: no MIDI output devices currently "
          "available.");
    }
  }

  ~midi_out_winmm() override
  {
    // Close a connection if it exists.
    midi_out_winmm::close_port();
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::WINDOWS_MM; }

  void open_port(unsigned int portNumber, std::string_view) override
  {
    if (connected_)
    {
      warning("midi_out_winmm::open_port: a valid connection already exists!");
      return;
    }

    unsigned int nDevices = midiOutGetNumDevs();
    if (nDevices < 1)
    {
      error<no_devices_found_error>(
          "midi_out_winmm::open_port: no MIDI output destinations found!");
      return;
    }

    if (portNumber >= nDevices)
    {
      std::ostringstream ost;
      ost << "midi_out_winmm::open_port: the 'portNumber' argument (" << portNumber
          << ") is invalid.";
      error<invalid_parameter_error>(ost.str());
      return;
    }

    MMRESULT result = midiOutOpen(&data.outHandle, portNumber, NULL, NULL, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR)
    {
      error<driver_error>(
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
      midiOutClose(data.outHandle);
      data.outHandle = nullptr;
      connected_ = false;
    }
  }

  unsigned int get_port_count() override { return midiOutGetNumDevs(); }

  std::string get_port_name(unsigned int portNumber) override
  {
    std::string stringName;
    unsigned int nDevices = midiOutGetNumDevs();
    if (portNumber >= nDevices)
    {
      std::ostringstream ost;
      ost << "midi_out_winmm::get_port_name: the 'portNumber' argument (" << portNumber
          << ") is invalid.";
      warning(ost.str());
      return stringName;
    }

    MIDIOUTCAPS deviceCaps;

    midiOutGetDevCaps(portNumber, &deviceCaps, sizeof(MIDIOUTCAPS));
    stringName = ConvertToUTF8(deviceCaps.szPname);

#ifndef LIBREMIDI_DO_NOT_ENSURE_UNIQUE_PORTNAMES
    MakeUniqueOutPortName(stringName, portNumber);
#endif

    return stringName;
  }

  void send_message(const unsigned char* message, size_t size) override
  {
    if (!connected_)
      return;

    unsigned int nBytes = static_cast<unsigned int>(size);
    if (nBytes == 0)
    {
      warning("midi_out_winmm::send_message: message argument is empty!");
      return;
    }

    MMRESULT result;
    if (message[0] == 0xF0)
    { // Sysex message

      // Allocate buffer for sysex data.
      buffer.clear();
      buffer.resize(nBytes);

      // Copy data to buffer.
      for (unsigned int i = 0; i < nBytes; ++i)
        buffer[i] = message[i];

      // Create and prepare MIDIHDR structure.
      MIDIHDR sysex{};
      sysex.lpData = (LPSTR)buffer.data();
      sysex.dwBufferLength = nBytes;
      sysex.dwFlags = 0;
      result = midiOutPrepareHeader(data.outHandle, &sysex, sizeof(MIDIHDR));
      if (result != MMSYSERR_NOERROR)
      {
        error<driver_error>("midi_out_winmm::send_message: error preparing sysex header.");
        return;
      }

      // Send the message.
      result = midiOutLongMsg(data.outHandle, &sysex, sizeof(MIDIHDR));
      if (result != MMSYSERR_NOERROR)
      {
        error<driver_error>("midi_out_winmm::send_message: error sending sysex message.");
        return;
      }

      // Unprepare the buffer and MIDIHDR.
      while (MIDIERR_STILLPLAYING
             == midiOutUnprepareHeader(data.outHandle, &sysex, sizeof(MIDIHDR)))
        Sleep(1);
    }
    else
    { // Channel or system message.

      // Make sure the message size isn't too big.
      if (nBytes > 3)
      {
        warning(
            "midi_out_winmm::send_message: message size is greater than 3 bytes "
            "(and not sysex)!");
        return;
      }

      // Pack MIDI bytes into double word.
      DWORD packet;
      unsigned char* ptr = (unsigned char*)&packet;
      for (unsigned int i = 0; i < nBytes; ++i)
      {
        *ptr = message[i];
        ++ptr;
      }

      // Send the message immediately.
      result = midiOutShortMsg(data.outHandle, packet);
      if (result != MMSYSERR_NOERROR)
      {
        error<driver_error>("midi_out_winmm::send_message: error sending MIDI message.");
      }
    }
  }

private:
  WinMidiData data;
  std::vector<char> buffer;
};

}
