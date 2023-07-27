#pragma once
/* This software is based on the RtMidi and ModernMidi libraries.

  RtMidi WWW site: http://music.mcgill.ca/~gary/libremidi/

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

#include <libremidi/api.hpp>
#include <libremidi/input_configuration.hpp>
#include <libremidi/message.hpp>
#include <libremidi/observer_configuration.hpp>
#include <libremidi/output_configuration.hpp>

#include <any>
#include <optional>

namespace libremidi
{
class midi_in;
class midi_out;
class observer;
LIBREMIDI_EXPORT
std::any midi_in_configuration_for(libremidi::API);
LIBREMIDI_EXPORT
std::any midi_out_configuration_for(libremidi::API);
LIBREMIDI_EXPORT
std::any observer_configuration_for(libremidi::API);

LIBREMIDI_EXPORT
std::any midi_in_configuration_for(const libremidi::observer&);
LIBREMIDI_EXPORT
std::any midi_out_configuration_for(const libremidi::observer&);

LIBREMIDI_EXPORT
std::any midi_in_default_configuration();
LIBREMIDI_EXPORT
std::any midi_out_default_configuration();
LIBREMIDI_EXPORT
std::any observer_default_configuration();

LIBREMIDI_EXPORT
std::optional<port_information> midi_in_default_port(libremidi::API api = libremidi::default_platform_api()) noexcept;
LIBREMIDI_EXPORT
std::optional<port_information> midi_out_default_port(libremidi::API api = libremidi::default_platform_api()) noexcept;

//! The callbacks will be called whenever a device is added or removed
//! for a given API.
class LIBREMIDI_EXPORT observer
{
public:
  //! Open an observer instance with the given configuration.
  //!
  //! api_conf can be either
  //! - an instance of observer_configuration,
  //!   such as jack_observer_configuration, winmm_observer_configuration, etc...
  //! - a libremidi::API enum to simply request a specific api
  explicit observer(observer_configuration conf = {}) noexcept;
  explicit observer(observer_configuration conf, std::any api_conf);
  observer(const observer&) = delete;
  observer(observer&& other) noexcept;
  observer& operator=(const observer&) = delete;
  observer& operator=(observer&& other) noexcept;
  ~observer();

  [[nodiscard]] libremidi::API get_current_api() const noexcept;

  //! Return identifiers for the available MIDI ports
  std::vector<libremidi::port_information> get_input_ports() const noexcept;
  std::vector<libremidi::port_information> get_output_ports() const noexcept;

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

class LIBREMIDI_EXPORT midi_in
{
public:
  //! Construct a midi_in object with the default back-end for the platform
  explicit midi_in(input_configuration conf) noexcept;

  //! Construct a midi_in object with a configuration object for a specific back-end
  //! see configuration.hpp for the available configuration types.
  //! An exception will be thrown if the requested back-end cannot be opened.
  explicit midi_in(input_configuration conf, std::any api_conf);

  midi_in(const midi_in&) = delete;
  midi_in(midi_in&& other) noexcept;
  midi_in& operator=(const midi_in&) = delete;
  midi_in& operator=(midi_in&& other) noexcept;
  ~midi_in();

  //! Returns the MIDI API specifier for the current instance of midi_in.
  [[nodiscard]] libremidi::API get_current_api() const noexcept;

  //! Open a MIDI input connection
  void open_port(const port_information& pt, std::string_view local_port_name = "libremidi input");

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
  void open_virtual_port(std::string_view portName);
  void open_virtual_port() { open_virtual_port("libremidi virtual port"); }

  void set_port_name(std::string_view portName);

  //! Close an open MIDI connection (if one exists).
  void close_port();

  //! Returns true if a port has been opened successfully with open_port or open_virtual_port
  [[nodiscard]] bool is_port_open() const noexcept;

  //! Returns true if a port is connected to another port.
  //! Never true for virtual ports.
  [[nodiscard]] bool is_port_connected() const noexcept;

private:
  std::unique_ptr<class midi_in_api> impl_;
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

class LIBREMIDI_EXPORT midi_out
{
public:
  //! Construct a midi_out object with the default back-end for the platform
  explicit midi_out(output_configuration conf = {}) noexcept;

  //! Construct a midi_out object with a configuration object for a specific back-end
  //! see configuration.hpp for the available configuration types.
  //! An exception will be thrown if the requested back-end cannot be opened.
  explicit midi_out(output_configuration conf, std::any api_conf);

  midi_out(const midi_out&) = delete;
  midi_out(midi_out&& other) noexcept;
  midi_out& operator=(const midi_out&) = delete;
  midi_out& operator=(midi_out&& other) noexcept;
  ~midi_out();

  //! Returns the MIDI API specifier for the current instance of midi_out.
  [[nodiscard]] libremidi::API get_current_api() noexcept;

  //! Open a MIDI output connection.
  void open_port(const port_information& pt, std::string_view local_port_name = "libremidi input");

  //! Close an open MIDI connection (if one exists).
  void close_port();

  //! Returns true if a port has been opened successfully with open_port or open_virtual_port
  [[nodiscard]] bool is_port_open() const noexcept;

  //! Returns true if a port is connected to another port.
  //! Never true for virtual ports.
  [[nodiscard]] bool is_port_connected() const noexcept;

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
  void open_virtual_port(std::string_view portName);
  void open_virtual_port() { open_virtual_port("libremidi virtual port"); }

  void set_port_name(std::string_view portName);

  //! Immediately send a single message out an open MIDI output port.
  /*!
      An exception is thrown if an error occurs during output or an
      output connection was not previously established.
  */
  void send_message(const libremidi::message& message);

  //! Immediately send a single message out an open MIDI output port.
  /*!
      An exception is thrown if an error occurs during output or an
      output connection was not previously established.

      \param message A pointer to the MIDI message as raw bytes
      \param size    Length of the MIDI message in bytes
  */
  void send_message(const unsigned char* message, size_t size);
  void send_message(std::span<const unsigned char>);
  void send_message(unsigned char b0);
  void send_message(unsigned char b0, unsigned char b1);
  void send_message(unsigned char b0, unsigned char b1, unsigned char b2);

private:
  std::unique_ptr<class midi_out_api> impl_;
};
}

#if defined(LIBREMIDI_HEADER_ONLY)
  #include <libremidi/libremidi.cpp>
  #include <libremidi/midi_in.cpp>
  #include <libremidi/midi_out.cpp>
  #include <libremidi/observer.cpp>

  #if defined(__EMSCRIPTEN__)
    #include <libremidi/backends/emscripten/midi_access.cpp>
    #include <libremidi/backends/emscripten/midi_in.cpp>
    #include <libremidi/backends/emscripten/midi_out.cpp>
    #include <libremidi/backends/emscripten/observer.cpp>
  #endif
#endif
