#pragma once

#if __has_include(<dlfcn.h>)
  #include <alsa/asoundlib.h>

  #include <dlfcn.h>

  #include <cassert>

namespace libremidi
{
class dylib_loader
{
public:
  explicit dylib_loader(const char* const so)
  {
    impl = dlopen(so, RTLD_LAZY | RTLD_LOCAL | RTLD_NODELETE);
  }

  dylib_loader(const dylib_loader&) noexcept = delete;
  dylib_loader& operator=(const dylib_loader&) noexcept = delete;
  dylib_loader(dylib_loader&& other) noexcept
  {
    impl = other.impl;
    other.impl = nullptr;
  }

  dylib_loader& operator=(dylib_loader&& other) noexcept
  {
    impl = other.impl;
    other.impl = nullptr;
    return *this;
  }

  ~dylib_loader()
  {
    if (impl)
    {
      dlclose(impl);
    }
  }

  template <typename T>
  T symbol(const char* const sym) const noexcept
  {
    assert(impl);
    return reinterpret_cast<T>(dlsym(impl, sym));
  }

  operator bool() const noexcept { return bool(impl); }

private:
  void* impl{};
};

struct libasound
{
  // Useful one-liner:
  // nm -A * | grep ' snd_' | grep -v '@' | cut -f 2 -d 'U' | sort | uniq  | sed 's/ snd_//' | sed 's/_/, /' | awk ' { print "LIBREMIDI_SYMBOL_DEF("$1 " " $2 ");" }'

  #define LIBREMIDI_SYMBOL_NAME_S(prefix, name) "snd_" #prefix "_" #name
  #define LIBREMIDI_SYMBOL_NAME(prefix, name) snd_##prefix##_##name
  #define LIBREMIDI_SYMBOL_DEF(prefix, name) \
    decltype(&::LIBREMIDI_SYMBOL_NAME(prefix, name)) name{};
  #define LIBREMIDI_SYMBOL_INIT(prefix, name)                                  \
    {                                                                          \
      name = library.symbol<decltype(&::LIBREMIDI_SYMBOL_NAME(prefix, name))>( \
          LIBREMIDI_SYMBOL_NAME_S(prefix, name));                              \
      if (!name)                                                               \
      {                                                                        \
        available = false;                                                     \
        return;                                                                \
      }                                                                        \
    }

  explicit libasound()
      : library{"libasound.so.2"}
  {
    if (!library)
    {
      available = false;
      return;
    }

    strerror = library.symbol<decltype(&::snd_strerror)>("snd_strerror");
    if (!strerror)
      available = false;
  }

  static const libasound& instance()
  {
    static const libasound self;
    return self;
  }

  dylib_loader library;
  decltype(&::snd_strerror) strerror{};
  bool available{true};

  struct card_t
  {
    explicit card_t(const dylib_loader& library)
    {
      if (!library)
      {
        available = false;
        return;
      }

      LIBREMIDI_SYMBOL_INIT(card, get_name);
      LIBREMIDI_SYMBOL_INIT(card, next);
    }
    bool available{true};

    LIBREMIDI_SYMBOL_DEF(card, get_name);
    LIBREMIDI_SYMBOL_DEF(card, next);
  } card{library};

  struct ctl_t
  {
    explicit ctl_t(const dylib_loader& library)
        : rawmidi{library}
  #if __has_include(<alsa/ump.h>)
        , ump{library}
  #endif
    {
      if (!library)
      {
        available = false;
        return;
      }

      LIBREMIDI_SYMBOL_INIT(ctl, close);
      LIBREMIDI_SYMBOL_INIT(ctl, open);
    }
    bool available{true};

    LIBREMIDI_SYMBOL_DEF(ctl, close);
    LIBREMIDI_SYMBOL_DEF(ctl, open);

    struct rawmidi_t
    {
      explicit rawmidi_t(const dylib_loader& library)
      {
        if (!library)
        {
          available = false;
          return;
        }

        LIBREMIDI_SYMBOL_INIT(ctl_rawmidi, info);
        LIBREMIDI_SYMBOL_INIT(ctl_rawmidi, next_device);
      }
      bool available{true};
      LIBREMIDI_SYMBOL_DEF(ctl_rawmidi, info);
      LIBREMIDI_SYMBOL_DEF(ctl_rawmidi, next_device);
    } rawmidi;

  #if __has_include(<alsa/ump.h>)
    struct ump_t
    {
      explicit ump_t(const dylib_loader& library)
      {
        if (!library)
        {
          available = false;
          return;
        }

        LIBREMIDI_SYMBOL_INIT(ctl_ump, block_info);
        LIBREMIDI_SYMBOL_INIT(ctl_ump, endpoint_info);
        LIBREMIDI_SYMBOL_INIT(ctl_ump, next_device);
      }
      bool available{true};
      LIBREMIDI_SYMBOL_DEF(ctl_ump, block_info);
      LIBREMIDI_SYMBOL_DEF(ctl_ump, endpoint_info);
      LIBREMIDI_SYMBOL_DEF(ctl_ump, next_device);
    } ump;
  #endif
  } ctl{library};

  struct midi_t
  {
    explicit midi_t(const dylib_loader& library)
    {
      if (!library)
      {
        available = false;
        return;
      }

      LIBREMIDI_SYMBOL_INIT(midi, event_decode);
      LIBREMIDI_SYMBOL_INIT(midi, event_encode);
      LIBREMIDI_SYMBOL_INIT(midi, event_free);
      LIBREMIDI_SYMBOL_INIT(midi, event_init);
      LIBREMIDI_SYMBOL_INIT(midi, event_new);
      LIBREMIDI_SYMBOL_INIT(midi, event_no_status);
      LIBREMIDI_SYMBOL_INIT(midi, event_resize_buffer);
    }

    bool available{true};
    LIBREMIDI_SYMBOL_DEF(midi, event_decode);
    LIBREMIDI_SYMBOL_DEF(midi, event_encode);
    LIBREMIDI_SYMBOL_DEF(midi, event_free);
    LIBREMIDI_SYMBOL_DEF(midi, event_init);
    LIBREMIDI_SYMBOL_DEF(midi, event_new);
    LIBREMIDI_SYMBOL_DEF(midi, event_no_status);
    LIBREMIDI_SYMBOL_DEF(midi, event_resize_buffer);
  } midi{library};

  #if __has_include(<alsa/rawmidi.h>)
  struct rawmidi_t
  {
    explicit rawmidi_t(const dylib_loader& library)
    {
      if (!library)
      {
        available = false;
        return;
      }
      LIBREMIDI_SYMBOL_INIT(rawmidi, close);
      LIBREMIDI_SYMBOL_INIT(rawmidi, info_get_name);
      LIBREMIDI_SYMBOL_INIT(rawmidi, info_get_subdevice_name);
      LIBREMIDI_SYMBOL_INIT(rawmidi, info_get_subdevices_count);
      LIBREMIDI_SYMBOL_INIT(rawmidi, info_set_device);
      LIBREMIDI_SYMBOL_INIT(rawmidi, info_set_stream);
      LIBREMIDI_SYMBOL_INIT(rawmidi, info_set_subdevice);
      LIBREMIDI_SYMBOL_INIT(rawmidi, info_sizeof);
      LIBREMIDI_SYMBOL_INIT(rawmidi, open);
      LIBREMIDI_SYMBOL_INIT(rawmidi, params);
      LIBREMIDI_SYMBOL_INIT(rawmidi, params_current);
      LIBREMIDI_SYMBOL_INIT(rawmidi, params_get_buffer_size);
      LIBREMIDI_SYMBOL_INIT(rawmidi, params_set_clock_type);
      LIBREMIDI_SYMBOL_INIT(rawmidi, params_set_no_active_sensing);
      LIBREMIDI_SYMBOL_INIT(rawmidi, params_set_read_mode);
      LIBREMIDI_SYMBOL_INIT(rawmidi, params_sizeof);
      LIBREMIDI_SYMBOL_INIT(rawmidi, poll_descriptors);
      LIBREMIDI_SYMBOL_INIT(rawmidi, poll_descriptors_count);
      LIBREMIDI_SYMBOL_INIT(rawmidi, poll_descriptors_revents);
      LIBREMIDI_SYMBOL_INIT(rawmidi, read);
      LIBREMIDI_SYMBOL_INIT(rawmidi, status);
      LIBREMIDI_SYMBOL_INIT(rawmidi, status_get_avail);
      LIBREMIDI_SYMBOL_INIT(rawmidi, status_sizeof);
      LIBREMIDI_SYMBOL_INIT(rawmidi, tread);
      LIBREMIDI_SYMBOL_INIT(rawmidi, write);
    }

    bool available{true};
    LIBREMIDI_SYMBOL_DEF(rawmidi, close);
    LIBREMIDI_SYMBOL_DEF(rawmidi, info_get_name);
    LIBREMIDI_SYMBOL_DEF(rawmidi, info_get_subdevice_name);
    LIBREMIDI_SYMBOL_DEF(rawmidi, info_get_subdevices_count);
    LIBREMIDI_SYMBOL_DEF(rawmidi, info_set_device);
    LIBREMIDI_SYMBOL_DEF(rawmidi, info_set_stream);
    LIBREMIDI_SYMBOL_DEF(rawmidi, info_set_subdevice);
    LIBREMIDI_SYMBOL_DEF(rawmidi, info_sizeof);
    LIBREMIDI_SYMBOL_DEF(rawmidi, open);
    LIBREMIDI_SYMBOL_DEF(rawmidi, params);
    LIBREMIDI_SYMBOL_DEF(rawmidi, params_current);
    LIBREMIDI_SYMBOL_DEF(rawmidi, params_get_buffer_size);
    LIBREMIDI_SYMBOL_DEF(rawmidi, params_set_clock_type);
    LIBREMIDI_SYMBOL_DEF(rawmidi, params_set_no_active_sensing);
    LIBREMIDI_SYMBOL_DEF(rawmidi, params_set_read_mode);
    LIBREMIDI_SYMBOL_DEF(rawmidi, params_sizeof);
    LIBREMIDI_SYMBOL_DEF(rawmidi, poll_descriptors);
    LIBREMIDI_SYMBOL_DEF(rawmidi, poll_descriptors_count);
    LIBREMIDI_SYMBOL_DEF(rawmidi, poll_descriptors_revents);
    LIBREMIDI_SYMBOL_DEF(rawmidi, read);
    LIBREMIDI_SYMBOL_DEF(rawmidi, status);
    LIBREMIDI_SYMBOL_DEF(rawmidi, status_get_avail);
    LIBREMIDI_SYMBOL_DEF(rawmidi, status_sizeof);
    LIBREMIDI_SYMBOL_DEF(rawmidi, tread);
    LIBREMIDI_SYMBOL_DEF(rawmidi, write);
  } rawmidi{library};
  #endif

  struct seq_t
  {
    explicit seq_t(const dylib_loader& library)
  #if __has_include(<alsa/ump.h>)
        : ump{library}
  #endif
    {
      if (!library)
      {
        available = false;
        return;
      }

      LIBREMIDI_SYMBOL_INIT(seq, alloc_queue);
      LIBREMIDI_SYMBOL_INIT(seq, client_id);
      LIBREMIDI_SYMBOL_INIT(seq, client_info_get_client);
      LIBREMIDI_SYMBOL_INIT(seq, client_info_get_name);
      LIBREMIDI_SYMBOL_INIT(seq, client_info_set_client);
      LIBREMIDI_SYMBOL_INIT(seq, client_info_sizeof);
      LIBREMIDI_SYMBOL_INIT(seq, close);
      LIBREMIDI_SYMBOL_INIT(seq, connect_from);
      LIBREMIDI_SYMBOL_INIT(seq, control_queue);
      LIBREMIDI_SYMBOL_INIT(seq, create_port);
      LIBREMIDI_SYMBOL_INIT(seq, delete_port);
      LIBREMIDI_SYMBOL_INIT(seq, drain_output);
      LIBREMIDI_SYMBOL_INIT(seq, event_input);
      LIBREMIDI_SYMBOL_INIT(seq, event_input_pending);
      LIBREMIDI_SYMBOL_INIT(seq, event_output);
      LIBREMIDI_SYMBOL_INIT(seq, free_event);
      LIBREMIDI_SYMBOL_INIT(seq, free_queue);
      LIBREMIDI_SYMBOL_INIT(seq, get_any_client_info);
      LIBREMIDI_SYMBOL_INIT(seq, get_any_port_info);
      LIBREMIDI_SYMBOL_INIT(seq, get_port_info);
      LIBREMIDI_SYMBOL_INIT(seq, open);
      LIBREMIDI_SYMBOL_INIT(seq, poll_descriptors);
      LIBREMIDI_SYMBOL_INIT(seq, poll_descriptors_count);
      LIBREMIDI_SYMBOL_INIT(seq, port_info_get_addr);
      LIBREMIDI_SYMBOL_INIT(seq, port_info_get_capability);
      LIBREMIDI_SYMBOL_INIT(seq, port_info_get_name);
      LIBREMIDI_SYMBOL_INIT(seq, port_info_get_port);
      LIBREMIDI_SYMBOL_INIT(seq, port_info_get_type);
      LIBREMIDI_SYMBOL_INIT(seq, port_info_set_capability);
      LIBREMIDI_SYMBOL_INIT(seq, port_info_set_client);
      LIBREMIDI_SYMBOL_INIT(seq, port_info_set_midi_channels);
      LIBREMIDI_SYMBOL_INIT(seq, port_info_set_name);
      LIBREMIDI_SYMBOL_INIT(seq, port_info_set_port);
      LIBREMIDI_SYMBOL_INIT(seq, port_info_set_timestamping);
      LIBREMIDI_SYMBOL_INIT(seq, port_info_set_timestamp_queue);
      LIBREMIDI_SYMBOL_INIT(seq, port_info_set_timestamp_real);
      LIBREMIDI_SYMBOL_INIT(seq, port_info_set_type);
      LIBREMIDI_SYMBOL_INIT(seq, port_info_sizeof);
      LIBREMIDI_SYMBOL_INIT(seq, port_subscribe_free);
      LIBREMIDI_SYMBOL_INIT(seq, port_subscribe_malloc);
      LIBREMIDI_SYMBOL_INIT(seq, port_subscribe_set_dest);
      LIBREMIDI_SYMBOL_INIT(seq, port_subscribe_set_sender);
      LIBREMIDI_SYMBOL_INIT(seq, port_subscribe_set_time_real);
      LIBREMIDI_SYMBOL_INIT(seq, port_subscribe_set_time_update);
      LIBREMIDI_SYMBOL_INIT(seq, query_next_client);
      LIBREMIDI_SYMBOL_INIT(seq, query_next_port);
      LIBREMIDI_SYMBOL_INIT(seq, queue_tempo_set_ppq);
      LIBREMIDI_SYMBOL_INIT(seq, queue_tempo_set_tempo);
      LIBREMIDI_SYMBOL_INIT(seq, queue_tempo_sizeof);
      LIBREMIDI_SYMBOL_INIT(seq, set_client_midi_version);
      LIBREMIDI_SYMBOL_INIT(seq, set_client_name);
      LIBREMIDI_SYMBOL_INIT(seq, set_port_info);
      LIBREMIDI_SYMBOL_INIT(seq, set_queue_tempo);
      LIBREMIDI_SYMBOL_INIT(seq, subscribe_port);
      LIBREMIDI_SYMBOL_INIT(seq, unsubscribe_port);
    }

    bool available{true};
    LIBREMIDI_SYMBOL_DEF(seq, alloc_queue);
    LIBREMIDI_SYMBOL_DEF(seq, client_id);
    LIBREMIDI_SYMBOL_DEF(seq, client_info_get_client);
    LIBREMIDI_SYMBOL_DEF(seq, client_info_get_name);
    LIBREMIDI_SYMBOL_DEF(seq, client_info_set_client);
    LIBREMIDI_SYMBOL_DEF(seq, client_info_sizeof);
    LIBREMIDI_SYMBOL_DEF(seq, close);
    LIBREMIDI_SYMBOL_DEF(seq, connect_from);
    LIBREMIDI_SYMBOL_DEF(seq, control_queue);
    LIBREMIDI_SYMBOL_DEF(seq, create_port);
    LIBREMIDI_SYMBOL_DEF(seq, delete_port);
    LIBREMIDI_SYMBOL_DEF(seq, drain_output);
    LIBREMIDI_SYMBOL_DEF(seq, event_input);
    LIBREMIDI_SYMBOL_DEF(seq, event_input_pending);
    LIBREMIDI_SYMBOL_DEF(seq, event_output);
    LIBREMIDI_SYMBOL_DEF(seq, free_event);
    LIBREMIDI_SYMBOL_DEF(seq, free_queue);
    LIBREMIDI_SYMBOL_DEF(seq, get_any_client_info);
    LIBREMIDI_SYMBOL_DEF(seq, get_any_port_info);
    LIBREMIDI_SYMBOL_DEF(seq, get_port_info);
    LIBREMIDI_SYMBOL_DEF(seq, open);
    LIBREMIDI_SYMBOL_DEF(seq, poll_descriptors);
    LIBREMIDI_SYMBOL_DEF(seq, poll_descriptors_count);
    LIBREMIDI_SYMBOL_DEF(seq, port_info_get_addr);
    LIBREMIDI_SYMBOL_DEF(seq, port_info_get_capability);
    LIBREMIDI_SYMBOL_DEF(seq, port_info_get_name);
    LIBREMIDI_SYMBOL_DEF(seq, port_info_get_port);
    LIBREMIDI_SYMBOL_DEF(seq, port_info_get_type);
    LIBREMIDI_SYMBOL_DEF(seq, port_info_set_capability);
    LIBREMIDI_SYMBOL_DEF(seq, port_info_set_client);
    LIBREMIDI_SYMBOL_DEF(seq, port_info_set_midi_channels);
    LIBREMIDI_SYMBOL_DEF(seq, port_info_set_name);
    LIBREMIDI_SYMBOL_DEF(seq, port_info_set_port);
    LIBREMIDI_SYMBOL_DEF(seq, port_info_set_timestamping);
    LIBREMIDI_SYMBOL_DEF(seq, port_info_set_timestamp_queue);
    LIBREMIDI_SYMBOL_DEF(seq, port_info_set_timestamp_real);
    LIBREMIDI_SYMBOL_DEF(seq, port_info_set_type);
    LIBREMIDI_SYMBOL_DEF(seq, port_info_sizeof);
    LIBREMIDI_SYMBOL_DEF(seq, port_subscribe_free);
    LIBREMIDI_SYMBOL_DEF(seq, port_subscribe_malloc);
    LIBREMIDI_SYMBOL_DEF(seq, port_subscribe_set_dest);
    LIBREMIDI_SYMBOL_DEF(seq, port_subscribe_set_sender);
    LIBREMIDI_SYMBOL_DEF(seq, port_subscribe_set_time_real);
    LIBREMIDI_SYMBOL_DEF(seq, port_subscribe_set_time_update);
    LIBREMIDI_SYMBOL_DEF(seq, query_next_client);
    LIBREMIDI_SYMBOL_DEF(seq, query_next_port);
    LIBREMIDI_SYMBOL_DEF(seq, queue_tempo_set_ppq);
    LIBREMIDI_SYMBOL_DEF(seq, queue_tempo_set_tempo);
    LIBREMIDI_SYMBOL_DEF(seq, queue_tempo_sizeof);
    LIBREMIDI_SYMBOL_DEF(seq, set_client_midi_version);
    LIBREMIDI_SYMBOL_DEF(seq, set_client_name);
    LIBREMIDI_SYMBOL_DEF(seq, set_port_info);
    LIBREMIDI_SYMBOL_DEF(seq, set_queue_tempo);
    LIBREMIDI_SYMBOL_DEF(seq, subscribe_port);
    LIBREMIDI_SYMBOL_DEF(seq, unsubscribe_port);

  #if __has_include(<alsa/ump.h>)
    struct ump_t
    {
      explicit ump_t(const dylib_loader& library)
      {
        if (!library)
        {
          available = false;
          return;
        }
        LIBREMIDI_SYMBOL_INIT(seq_ump, event_input);
        LIBREMIDI_SYMBOL_INIT(seq_ump, event_output);
      }

      bool available{true};
      LIBREMIDI_SYMBOL_DEF(seq_ump, event_input);
      LIBREMIDI_SYMBOL_DEF(seq_ump, event_output);
    } ump;
  #endif
  } seq{library};

  #if __has_include(<alsa/ump.h>)
  struct ump_t
  {
    explicit ump_t(const dylib_loader& library)
    {
      if (!library)
      {
        available = false;
        return;
      }

      LIBREMIDI_SYMBOL_INIT(ump, block_info_get_name);
      LIBREMIDI_SYMBOL_INIT(ump, block_info_sizeof);
      LIBREMIDI_SYMBOL_INIT(ump, close);
      LIBREMIDI_SYMBOL_INIT(ump, endpoint_info_get_name);
      LIBREMIDI_SYMBOL_INIT(ump, endpoint_info_sizeof);
      LIBREMIDI_SYMBOL_INIT(ump, open);
      LIBREMIDI_SYMBOL_INIT(ump, poll_descriptors);
      LIBREMIDI_SYMBOL_INIT(ump, poll_descriptors_count);
      LIBREMIDI_SYMBOL_INIT(ump, poll_descriptors_revents);
      LIBREMIDI_SYMBOL_INIT(ump, rawmidi);
      LIBREMIDI_SYMBOL_INIT(ump, rawmidi_params);
      LIBREMIDI_SYMBOL_INIT(ump, rawmidi_params_current);
      LIBREMIDI_SYMBOL_INIT(ump, read);
      LIBREMIDI_SYMBOL_INIT(ump, tread);
    }

    bool available{true};
    LIBREMIDI_SYMBOL_DEF(ump, block_info_get_name);
    LIBREMIDI_SYMBOL_DEF(ump, block_info_sizeof);
    LIBREMIDI_SYMBOL_DEF(ump, close);
    LIBREMIDI_SYMBOL_DEF(ump, endpoint_info_get_name);
    LIBREMIDI_SYMBOL_DEF(ump, endpoint_info_sizeof);
    LIBREMIDI_SYMBOL_DEF(ump, open);
    LIBREMIDI_SYMBOL_DEF(ump, poll_descriptors);
    LIBREMIDI_SYMBOL_DEF(ump, poll_descriptors_count);
    LIBREMIDI_SYMBOL_DEF(ump, poll_descriptors_revents);
    LIBREMIDI_SYMBOL_DEF(ump, rawmidi);
    LIBREMIDI_SYMBOL_DEF(ump, rawmidi_params);
    LIBREMIDI_SYMBOL_DEF(ump, rawmidi_params_current);
    LIBREMIDI_SYMBOL_DEF(ump, read);
    LIBREMIDI_SYMBOL_DEF(ump, tread);
  } ump{library};
  #endif
};

  #undef snd_dylib_alloca
  #define snd_dylib_alloca(ptr, access, type)                                \
    {                                                                        \
      *ptr = (snd_##access##_##type##_t*)alloca(snd.access.type##_sizeof()); \
      memset(*ptr, 0, snd.access.type##_sizeof());                           \
    }
  #define snd_dylib_alloca2(ptr, access1, access2, type)                                         \
    {                                                                                            \
      *ptr = (snd_##access1##_access2##_##type##_t*)alloca(snd.access1.access2.type##_sizeof()); \
      memset(*ptr, 0, snd.access1.access2.type##_sizeof());                                      \
    }

  #undef snd_rawmidi_info_alloca
  #define snd_rawmidi_info_alloca(ptr) snd_dylib_alloca(ptr, rawmidi, info)
  #undef snd_rawmidi_params_alloca
  #define snd_rawmidi_params_alloca(ptr) snd_dylib_alloca(ptr, rawmidi, params)
  #undef snd_rawmidi_status_alloca
  #define snd_rawmidi_status_alloca(ptr) snd_dylib_alloca(ptr, rawmidi, status)

  #undef snd_seq_client_info_alloca
  #define snd_seq_client_info_alloca(ptr) snd_dylib_alloca(ptr, seq, client_info)
  #undef snd_seq_port_info_alloca
  #define snd_seq_port_info_alloca(ptr) snd_dylib_alloca(ptr, seq, port_info)
  #undef snd_seq_queue_tempo_alloca
  #define snd_seq_queue_tempo_alloca(ptr) snd_dylib_alloca(ptr, seq, queue_tempo)

  #if __has_include(<alsa/ump.h>)
    #undef snd_ump_block_info_alloca
    #define snd_ump_block_info_alloca(ptr) snd_dylib_alloca(ptr, ump, block_info)
    #undef snd_ump_endpoint_info_alloca
    #define snd_ump_endpoint_info_alloca(ptr) snd_dylib_alloca(ptr, ump, endpoint_info)
  #endif
}
#endif
