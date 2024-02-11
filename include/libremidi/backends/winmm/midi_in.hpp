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
        &this->inHandle, portNumber, std::bit_cast<DWORD_PTR>(&midiInputCallback),
        std::bit_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);
    if (result != MMSYSERR_NOERROR)
    {
      error<driver_error>(
          configuration, "midi_in_winmm::open_port: error creating Windows MM MIDI input port.");
      return false;
    }

    // Allocate and init the sysex buffers.
    const auto bufferCount = static_cast<std::size_t>(configuration.sysex_buffer_count);
    this->sysexBuffer.resize(bufferCount);
    for (std::size_t i = 0; i < bufferCount; ++i)
    {
      this->sysexBuffer[i] = new MIDIHDR;
      this->sysexBuffer[i]->lpData = new char[configuration.sysex_buffer_size];
      this->sysexBuffer[i]->dwBufferLength = static_cast<DWORD>(configuration.sysex_buffer_size);
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
    midi_start_timestamp = std::chrono::steady_clock::now();
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

      for (std::size_t i = 0; i < static_cast<std::size_t>(configuration.sysex_buffer_count); ++i)
      {
        MMRESULT res{};

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
      this->inHandle = nullptr;
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
          msg.timestamp = 0;
        }
        else
        {
          msg.timestamp = static_cast<int64_t>(time);
        }
        return;
      }
      case timestamp_mode::Absolute: {
        msg.timestamp = static_cast<int64_t>(ts * 1'000'000);
        break;
      }
      case timestamp_mode::SystemMonotonic: {
        namespace clk = std::chrono;
        msg.timestamp
            = clk::duration_cast<clk::nanoseconds>(clk::steady_clock::now().time_since_epoch())
                  .count();
        break;
      }
      case timestamp_mode::Custom:
        msg.timestamp = configuration.get_timestamp(static_cast<int64_t>(ts * 1'000'000));
        break;
    }
  }

  int64_t absolute_timestamp() const noexcept override
  {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now() - midi_start_timestamp)
        .count();
  }

  static void CALLBACK midiInputCallback(
      HMIDIIN /*hmin*/, UINT inputStatus, DWORD_PTR instancePtr, DWORD_PTR midiMessage,
      DWORD_PTR timestamp)
  {
    if (inputStatus != MIM_DATA && inputStatus != MIM_LONGDATA && inputStatus != MIM_LONGERROR)
      return;

    auto& self = *reinterpret_cast<midi_in_winmm*>(instancePtr);

    if (inputStatus == MIM_DATA)
    { // Channel or system message

      // Make sure the first byte is a status byte.
      const auto status = static_cast<unsigned char>(midiMessage & 0x000000FF);
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
      const auto* ptr = reinterpret_cast<unsigned char*>(&midiMessage);

      self.set_timestamp(timestamp, self.basic_message);
      self.basic_message.bytes.assign(ptr, ptr + nBytes);
      self.configuration.on_message(std::move(self.basic_message));
      // Save the time of the last non-filtered message
      self.last_time = timestamp;

      return;
    }
    else
    {
      // Sysex message ( MIM_LONGDATA or MIM_LONGERROR )
      const auto* sysex = reinterpret_cast<MIDIHDR*>(midiMessage);
      bool can_send_message = false;
      if(inputStatus == MIM_LONGERROR)
      {
        self.sysex_message.bytes.clear();
      }
      else if (!self.configuration.ignore_sysex)
      {
        if(sysex->dwBytesRecorded > 0)
        {
          const unsigned char first_byte = sysex->lpData[0];
          const unsigned char last_byte = sysex->lpData[sysex->dwBytesRecorded - 1];
          if(first_byte == 0xF0) {
            // Starting a new sysex
            self.sysex_message.bytes.clear();
          }
          if(last_byte == 0xF7) {
            // The sysex is finished
            can_send_message = true;
          }

          self.sysex_message.bytes.insert(
              self.sysex_message.bytes.end(), sysex->lpData, sysex->lpData + sysex->dwBytesRecorded);
        }
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
        {
#if defined(__LIBREMIDI_DEBUG__)
          std::cerr << "\nmidi_in::midiInputCallback: error sending sysex to "
                       "Midi device!!\n\n";
#endif
          return;
        }
        if (can_send_message)
        {
          self.set_timestamp(timestamp, self.sysex_message);
          self.configuration.on_message(std::move(self.sysex_message));
          self.sysex_message.clear();
          // Save the time of the last non-filtered message
          self.last_time = timestamp;
        }
      }
      else
      {
        return;
      }
    }
  }

  HMIDIIN inHandle; // Handle to Midi Input Device

  DWORD last_time;
  std::vector<LPMIDIHDR> sysexBuffer;
  // [Patrice] see
  // https://groups.google.com/forum/#!topic/mididev/6OUjHutMpEo
  CRITICAL_SECTION _mutex;
  std::chrono::steady_clock::time_point midi_start_timestamp;

  libremidi::message basic_message;
  libremidi::message sysex_message;
};

}
