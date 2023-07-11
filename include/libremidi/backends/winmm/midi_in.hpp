#pragma once
#include <libremidi/backends/winmm/config.hpp>
#include <libremidi/detail/midi_in.hpp>

namespace libremidi
{

class midi_in_winmm final : public midi_in_default<midi_in_winmm>
{
public:
  static const constexpr auto backend = "WinMM";
  midi_in_winmm(std::string_view, unsigned int queueSizeLimit)
      : midi_in_default{&data, queueSizeLimit}
  {
    // We'll issue a warning here if no devices are available but not
    // throw an error since the user can plugin something later.
    unsigned int nDevices = get_port_count();
    if (nDevices == 0)
    {
      warning("midi_in_winmm::initialize: no MIDI input devices currently available.");
    }

    if (!InitializeCriticalSectionAndSpinCount(&(data._mutex), 0x00000400))
    {
      warning("midi_in_winmm::initialize: InitializeCriticalSectionAndSpinCount failed.");
    }
  }

  ~midi_in_winmm() override
  {
    // Close a connection if it exists.
    midi_in_winmm::close_port();

    DeleteCriticalSection(&(data._mutex));
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::WINDOWS_MM; }

  void open_port(unsigned int portNumber, std::string_view) override
  {
    if (connected_)
    {
      warning("midi_in_winmm::open_port: a valid connection already exists!");
      return;
    }

    unsigned int nDevices = midiInGetNumDevs();
    if (nDevices == 0)
    {
      error<no_devices_found_error>("midi_in_winmm::open_port: no MIDI input sources found!");
      return;
    }

    if (portNumber >= nDevices)
    {
      std::ostringstream ost;
      ost << "midi_in_winmm::open_port: the 'portNumber' argument (" << portNumber
          << ") is invalid.";
      error<invalid_parameter_error>(ost.str());
      return;
    }

    MMRESULT result = midiInOpen(
        &data.inHandle, portNumber, (DWORD_PTR)&midiInputCallback, (DWORD_PTR)&inputData_,
        CALLBACK_FUNCTION);
    if (result != MMSYSERR_NOERROR)
    {
      error<driver_error>("midi_in_winmm::open_port: error creating Windows MM MIDI input port.");
      return;
    }

    // Allocate and init the sysex buffers.
    for (int i = 0; i < RT_SYSEX_BUFFER_COUNT; ++i)
    {
      data.sysexBuffer[i] = (MIDIHDR*)new char[sizeof(MIDIHDR)];
      data.sysexBuffer[i]->lpData = new char[RT_SYSEX_BUFFER_SIZE];
      data.sysexBuffer[i]->dwBufferLength = RT_SYSEX_BUFFER_SIZE;
      data.sysexBuffer[i]->dwUser = i; // We use the dwUser parameter as buffer indicator
      data.sysexBuffer[i]->dwFlags = 0;

      result = midiInPrepareHeader(data.inHandle, data.sysexBuffer[i], sizeof(MIDIHDR));
      if (result != MMSYSERR_NOERROR)
      {
        midiInClose(data.inHandle);
        data.inHandle = nullptr;
        error<driver_error>(
            "midi_in_winmm::open_port: error starting Windows MM MIDI input port "
            "(PrepareHeader).");
        return;
      }

      // Register the buffer.
      result = midiInAddBuffer(data.inHandle, data.sysexBuffer[i], sizeof(MIDIHDR));
      if (result != MMSYSERR_NOERROR)
      {
        midiInClose(data.inHandle);
        data.inHandle = nullptr;
        error<driver_error>(
            "midi_in_winmm::open_port: error starting Windows MM MIDI input port "
            "(AddBuffer).");
        return;
      }
    }

    result = midiInStart(data.inHandle);
    if (result != MMSYSERR_NOERROR)
    {
      midiInClose(data.inHandle);
      data.inHandle = nullptr;
      error<driver_error>("midi_in_winmm::open_port: error starting Windows MM MIDI input port.");
      return;
    }

    connected_ = true;
  }

  void close_port() override
  {
    if (connected_)
    {
      EnterCriticalSection(&(data._mutex));
      midiInReset(data.inHandle);
      midiInStop(data.inHandle);

      for (int i = 0; i < RT_SYSEX_BUFFER_COUNT; ++i)
      {
        int result = midiInUnprepareHeader(data.inHandle, data.sysexBuffer[i], sizeof(MIDIHDR));
        delete[] data.sysexBuffer[i]->lpData;
        delete[] data.sysexBuffer[i];
        if (result != MMSYSERR_NOERROR)
        {
          midiInClose(data.inHandle);
          data.inHandle = nullptr;
          error<driver_error>(
              "midi_in_winmm::open_port: error closing Windows MM MIDI input "
              "port (midiInUnprepareHeader).");
          return;
        }
      }

      midiInClose(data.inHandle);
      data.inHandle = 0;
      connected_ = false;
      LeaveCriticalSection(&(data._mutex));
    }
  }

  unsigned int get_port_count() override { return midiInGetNumDevs(); }

  std::string get_port_name(unsigned int portNumber) override
  {
    std::string stringName;
    unsigned int nDevices = midiInGetNumDevs();
    if (portNumber >= nDevices)
    {
      std::ostringstream ost;
      ost << "midi_in_winmm::get_port_name: the 'portNumber' argument (" << portNumber
          << ") is invalid.";
      warning(ost.str());
      return stringName;
    }

    MIDIINCAPS deviceCaps;
    midiInGetDevCaps(portNumber, &deviceCaps, sizeof(MIDIINCAPS));
    stringName = ConvertToUTF8(deviceCaps.szPname);

#ifndef LIBREMIDI_DO_NOT_ENSURE_UNIQUE_PORTNAMES
    MakeUniqueInPortName(stringName, portNumber);
#endif

    return stringName;
  }

private:
  static void CALLBACK midiInputCallback(
      HMIDIIN /*hmin*/, UINT inputStatus, DWORD_PTR instancePtr, DWORD_PTR midiMessage,
      DWORD timestamp)
  {
    if (inputStatus != MIM_DATA && inputStatus != MIM_LONGDATA && inputStatus != MIM_LONGERROR)
      return;

    midi_in_api::in_data& data = *(midi_in_api::in_data*)instancePtr;
    WinMidiData& apiData = *static_cast<WinMidiData*>(data.apiData);

    // Calculate time stamp.
    if (data.firstMessage == true)
    {
      apiData.message.timestamp = 0.0;
      data.firstMessage = false;
    }
    else
      apiData.message.timestamp = (double)(timestamp - apiData.lastTime) * 0.001;

    if (inputStatus == MIM_DATA)
    { // Channel or system message

      // Make sure the first byte is a status byte.
      unsigned char status = (unsigned char)(midiMessage & 0x000000FF);
      if (!(status & 0x80))
        return;

      // Determine the number of bytes in the MIDI message.
      unsigned short nBytes = 1;
      if (status < 0xC0)
        nBytes = 3;
      else if (status < 0xE0)
        nBytes = 2;
      else if (status < 0xF0)
        nBytes = 3;
      else if (status == 0xF1)
      {
        if (data.ignoreFlags & 0x02)
          return;
        else
          nBytes = 2;
      }
      else if (status == 0xF2)
        nBytes = 3;
      else if (status == 0xF3)
        nBytes = 2;
      else if (status == 0xF8 && (data.ignoreFlags & 0x02))
      {
        // A MIDI timing tick message and we're ignoring it.
        return;
      }
      else if (status == 0xFE && (data.ignoreFlags & 0x04))
      {
        // A MIDI active sensing message and we're ignoring it.
        return;
      }

      // Copy bytes to our MIDI message.
      unsigned char* ptr = (unsigned char*)&midiMessage;
      apiData.message.bytes.resize(nBytes);
      for (int i = 0; i < nBytes; ++i)
        apiData.message.bytes[i] = ptr[i];
    }
    else
    { // Sysex message ( MIM_LONGDATA or MIM_LONGERROR )
      MIDIHDR* sysex = (MIDIHDR*)midiMessage;
      if (!(data.ignoreFlags & 0x01) && inputStatus != MIM_LONGERROR)
      {
        // Sysex message and we're not ignoring it
        for (int i = 0; i < (int)sysex->dwBytesRecorded; ++i)
          apiData.message.bytes.push_back(sysex->lpData[i]);
      }

      // The WinMM API requires that the sysex buffer be requeued after
      // input of each sysex message.  Even if we are ignoring sysex
      // messages, we still need to requeue the buffer in case the user
      // decides to not ignore sysex messages in the future.  However,
      // it seems that WinMM calls this function with an empty sysex
      // buffer when an application closes and in this case, we should
      // avoid requeueing it, else the computer suddenly reboots after
      // one or two minutes.
      if (apiData.sysexBuffer[sysex->dwUser]->dwBytesRecorded > 0)
      {
        // if ( sysex->dwBytesRecorded > 0 ) {
        EnterCriticalSection(&(apiData._mutex));
        MMRESULT result = midiInAddBuffer(
            apiData.inHandle, apiData.sysexBuffer[sysex->dwUser], sizeof(MIDIHDR));
        LeaveCriticalSection(&(apiData._mutex));
        if (result != MMSYSERR_NOERROR)
          std::cerr << "\nmidi_in::midiInputCallback: error sending sysex to "
                       "Midi device!!\n\n";

        if (data.ignoreFlags & 0x01)
          return;
      }
      else
        return;
    }

    // Save the time of the last non-filtered message
    apiData.lastTime = timestamp;

    data.on_message_received(std::move(apiData.message));
  }

  WinMidiData data;
};

}
