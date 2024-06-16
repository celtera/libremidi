#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(LIBREMIDI_EXPORTS)
  #if defined(_MSC_VER)
    #define LIBREMIDI_EXPORT __declspec(dllexport)
  #elif defined(__GNUC__) || defined(__clang__)
    #define LIBREMIDI_EXPORT __attribute__((visibility("default")))
  #endif
#else
  #define LIBREMIDI_EXPORT
#endif

#if __cplusplus
extern "C" {
#endif
typedef unsigned char midi1_symbol;
typedef unsigned char* midi1_message;

typedef uint32_t midi2_symbol;
typedef midi2_symbol* midi2_message;

typedef int64_t libremidi_timestamp;

typedef struct libremidi_midi_in_port libremidi_midi_in_port;
typedef struct libremidi_midi_out_port libremidi_midi_out_port;
typedef struct libremidi_midi_in_handle libremidi_midi_in_handle;
typedef struct libremidi_midi_out_handle libremidi_midi_out_handle;
typedef struct libremidi_midi_observer_handle libremidi_midi_observer_handle;

typedef struct libremidi_api_configuration libremidi_api_configuration;

enum libremidi_timestamp_mode
{
  NoTimestamp,

  Relative,
  Absolute,
  SystemMonotonic,
  AudioFrame,
  Custom
};

enum libremidi_api
{
  UNSPECIFIED, /*!< Search for a working compiled API. */

  // MIDI 1.0 APIs
  COREMIDI,    /*!< macOS CoreMidi API. */
  ALSA_SEQ,    /*!< Linux ALSA Sequencer API. */
  ALSA_RAW,    /*!< Linux Raw ALSA API. */
  JACK_MIDI,   /*!< JACK Low-Latency MIDI Server API. */
  WINDOWS_MM,  /*!< Microsoft Multimedia MIDI API. */
  WINDOWS_UWP, /*!< Microsoft WinRT MIDI API. */
  WEBMIDI,     /*!< Web MIDI API through Emscripten */
  PIPEWIRE,    /*!< PipeWire */

  // MIDI 2.0 APIs
  ALSA_RAW_UMP,          /*!< Raw ALSA API for MIDI 2.0 */
  ALSA_SEQ_UMP,          /*!< Linux ALSA Sequencer API for MIDI 2.0 */
  COREMIDI_UMP,          /*!< macOS CoreMidi API for MIDI 2.0. Requires macOS 11+ */
  WINDOWS_MIDI_SERVICES, /*!< Windows API for MIDI 2.0. Requires Windows 11 */

  DUMMY /*!< A compilable but non-functional API. */
};

typedef struct libremidi_api_configuration
{
  enum libremidi_api api;
  enum
  {
    Observer,
    Input,
    Output
  } configuration_type;

  void* data;
} libremidi_api_configuration;

typedef struct libremidi_observer_configuration
{
  struct
  {
    void* context;
    void (*callback)(void* ctx, const char* error, size_t error_len, const void* source_location);
  } on_error;
  struct
  {
    void* context;
    void (*callback)(void* ctx, const char* error, size_t error_len, const void* source_location);
  } on_warning;

  struct
  {
    void* context;
    void (*callback)(void* ctx, const libremidi_midi_in_port*);
  } input_added;
  struct
  {
    void* context;
    void (*callback)(void* ctx, const libremidi_midi_in_port*);
  } input_removed;
  struct
  {
    void* context;
    void (*callback)(void* ctx, const libremidi_midi_out_port*);
  } output_added;
  struct
  {
    void* context;
    void (*callback)(void* ctx, const libremidi_midi_out_port*);
  } output_removed;

  bool track_hardware;
  bool track_virtual;
  bool track_any;
  bool notify_in_constructor;
} libremidi_observer_configuration;

typedef struct libremidi_midi_configuration
{
  enum
  {
    MIDI1,
    MIDI2
  } version;

  union
  {
    libremidi_midi_in_port* in_port;
    libremidi_midi_out_port* out_port;
  };

  union
  {
    struct
    {
      void* context;
      void (*callback)(void* ctx, const midi1_symbol*, size_t len);
    } on_midi1_message;
    struct
    {
      void* context;
      void (*callback)(void* ctx, const midi2_symbol*, size_t len);
    } on_midi2_message;
  };

  struct
  {
    void* context;
    libremidi_timestamp (*callback)(void* ctx, libremidi_timestamp);
  } get_timestamp;

  struct
  {
    void* context;
    void (*callback)(void* ctx, const char* error, size_t error_len, const void* source_location);
  } on_error;
  struct
  {
    void* context;
    void (*callback)(void* ctx, const char* error, size_t error_len, const void* source_location);
  } on_warning;

  const char* port_name;
  bool virtual_port;

  bool ignore_sysex;
  bool ignore_timing;
  bool ignore_sensing;

  enum libremidi_timestamp_mode timestamps;
} libremidi_midi_configuration;

LIBREMIDI_EXPORT
int libremidi_midi_api_configuration_init(libremidi_api_configuration*);

LIBREMIDI_EXPORT
int libremidi_midi_observer_configuration_init(libremidi_observer_configuration*);

LIBREMIDI_EXPORT
int libremidi_midi_configuration_init(libremidi_midi_configuration*);

LIBREMIDI_EXPORT
int libremidi_midi_in_port_clone(const libremidi_midi_in_port* port, libremidi_midi_in_port** dst);

LIBREMIDI_EXPORT
int libremidi_midi_in_port_free(libremidi_midi_in_port* port);

LIBREMIDI_EXPORT
int libremidi_midi_in_port_name(
    const libremidi_midi_in_port* port, const char** name, size_t* len);

LIBREMIDI_EXPORT
int libremidi_midi_out_port_clone(
    const libremidi_midi_out_port* port, libremidi_midi_out_port** dst);

LIBREMIDI_EXPORT
int libremidi_midi_out_port_free(libremidi_midi_out_port* port);

LIBREMIDI_EXPORT
int libremidi_midi_out_port_name(
    const libremidi_midi_out_port* port, const char** name, size_t* len);

LIBREMIDI_EXPORT
int libremidi_midi_observer_new(
    const libremidi_observer_configuration*, libremidi_api_configuration*,
    libremidi_midi_observer_handle**);

LIBREMIDI_EXPORT
int libremidi_midi_observer_enumerate_input_ports(
    libremidi_midi_observer_handle*, void* context,
    void (*)(void* ctx, const libremidi_midi_in_port*));

LIBREMIDI_EXPORT
int libremidi_midi_observer_enumerate_output_ports(
    libremidi_midi_observer_handle*, void* context,
    void (*)(void* ctx, const libremidi_midi_out_port*));

LIBREMIDI_EXPORT
int libremidi_midi_observer_free(libremidi_midi_observer_handle*);

LIBREMIDI_EXPORT
int libremidi_midi_in_new(
    const libremidi_midi_configuration*, const libremidi_api_configuration*,
    libremidi_midi_in_handle**);

LIBREMIDI_EXPORT
int libremidi_midi_in_is_connected(const libremidi_midi_in_handle*);

LIBREMIDI_EXPORT
libremidi_timestamp libremidi_midi_in_absolute_timestamp(libremidi_midi_in_handle*);

LIBREMIDI_EXPORT
int libremidi_midi_in_free(libremidi_midi_in_handle*);

LIBREMIDI_EXPORT
int libremidi_midi_out_new(
    const libremidi_midi_configuration*, const libremidi_api_configuration*,
    libremidi_midi_out_handle**);

LIBREMIDI_EXPORT
int libremidi_midi_out_is_connected(const libremidi_midi_out_handle*);

LIBREMIDI_EXPORT
int libremidi_midi_out_send_message(libremidi_midi_out_handle*, const midi1_symbol*, size_t);

LIBREMIDI_EXPORT
int libremidi_midi_out_send_ump(libremidi_midi_out_handle*, const midi2_symbol*, size_t);

LIBREMIDI_EXPORT
int libremidi_midi_out_schedule_message(
    libremidi_midi_out_handle*, int64_t ts, const midi1_symbol*, size_t);

LIBREMIDI_EXPORT
int libremidi_midi_out_schedule_ump(
    libremidi_midi_out_handle*, int64_t ts, const midi2_symbol*, size_t);

LIBREMIDI_EXPORT
int libremidi_midi_out_free(libremidi_midi_out_handle*);

#if __cplusplus
}
#endif
