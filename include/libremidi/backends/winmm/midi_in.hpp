#pragma once
#include <libremidi/backends/winmm/config.hpp>
#include <libremidi/backends/winmm/helpers.hpp>
#include <libremidi/backends/winmm/observer.hpp>
#include <libremidi/detail/midi_in.hpp>

namespace libremidi
{

class midi_in_winmm final
    : public midi1::in_api
    , public error_handler
{
public:
  struct
      : input_configuration
      , winmm_input_configuration
  {
  } configuration;

  explicit midi_in_winmm(input_configuration&& conf, winmm_input_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    // We'll issue a warning here if no devices are available but not
    // throw an error since the user can plugin something later.
    if (midiInGetNumDevs() == 0)
    {
      warning(
          configuration, "midi_in_winmm::initialize: no MIDI input devices currently available.");
    }

    if (!InitializeCriticalSectionAndSpinCount(&(this->_mutex), 0x00000400))
    {
      warning(
          configuration,
          "midi_in_winmm::initialize: InitializeCriticalSectionAndSpinCount failed.");
    }
  }

  ~midi_in_winmm() override
  {
    // Close a connection if it exists.
    midi_in_winmm::close_port();

    DeleteCriticalSection(&(this->_mutex));
  }

  bool open_virtual_port(std::string_view) override
  {
    warning(configuration, "midi_in_winmm: open_virtual_port unsupported");
    return false;
  }
  void set_client_name(std::string_view) override
  {
    warning(configuration, "midi_in_winmm: set_client_name unsupported");
  }
  void set_port_name(std::string_view) override
  {
    warning(configuration, "midi_in_winmm: set_port_name unsupported");
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::WINDOWS_MM; }

  bool do_open(unsigned int portNumber)
  {
    MMRESULT result = midiInOpen(
        &this->inHandle, portNumber, (DWORD_PTR)&midiInputCallback, (DWORD_PTR)this,
        CALLBACK_FUNCTION);
    if (result != MMSYSERR_NOERROR)
    {
      error<driver_error>(
          configuration, "midi_in_winmm::open_port: error creating Windows MM MIDI input port.");
      return false;
    }

    // Allocate and init the sysex buffers.
    this->sysexBuffer.resize(configuration.sysex_buffer_count);
    for (int i = 0; i < configuration.sysex_buffer_count; ++i)
    {
      this->sysexBuffer[i] = (MIDIHDR*)new char[sizeof(MIDIHDR)];
      this->sysexBuffer[i]->lpData = new char[configuration.sysex_buffer_size];
      this->sysexBuffer[i]->dwBufferLength = configuration.sysex_buffer_size;
      this->sysexBuffer[i]->dwUser = i; // We use the dwUser parameter as buffer indicator
      this->sysexBuffer[i]->dwFlags = 0;

      result = midiInPrepareHeader(this->inHandle, this->sysexBuffer[i], sizeof(MIDIHDR));
      if (result != MMSYSERR_NOERROR)
      {
        midiInClose(this->inHandle);
        this->inHandle = nullptr;
        error<driver_error>(
            configuration,
            "midi_in_winmm::open_port: error starting Windows MM MIDI input port "
            "(PrepareHeader).");
        return false;
      }

      // Register the buffer.
      result = midiInAddBuffer(this->inHandle, this->sysexBuffer[i], sizeof(MIDIHDR));
      if (result != MMSYSERR_NOERROR)
      {
        midiInClose(this->inHandle);
        this->inHandle = nullptr;
        error<driver_error>(
            configuration,
            "midi_in_winmm::open_port: error starting Windows MM MIDI input port "
            "(AddBuffer).");
        return false;
      }
    }

    result = midiInStart(this->inHandle);
    if (result != MMSYSERR_NOERROR)
    {
      midiInClose(this->inHandle);
      this->inHandle = nullptr;
      error<driver_error>(
          configuration, "midi_in_winmm::open_port: error starting Windows MM MIDI input port.");
      return false;
    }

    return true;
  }

  bool open_port(const input_port& p, std::string_view) override
  {
    observer_winmm obs{{}, winmm_observer_configuration{}};
    auto ports = obs.get_input_ports();

    // First check with the display name, e.g. MIDI KEYBOARD 2 will match MIDI KEYBOARD 2
    for (auto& port : ports)
    {
      if (p.display_name == port.display_name)
        return do_open(port.port);
    }
    // If nothing is found, try to check with the raw name
    for (auto& port : ports)
    {
      if (p.port_name == port.port_name)
        return do_open(port.port);
    }
    error<invalid_parameter_error>(
        configuration, "midi_in_winmm::open_port: port not found: " + p.port_name);
    return false;
  }

  void close_port() override
  {
    if (connected_)
    {
      EnterCriticalSection(&(this->_mutex));
      midiInReset(this->inHandle);
      midiInStop(this->inHandle);

      for (int i = 0; i < configuration.sysex_buffer_count; ++i)
      {
        int res{};

        int wait_count = 5;
        while (
            ((res = midiInUnprepareHeader(this->inHandle, this->sysexBuffer[i], sizeof(MIDIHDR)))
             == MIDIERR_STILLPLAYING)
            && wait_count-- >= 0)
        {
          Sleep(1);
        }

        if (res != MMSYSERR_NOERROR)
        {
          warning(
              configuration,
              "midi_in_winmm::open_port: error closing Windows MM MIDI input "
              "port (midiInUnprepareHeader).");
          continue;
        }
        else
        {
          delete[] this->sysexBuffer[i]->lpData;
          delete[] this->sysexBuffer[i];
        }
      }

      midiInClose(this->inHandle);
      this->inHandle = 0;
      LeaveCriticalSection(&(this->_mutex));
    }
  }

private:
  void set_timestamp(DWORD_PTR ts, libremidi::message& msg) noexcept
  {
    switch (configuration.timestamps)
    {
      case timestamp_mode::NoTimestamp:
        msg.timestamp = 0;
        return;
      case timestamp_mode::Relative: {
        const auto time = ts * 1'000'000 - last_time;

        last_time = ts * 1'000'000;

        if (firstMessage == true)
        {
          firstMessage = false;
          message.timestamp = 0;
        }
        else
        {
          message.timestamp = time;
        }
        return;
      }
      case timestamp_mode::Absolute: {
        msg.timestamp = ts * 1'000'000;
        break;
      }
      case timestamp_mode::SystemMonotonic: {
        namespace clk = std::chrono;
        msg.timestamp
            = clk::duration_cast<clk::nanoseconds>(clk::steady_clock::now().time_since_epoch())
                  .count();
        break;
      }
    }
  }
  static void CALLBACK midiInputCallback(
      HMIDIIN /*hmin*/, UINT inputStatus, DWORD_PTR instancePtr, DWORD_PTR midiMessage,
      DWORD_PTR timestamp)
  {
    if (inputStatus != MIM_DATA && inputStatus != MIM_LONGDATA && inputStatus != MIM_LONGERROR)
      return;

    auto& self = *(midi_in_winmm*)instancePtr;

    auto& message = self.message;

    self.set_timestamp(timestamp, message);

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
        if (self.configuration.ignore_timing)
          return;
        else
          nBytes = 2;
      }
      else if (status == 0xF2)
        nBytes = 3;
      else if (status == 0xF3)
        nBytes = 2;
      else if (status == 0xF8 && (self.configuration.ignore_timing))
      {
        // A MIDI timing tick message and we're ignoring it.
        return;
      }
      else if (status == 0xFE && (self.configuration.ignore_sensing))
      {
        // A MIDI active sensing message and we're ignoring it.
        return;
      }

      // Copy bytes to our MIDI message.
      unsigned char* ptr = (unsigned char*)&midiMessage;
      message.bytes.assign(ptr, ptr + nBytes);
    }
    else
    { // Sysex message ( MIM_LONGDATA or MIM_LONGERROR )
      MIDIHDR* sysex = (MIDIHDR*)midiMessage;
      if (!self.configuration.ignore_sysex && inputStatus != MIM_LONGERROR)
      {
        // Sysex message and we're not ignoring it
        message.bytes.insert(
            message.bytes.end(), sysex->lpData, sysex->lpData + sysex->dwBytesRecorded);
      }

      // The WinMM API requires that the sysex buffer be requeued after
      // input of each sysex message.  Even if we are ignoring sysex
      // messages, we still need to requeue the buffer in case the user
      // decides to not ignore sysex messages in the future.  However,
      // it seems that WinMM calls this function with an empty sysex
      // buffer when an application closes and in this case, we should
      // avoid requeueing it, else the computer suddenly reboots after
      // one or two minutes.
      if (self.sysexBuffer[sysex->dwUser]->dwBytesRecorded > 0)
      {
        // if ( sysex->dwBytesRecorded > 0 ) {
        EnterCriticalSection(&(self._mutex));
        MMRESULT result
            = midiInAddBuffer(self.inHandle, self.sysexBuffer[sysex->dwUser], sizeof(MIDIHDR));
        LeaveCriticalSection(&(self._mutex));
        if (result != MMSYSERR_NOERROR)

#if defined(__LIBREMIDI_DEBUG__)
          std::cerr << "\nmidi_in::midiInputCallback: error sending sysex to "
                       "Midi device!!\n\n";
#endif
        if (self.configuration.ignore_sysex)
          return;
      }
      else
        return;
    }

    // Save the time of the last non-filtered message
    self.last_time = timestamp;

    self.configuration.on_message(std::move(message));
  }

  HMIDIIN inHandle; // Handle to Midi Input Device

  DWORD last_time;
  std::vector<LPMIDIHDR> sysexBuffer;
  // [Patrice] see
  // https://groups.google.com/forum/#!topic/mididev/6OUjHutMpEo
  CRITICAL_SECTION _mutex;
};

}
