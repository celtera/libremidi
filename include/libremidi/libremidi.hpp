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
#include <libremidi/error.hpp>
#include <libremidi/input_configuration.hpp>
#include <libremidi/message.hpp>
#include <libremidi/output_configuration.hpp>

#include <algorithm>
#include <any>
#include <cassert>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace libremidi
{

//! The callbacks will be called whenever a device is added or removed
//! for a given API.
class LIBREMIDI_EXPORT observer
{
public:
  struct callbacks
  {
    std::function<void(int, std::string)> input_added;
    std::function<void(int, std::string)> input_removed;
    std::function<void(int, std::string)> output_added;
    std::function<void(int, std::string)> output_removed;
  };

  observer(libremidi::API, callbacks);
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

class LIBREMIDI_EXPORT midi_in
{
public:
  //! User callback function type definition.
  using message_callback = libremidi::message_callback;

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
    specified.
  */
  explicit midi_in(
      libremidi::API api = API::UNSPECIFIED,
      std::string_view clientName = "libremidi input client");

  explicit midi_in(input_configuration conf, std::any api_conf = {});

  midi_in(const midi_in&) = delete;
  midi_in(midi_in&& other) noexcept;
  midi_in& operator=(const midi_in&) = delete;
  midi_in& operator=(midi_in&& other) noexcept;
  //! If a MIDI connection is still open, it will be closed by the destructor.
  ~midi_in();

  //! Returns the MIDI API specifier for the current instance of midi_in.
  [[nodiscard]] libremidi::API get_current_api() const noexcept;

  //! Open a MIDI input connection given by enumeration number.
  /*!
    \param portNumber A port number greater than 0 can be specified.
                      Otherwise, the default or first port found is opened.
    \param portName A name for the application port that is used to
    connect to portId can be specified.
  */
  void open_port(unsigned int portNumber, std::string_view portName);
  void open_port() { open_port(0, "libremidi Input"); }
  void open_port(unsigned int port) { open_port(port, "libremidi Input"); }

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

  //! Close an open MIDI connection (if one exists).
  void close_port();

  //! Returns true if a port is open and false if not.
  /*!
      Note that this only applies to connections made with the openPort()
      function, not to virtual ports.
  */
  [[nodiscard]] bool is_port_open() const noexcept;

  //! Return the number of available MIDI input ports.
  /*!
    \return This function returns the number of MIDI ports of the selected API.
  */
  [[nodiscard]] unsigned int get_port_count();

  //! Return a string identifier for the specified MIDI input port number.
  /*!
    \return The name of the port with the given Id is returned.
            An empty string is returned if an invalid port specifier
            is provided. User code should assume a UTF-8 encoding.
  */
  [[nodiscard]] std::string get_port_name(unsigned int portNumber = 0);

  //! Set an error callback function to be invoked when an error has occured.
  /*!
    The callback function will be called whenever an error has occured. It is
    best to set the error callback function before opening a port.
  */
  void set_error_callback(midi_error_callback errorCallback);

  void set_port_name(std::string_view portName);

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
  //! Default constructor that allows an optional client name.
  /*!
    An exception will be thrown if a MIDI system initialization error occurs.

    If no API argument is specified and multiple API support has been
    compiled, the default order of use is ALSA, JACK (Linux) and CORE,
    JACK (OS-X).
  */
  explicit midi_out(
      libremidi::API api = libremidi::API::UNSPECIFIED,
      std::string_view clientName = "libremidi client");
  explicit midi_out(output_configuration conf, std::any api_conf = {});

  midi_out(const midi_out&) = delete;
  midi_out(midi_out&& other) noexcept;
  midi_out& operator=(const midi_out&) = delete;
  midi_out& operator=(midi_out&& other) noexcept;

  //! The destructor closes any open MIDI connections.
  ~midi_out();

  //! Returns the MIDI API specifier for the current instance of midi_out.
  [[nodiscard]] libremidi::API get_current_api() noexcept;

  //! Open a MIDI output connection.
  /*!
      An optional port number greater than 0 can be specified.
      Otherwise, the default or first port found is opened.  An
      exception is thrown if an error occurs while attempting to make
      the port connection.
  */
  void open_port(unsigned int portNumber, std::string_view portName);
  void open_port() { open_port(0, "libremidi Output"); }
  void open_port(unsigned int port) { open_port(port, "libremidi Output"); }

  //! Close an open MIDI connection (if one exists).
  void close_port();

  //! Returns true if a port is open and false if not.
  /*!
      Note that this only applies to connections made with the openPort()
      function, not to virtual ports.
  */
  [[nodiscard]] bool is_port_open() const noexcept;

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

  //! Return the number of available MIDI output ports.
  [[nodiscard]] unsigned int get_port_count();

  //! Return a string identifier for the specified MIDI port type and number.
  /*!
    \return The name of the port with the given Id is returned.
            An empty string is returned if an invalid port specifier
            is provided. User code should assume a UTF-8 encoding.
  */
  [[nodiscard]] std::string get_port_name(unsigned int portNumber = 0);

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

  //! Set an error callback function to be invoked when an error has occured.
  /*!
    The callback function will be called whenever an error has occured. It is
    best to set the error callback function before opening a port.
  */
  void set_error_callback(midi_error_callback errorCallback) noexcept;

  void set_port_name(std::string_view portName);

private:
  std::unique_ptr<class midi_out_api> impl_;
};
}

#if defined(LIBREMIDI_HEADER_ONLY)
  #include <libremidi/libremidi.cpp>
#endif
