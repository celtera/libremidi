#pragma once
#include <libremidi/config.hpp>
#include <libremidi/error.hpp>
#include <libremidi/message.hpp>

#include <functional>
#include <string>

namespace libremidi
{
//! Specify how timestamps are handled in the system
enum timestamp_mode
{
  //! No timestamping at all, all timestamps are zero
  NoTimestamp,

  //! In nanoseconds, timestamp is the time since the previous event (or zero)
  Relative,

  //! In nanoseconds, as per an arbitrary reference which may be provided by the host API,
  //! e.g. since the JACK cycle start, ALSA sequencer queue creation, through AudioHostTime on macOS.
  //! It offers the most precise ordering between events as it's the closest to the real timestamp of
  //! the event as provided by the host API.
  //! If the API does not provide any timing, it will be mapped to SystemMonotonic instead.
  Absolute,

  //! In nanoseconds, as per std::steady_clock::now() or equivalent (raw if possible).
  //! May be less precise than Absolute as timestamping is done within the library,
  //! but is more useful for system-wide synchronization.
  //! Note: depending on the backend, Absolute and SystemMonotonic may be the same.
  SystemMonotonic,
};

using message_callback = std::function<void(message&& message)>;
struct input_configuration
{
  //! Set a callback function to be invoked for incoming MIDI messages.
  /*!
    The callback function will be called whenever an incoming MIDI
    message is received.  While not absolutely necessary, it is best
    to set the callback function before opening a MIDI port to avoid
    leaving some messages in the queue.

    \param callback A callback function must be given.
  */
  message_callback on_message;

  //! Set an error callback function to be invoked when an error has occured.
  /*!
    The callback function will be called whenever an error has occured. It is
    best to set the error callback function before opening a port.
  */
  midi_error_callback on_error{};
  midi_error_callback on_warning{};

  //! Specify whether certain MIDI message types should be queued or ignored
  //! during input.
  /*!
    By default, MIDI timing and active sensing messages are ignored
    during message input because of their relative high data rates.
    MIDI sysex messages are ignored by default as well.  Variable
    values of "true" imply that the respective message type will be
    ignored.
  */
  uint32_t ignore_sysex : 1 = true;
  uint32_t ignore_timing : 1 = true;
  uint32_t ignore_sensing : 1 = true;

  uint32_t timestamps : 2 = timestamp_mode::Absolute;
};

}
