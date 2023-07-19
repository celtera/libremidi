#pragma once
#include <libremidi/config.hpp>

#include <libremidi/message.hpp>
#include <libremidi/error.hpp>

#include <string>
#include <functional>

namespace libremidi
{
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

  //! Specify how timestamps are handled in the system
  enum timestamp_mode
  {
    NoTimestamp,
    Relative, // Revert to old behaviour
    Absolute  // In nanoseconds, as per std::high_resolution_clock::now()
  };

  uint32_t timestamps : 2 = timestamp_mode::Relative;
};

}
