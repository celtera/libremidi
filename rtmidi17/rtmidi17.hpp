#pragma once
/* This software is based on the RtMidi and ModernMidi libraries.

  RtMidi WWW site: http://music.mcgill.ca/~gary/rtmidi/

  RtMidi: realtime MIDI i/o C++ classes
  Copyright (c) 2003-2017 Gary P. Scavone

  Permission is hereby granted, free of charge, to any person
  obtaining a copy of this software and associated documentation files
  (the "Software"), to deal in the Software without restriction,
  including without limitation the rights to use, copy, modify, merge,
  publish, distribute, sublicense, and/or sell copies of the Software,
  and to permit persons to whom the Software is furnished to do so,
  subject to the following conditions:

  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.

  Any person wishing to distribute modifications to the Software is
  asked to send the modifications to the original developer so that
  they can be incorporated into the canonical version.  This is,
  however, not a binding provision of this license.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
  ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
  CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 ------------

  ModernMidi Copyright (c) 2015, Dimitri Diakopoulos All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice, this
    list of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.
*/

#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#  define RTMIDI17_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#  define RTMIDI17_EXPORT __attribute__((visibility("default")))
#endif

#define RTMIDI17_VERSION "1.0.0"

#if defined(RTMIDI17_HEADER_ONLY)
#  define RTMIDI17_INLINE inline
#else
#  define RTMIDI17_INLINE
#endif

#if __has_include(<boost/container/small_vector.hpp>) && !defined(RTMIDI17_NO_BOOST)
#  include <boost/container/small_vector.hpp>
namespace rtmidi
{
using midi_bytes = boost::container::small_vector<unsigned char, 4>;
}
#else
namespace rtmidi
{
using midi_bytes = std::vector<unsigned char>;
}
#endif

namespace rtmidi
{
enum class message_type : uint8_t
{
  INVALID = 0x0,
  // Standard Message
  NOTE_OFF = 0x80,
  NOTE_ON = 0x90,
  POLY_PRESSURE = 0xA0,
  CONTROL_CHANGE = 0xB0,
  PROGRAM_CHANGE = 0xC0,
  AFTERTOUCH = 0xD0,
  PITCH_BEND = 0xE0,

  // System Common Messages
  SYSTEM_EXCLUSIVE = 0xF0,
  TIME_CODE = 0xF1,
  SONG_POS_POINTER = 0xF2,
  SONG_SELECT = 0xF3,
  RESERVED1 = 0xF4,
  RESERVED2 = 0xF5,
  TUNE_REQUEST = 0xF6,
  EOX = 0xF7,

  // System Realtime Messages
  TIME_CLOCK = 0xF8,
  RESERVED3 = 0xF9,
  START = 0xFA,
  CONTINUE = 0xFB,
  STOP = 0xFC,
  RESERVED4 = 0xFD,
  ACTIVE_SENSING = 0xFE,
  SYSTEM_RESET = 0xFF
};

enum class meta_event_type : uint8_t
{
  SEQUENCE_NUMBER = 0x00,
  TEXT = 0x01,
  COPYRIGHT = 0x02,
  TRACK_NAME = 0x03,
  INSTRUMENT = 0x04,
  LYRIC = 0x05,
  MARKER = 0x06,
  CUE = 0x07,
  PATCH_NAME = 0x08,
  DEVICE_NAME = 0x09,
  END_OF_TRACK = 0x2F,
  TEMPO_CHANGE = 0x51,
  SMPTE_OFFSET = 0x54,
  TIME_SIGNATURE = 0x58,
  KEY_SIGNATURE = 0x59,
  PROPRIETARY = 0x7F,
  UNKNOWN = 0xFF
};

constexpr inline uint8_t clamp(uint8_t val, uint8_t min, uint8_t max)
{
  return std::max(std::min(val, max), min);
}

struct message
{
  midi_bytes bytes;
  double timeStamp{};

  static uint8_t make_command(const message_type type, const int channel)
  {
    return (uint8_t)((uint8_t)type | clamp(channel, 0, channel - 1));
  }

  static message note_on(uint8_t channel, uint8_t note, uint8_t velocity)
  {
    return {midi_bytes{make_command(message_type::NOTE_ON, channel), note, velocity}};
  }

  static message note_off(uint8_t channel, uint8_t note, uint8_t velocity)
  {
    return {midi_bytes{make_command(message_type::NOTE_OFF, channel), note, velocity}};
  }

  static message control_change(uint8_t channel, uint8_t control, uint8_t value)
  {
    return {midi_bytes{make_command(message_type::CONTROL_CHANGE, channel), control, value}};
  }

  static message program_change(uint8_t channel, uint8_t value)
  {
    return {midi_bytes{make_command(message_type::PROGRAM_CHANGE, channel), value}};
  }

  static message pitch_bend(uint8_t channel, int value)
  {
    return {midi_bytes{make_command(message_type::PITCH_BEND, channel),
                       (unsigned char)(value & 0x7F), (uint8_t)((value >> 7) & 0x7F)}};
  }

  static message pitch_bend(uint8_t channel, uint8_t lsb, uint8_t msb)
  {
    return {midi_bytes{make_command(message_type::PITCH_BEND, channel), lsb, msb}};
  }

  static message poly_pressure(uint8_t channel, uint8_t note, uint8_t value)
  {
    return {midi_bytes{make_command(message_type::POLY_PRESSURE, channel), note, value}};
  }

  static message aftertouch(uint8_t channel, uint8_t value)
  {
    return {midi_bytes{make_command(message_type::AFTERTOUCH, channel), value}};
  }

  bool uses_channel(int channel) const
  {
    if (channel <= 0 || channel > 16)
      throw std::range_error("midi_message::uses_channel: out of range");
    return ((bytes[0] & 0xF) == channel - 1) && ((bytes[0] & 0xF0) != 0xF0);
  }

  int get_channel() const
  {
    if ((bytes[0] & 0xF0) != 0xF0)
      return (bytes[0] & 0xF) + 1;
    return 0;
  }

  bool is_meta_event() const
  {
    return bytes[0] == 0xFF;
  }

  meta_event_type get_meta_event_type() const
  {
    if (!is_meta_event())
      return meta_event_type::UNKNOWN;
    return (meta_event_type)bytes[1];
  }

  message_type get_message_type() const
  {
    if (bytes[0] >= uint8_t(message_type::SYSTEM_EXCLUSIVE))
    {
      return (message_type)(bytes[0] & 0xFF);
    }
    else
    {
      return (message_type)(bytes[0] & 0xF0);
    }
  }

  bool is_note_on_or_off() const
  {
    const auto status = get_message_type();
    return (status == message_type::NOTE_ON) || (status == message_type::NOTE_OFF);
  }

  auto size() const
  {
    return bytes.size();
  }

  auto& front() const
  {
    return bytes.front();
  }
  auto& back() const
  {
    return bytes.back();
  }
  auto& operator[](int i) const
  {
    return bytes[i];
  }
  auto& front()
  {
    return bytes.front();
  }
  auto& back()
  {
    return bytes.back();
  }
  auto& operator[](int i)
  {
    return bytes[i];
  }

  template <typename... Args>
  auto assign(Args&&... args)
  {
    return bytes.assign(std::forward<Args>(args)...);
  }
  template <typename... Args>
  auto insert(Args&&... args)
  {
    return bytes.insert(std::forward<Args>(args)...);
  }
  auto clear()
  {
    bytes.clear();
  }

  auto begin() const
  {
    return bytes.begin();
  }
  auto end() const
  {
    return bytes.end();
  }
  auto begin()
  {
    return bytes.begin();
  }
  auto end()
  {
    return bytes.end();
  }
  auto cbegin() const
  {
    return bytes.cbegin();
  }
  auto cend() const
  {
    return bytes.cend();
  }
  auto cbegin()
  {
    return bytes.cbegin();
  }
  auto cend()
  {
    return bytes.cend();
  }
  auto rbegin() const
  {
    return bytes.rbegin();
  }
  auto rend() const
  {
    return bytes.rend();
  }
  auto rbegin()
  {
    return bytes.rbegin();
  }
  auto rend()
  {
    return bytes.rend();
  }
};

struct meta_events
{
  static message end_of_track()
  {
    return {midi_bytes{0xFF, 0x2F, 0}};
  }

  static message channel(int channel)
  {
    return {midi_bytes{0xff, 0x20, 0x01, clamp(0, 0xff, channel - 1)}};
  }

  static message tempo(int mpqn)
  {
    return {midi_bytes{0xff, 81, 3, (uint8_t)(mpqn >> 16), (uint8_t)(mpqn >> 8), (uint8_t)mpqn}};
  }

  static message time_signature(int numerator, int denominator)
  {
    int n = 1;
    int powTwo = 0;

    while (n < denominator)
    {
      n <<= 1;
      ++powTwo;
    }

    return {midi_bytes{0xff, 0x58, 0x04, (uint8_t)numerator, (uint8_t)powTwo, 1, 96}};
  }

  // Where key index goes from -7 (7 flats, C♭ Major) to +7 (7 sharps, C♯
  // Major)
  static message key_signature(int keyIndex, bool isMinor)
  {
    if (keyIndex < -7 || keyIndex > 7)
      throw std::range_error("meta_events::key_signature: out of range");
    return {midi_bytes{0xff, 0x59, 0x02, (uint8_t)keyIndex, isMinor ? (uint8_t)1 : (uint8_t)0}};
  }

  static message song_position(int positionInBeats) noexcept
  {
    return {midi_bytes{0xf2, (uint8_t)(positionInBeats & 127),
                       (uint8_t)((positionInBeats >> 7) & 127)}};
  }
};

//! Defines various error types.
enum midi_error
{
  WARNING,           /*!< A non-critical error. */
  UNSPECIFIED,       /*!< The default, unspecified error type. */
  NO_DEVICES_FOUND,  /*!< No devices found on system. */
  INVALID_DEVICE,    /*!< An invalid device ID was specified. */
  MEMORY_ERROR,      /*!< An error occured during memory allocation. */
  INVALID_PARAMETER, /*!< An invalid parameter was specified to a function. */
  INVALID_USE,       /*!< The function was called incorrectly. */
  DRIVER_ERROR,      /*!< A system driver error occured. */
  SYSTEM_ERROR,      /*!< A system error occured. */
  THREAD_ERROR       /*!< A thread error occured. */
};

//! Base exception class for MIDI problems
struct RTMIDI17_EXPORT midi_exception : public std::runtime_error
{
  using std::runtime_error::runtime_error;
  ~midi_exception() override;
};

struct RTMIDI17_EXPORT no_devices_found_error final : public midi_exception
{
  static constexpr auto code = midi_error::NO_DEVICES_FOUND;
  using midi_exception::midi_exception;
  ~no_devices_found_error() override;
};
struct RTMIDI17_EXPORT invalid_device_error final : public midi_exception
{
  static constexpr auto code = midi_error::INVALID_DEVICE;
  using midi_exception::midi_exception;
  ~invalid_device_error() override;
};
struct RTMIDI17_EXPORT memory_error final : public midi_exception
{
  static constexpr auto code = midi_error::MEMORY_ERROR;
  using midi_exception::midi_exception;
  ~memory_error() override;
};
struct RTMIDI17_EXPORT invalid_parameter_error final : public midi_exception
{
  static constexpr auto code = midi_error::INVALID_PARAMETER;
  using midi_exception::midi_exception;
  ~invalid_parameter_error() override;
};
struct RTMIDI17_EXPORT invalid_use_error final : public midi_exception
{
  static constexpr auto code = midi_error::INVALID_USE;
  using midi_exception::midi_exception;
  ~invalid_use_error() override;
};
struct RTMIDI17_EXPORT driver_error final : public midi_exception
{
  static constexpr auto code = midi_error::DRIVER_ERROR;
  using midi_exception::midi_exception;
  ~driver_error() override;
};
struct RTMIDI17_EXPORT system_error final : public midi_exception
{
  static constexpr auto code = midi_error::SYSTEM_ERROR;
  using midi_exception::midi_exception;
  ~system_error() override;
};
struct RTMIDI17_EXPORT thread_error final : public midi_exception
{
  static constexpr auto code = midi_error::THREAD_ERROR;
  using midi_exception::midi_exception;
  ~thread_error() override;
};

/*! \brief Error callback function
    \param type Type of error.
    \param errorText Error description.

    Note that class behaviour is undefined after a critical error (not
    a warning) is reported.
 */
using midi_error_callback = std::function<void(midi_error type, std::string_view errorText)>;

//! MIDI API specifier arguments.
enum class API
{
  UNSPECIFIED, /*!< Search for a working compiled API. */
  MACOSX_CORE, /*!< Macintosh OS-X Core Midi API. */
  LINUX_ALSA,  /*!< The Advanced Linux Sound Architecture API. */
  UNIX_JACK,   /*!< The JACK Low-Latency MIDI Server API. */
  WINDOWS_MM,  /*!< The Microsoft Multimedia MIDI API. */
  WINDOWS_UWP, /*!< The Microsoft WinRT MIDI API. */
  DUMMY        /*!< A compilable but non-functional API. */
};

/**
 * \brief A static function to determine the available compiled MIDI APIs.

  The values returned in the std::vector can be compared against
  the enumerated list values.  Note that there can be more than one
  API compiled for certain operating systems.
*/
std::vector<rtmidi::API> available_apis() noexcept;

//! A static function to determine the current version.
std::string get_version() noexcept;

//! The callbacks will be called whenever a device is added or removed
//! for a given API.
class RTMIDI17_EXPORT observer
{
public:
  struct callbacks
  {
    std::function<void(int, std::string)> input_added;
    std::function<void(int, std::string)> input_removed;
    std::function<void(int, std::string)> output_added;
    std::function<void(int, std::string)> output_removed;
  };

  observer(rtmidi::API, callbacks);
  ~observer();

private:
  std::unique_ptr<class observer_api> impl_;
};

/**********************************************************************/
/*! \class midi_in
    \brief A realtime MIDI input class.

    This class provides a common, platform-independent API for
    realtime MIDI input.  It allows access to a single MIDI input
    port.  Incoming MIDI messages are either saved to a queue for
    retrieval using the getMessage() function or immediately passed to
    a user-specified callback function.  Create multiple instances of
    this class to connect to more than one MIDI device at the same
    time.  With the OS-X, Linux ALSA, and JACK MIDI APIs, it is also
    possible to open a virtual input port to which other MIDI software
    clients can connect.

    by Gary P. Scavone, 2003-2017.
*/
class RTMIDI17_EXPORT midi_in
{
public:
  //! User callback function type definition.
  using message_callback = std::function<void(const message& message)>;

  //! Default constructor that allows an optional api, client name and queue
  //! size.
  /*!
    An exception will be thrown if a MIDI system initialization
    error occurs.  The queue size defines the maximum number of
    messages that can be held in the MIDI queue (when not using a
    callback function).  If the queue size limit is reached,
    incoming messages will be ignored.

    If no API argument is specified and multiple API support has been
    compiled, the default order of use is ALSA, JACK (Linux) and CORE,
    JACK (OS-X).

    \param api        An optional API id can be specified.
    \param clientName An optional client name can be specified. This
                      will be used to group the ports that are created
                      by the application.
    \param queueSizeLimit An optional size of the MIDI input queue can be
    specified.
  */
  midi_in(
      rtmidi::API api = API::UNSPECIFIED,
      const std::string& clientName = "RtMidi Input Client",
      unsigned int queueSizeLimit = 100);

  //! If a MIDI connection is still open, it will be closed by the destructor.
  ~midi_in();

  //! Returns the MIDI API specifier for the current instance of RtMidiIn.
  rtmidi::API get_current_api() const noexcept;

  //! Open a MIDI input connection given by enumeration number.
  /*!
    \param portNumber An optional port number greater than 0 can be specified.
                      Otherwise, the default or first port found is opened.
    \param portName An optional name for the application port that is used to
    connect to portId can be specified.
  */
  void open_port(
      unsigned int portNumber = 0, const std::string& portName = std::string("RtMidi Input"));

  //! Create a virtual input port, with optional name, to allow software
  //! connections (OS X, JACK and ALSA only).
  /*!
    This function creates a virtual MIDI input port to which other
    software applications can connect.  This type of functionality
    is currently only supported by the Macintosh OS-X, any JACK,
    and Linux ALSA APIs (the function returns an error for the other APIs).

    \param portName An optional name for the application port that is
                    used to connect to portId can be specified.
  */
  void open_virtual_port(const std::string& portName = std::string("RtMidi Input"));

  //! Set a callback function to be invoked for incoming MIDI messages.
  /*!
    The callback function will be called whenever an incoming MIDI
    message is received.  While not absolutely necessary, it is best
    to set the callback function before opening a MIDI port to avoid
    leaving some messages in the queue.

    \param callback A callback function must be given.
    \param userData Optionally, a pointer to additional data can be
                    passed to the callback function whenever it is called.
  */
  void set_callback(message_callback callback);

  //! Cancel use of the current callback function (if one exists).
  /*!
    Subsequent incoming MIDI messages will be written to the queue
    and can be retrieved with the \e getMessage function.
  */
  void cancel_callback();

  //! Close an open MIDI connection (if one exists).
  void close_port();

  //! Returns true if a port is open and false if not.
  /*!
      Note that this only applies to connections made with the openPort()
      function, not to virtual ports.
  */
  bool is_port_open() const noexcept;

  //! Return the number of available MIDI input ports.
  /*!
    \return This function returns the number of MIDI ports of the selected API.
  */
  unsigned int get_port_count();

  //! Return a string identifier for the specified MIDI input port number.
  /*!
    \return The name of the port with the given Id is returned.
            An empty string is returned if an invalid port specifier
            is provided. User code should assume a UTF-8 encoding.
  */
  std::string get_port_name(unsigned int portNumber = 0);

  //! Specify whether certain MIDI message types should be queued or ignored
  //! during input.
  /*!
    By default, MIDI timing and active sensing messages are ignored
    during message input because of their relative high data rates.
    MIDI sysex messages are ignored by default as well.  Variable
    values of "true" imply that the respective message type will be
    ignored.
  */
  void ignore_types(bool midiSysex = true, bool midiTime = true, bool midiSense = true);

  //! Fill the user-provided vector with the data bytes for the next available
  //! MIDI message in the input queue and return the event delta-time in
  //! seconds.
  /*!
    This function returns immediately whether a new message is
    available or not.  A valid message is indicated by a non-zero
    vector size.  An exception is thrown if an error occurs during
    message retrieval or an input connection was not previously
    established.
  */
  message get_message();

  //! Set an error callback function to be invoked when an error has occured.
  /*!
    The callback function will be called whenever an error has occured. It is
    best to set the error callback function before opening a port.
  */
  void set_error_callback(midi_error_callback errorCallback);

  void set_client_name(const std::string& clientName);

  void set_port_name(const std::string& portName);

private:
  std::unique_ptr<class midi_in_api> rtapi_;
};

/**********************************************************************/
/*! \class midi_out
    \brief A realtime MIDI output class.

    This class provides a common, platform-independent API for MIDI
    output.  It allows one to probe available MIDI output ports, to
    connect to one such port, and to send MIDI bytes immediately over
    the connection.  Create multiple instances of this class to
    connect to more than one MIDI device at the same time.  With the
    OS-X, Linux ALSA and JACK MIDI APIs, it is also possible to open a
    virtual port to which other MIDI software clients can connect.

    by Gary P. Scavone, 2003-2017.
*/
/**********************************************************************/

class RTMIDI17_EXPORT midi_out
{
public:
  //! Default constructor that allows an optional client name.
  /*!
    An exception will be thrown if a MIDI system initialization error occurs.

    If no API argument is specified and multiple API support has been
    compiled, the default order of use is ALSA, JACK (Linux) and CORE,
    JACK (OS-X).
  */
  midi_out(
      rtmidi::API api = API::UNSPECIFIED, const std::string& clientName = "RtMidi Output Client");

  //! The destructor closes any open MIDI connections.
  ~midi_out();

  //! Returns the MIDI API specifier for the current instance of RtMidiOut.
  rtmidi::API get_current_api() noexcept;

  //! Open a MIDI output connection.
  /*!
      An optional port number greater than 0 can be specified.
      Otherwise, the default or first port found is opened.  An
      exception is thrown if an error occurs while attempting to make
      the port connection.
  */
  void open_port(
      unsigned int portNumber = 0, const std::string& portName = std::string("RtMidi Output"));

  //! Close an open MIDI connection (if one exists).
  void close_port();

  //! Returns true if a port is open and false if not.
  /*!
      Note that this only applies to connections made with the openPort()
      function, not to virtual ports.
  */
  bool is_port_open() const noexcept;

  //! Create a virtual output port, with optional name, to allow software
  //! connections (OS X, JACK and ALSA only).
  /*!
      This function creates a virtual MIDI output port to which other
      software applications can connect.  This type of functionality
      is currently only supported by the Macintosh OS-X, Linux ALSA
      and JACK APIs (the function does nothing with the other APIs).
      An exception is thrown if an error occurs while attempting to
      create the virtual port.
  */
  void open_virtual_port(const std::string& portName = std::string("RtMidi Output"));

  //! Return the number of available MIDI output ports.
  unsigned int get_port_count();

  //! Return a string identifier for the specified MIDI port type and number.
  /*!
    \return The name of the port with the given Id is returned.
            An empty string is returned if an invalid port specifier
            is provided. User code should assume a UTF-8 encoding.
  */
  std::string get_port_name(unsigned int portNumber = 0);

  //! Immediately send a single message out an open MIDI output port.
  /*!
      An exception is thrown if an error occurs during output or an
      output connection was not previously established.
  */
  void send_message(const std::vector<unsigned char>& message);

  //! Immediately send a single message out an open MIDI output port.
  /*!
      An exception is thrown if an error occurs during output or an
      output connection was not previously established.

      \param message A pointer to the MIDI message as raw bytes
      \param size    Length of the MIDI message in bytes
  */
  void send_message(const unsigned char* message, size_t size);

  //! Set an error callback function to be invoked when an error has occured.
  /*!
    The callback function will be called whenever an error has occured. It is
    best to set the error callback function before opening a port.
  */
  void set_error_callback(midi_error_callback errorCallback) noexcept;

  void set_client_name(const std::string& clientName);

  void set_port_name(const std::string& portName);

private:
  std::unique_ptr<class midi_out_api> rtapi_;
};
}

#if defined(RTMIDI17_HEADER_ONLY)
#  include <rtmidi17/rtmidi17.cpp>
#endif
