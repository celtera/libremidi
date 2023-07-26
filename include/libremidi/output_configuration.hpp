#pragma once
#include <libremidi/config.hpp>
#include <libremidi/error.hpp>

#include <string>

namespace libremidi
{
struct output_configuration
{
  //! Set an error callback function to be invoked when an error has occured.
  /*!
    The callback function will be called whenever an error has occured. It is
    best to set the error callback function before opening a port.
  */
  midi_error_callback on_error{};
  midi_error_callback on_warning{};
};
}
