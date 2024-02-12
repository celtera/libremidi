#pragma once

#include <libremidi/backends/linux/dylib_loader.hpp>

#include <pipewire/pipewire.h>

namespace libremidi
{

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
class libpipewire
{
public:
  decltype(&::pw_init) init{};
  decltype(&::pw_deinit) deinit{};

  decltype(&::pw_context_new) context_new{};
  decltype(&::pw_context_connect) context_connect{};
  decltype(&::pw_context_destroy) context_destroy{};

  decltype(&::pw_core_disconnect) core_disconnect{};

  decltype(&::pw_proxy_add_listener) proxy_add_listener{};
  decltype(&::pw_proxy_destroy) proxy_destroy{};

  decltype(&::pw_main_loop_new) main_loop_new{};
  decltype(&::pw_main_loop_destroy) main_loop_destroy{};
  decltype(&::pw_main_loop_quit) main_loop_quit{};
  decltype(&::pw_main_loop_run) main_loop_run{};
  decltype(&::pw_main_loop_get_loop) main_loop_get_loop{};
  /*
  decltype(&::pw_thread_loop_new) thread_loop_new{};
  decltype(&::pw_thread_loop_destroy) thread_loop_destroy{};
  decltype(&::pw_thread_loop_wait) thread_loop_wait{};
  decltype(&::pw_thread_loop_start) thread_loop_start{};
  decltype(&::pw_thread_loop_stop) thread_loop_stop;
  decltype(&::pw_thread_loop_lock) thread_loop_lock{};
  decltype(&::pw_thread_loop_unlock) thread_loop_unlock;
  decltype(&::pw_thread_loop_get_loop) thread_loop_get_loop{};
*/
  decltype(&::pw_properties_new) properties_new{};
  decltype(&::pw_properties_free) properties_free{};
  decltype(&::pw_properties_get) properties_get{};

  decltype(&::pw_filter_new_simple) filter_new_simple{};
  decltype(&::pw_filter_get_node_id) filter_get_node_id{};
  decltype(&::pw_filter_get_properties) filter_get_properties{};
  decltype(&::pw_filter_add_port) filter_add_port{};
  decltype(&::pw_filter_remove_port) filter_remove_port{};
  decltype(&::pw_filter_update_properties) filter_update_properties{};
  decltype(&::pw_filter_update_params) filter_update_params{};
  decltype(&::pw_filter_get_time) filter_get_time{};
  decltype(&::pw_filter_destroy) filter_destroy{};
  decltype(&::pw_filter_connect) filter_connect{};
  decltype(&::pw_filter_get_dsp_buffer) filter_get_dsp_buffer{};
  decltype(&::pw_filter_queue_buffer) filter_queue_buffer{};
  decltype(&::pw_filter_dequeue_buffer) filter_dequeue_buffer{};
  decltype(&::pw_filter_flush) filter_flush{};

  static const libpipewire& instance()
  {
    static const libpipewire self;
    return self;
  }

  bool available{true};

private:
  dylib_loader library;

  libpipewire()
      : library("libpipewire-0.3.so.0")
  {
    if (!library)
    {
      available = false;
      return;
    }

    // in terms of regex:
    // decltype\(&::([a-z_]+)\) [a-z_]+{};
    // \1 = library.symbol<decltype(&::\1)>("\1");
    init = library.symbol<decltype(&::pw_init)>("pw_init");
    deinit = library.symbol<decltype(&::pw_deinit)>("pw_deinit");

    context_new = library.symbol<decltype(&::pw_context_new)>("pw_context_new");
    context_connect = library.symbol<decltype(&::pw_context_connect)>("pw_context_connect");
    context_destroy = library.symbol<decltype(&::pw_context_destroy)>("pw_context_destroy");

    core_disconnect = library.symbol<decltype(&::pw_core_disconnect)>("pw_core_disconnect");

    proxy_add_listener
        = library.symbol<decltype(&::pw_proxy_add_listener)>("pw_proxy_add_listener");
    proxy_destroy = library.symbol<decltype(&::pw_proxy_destroy)>("pw_proxy_destroy");

    main_loop_new = library.symbol<decltype(&::pw_main_loop_new)>("pw_main_loop_new");
    main_loop_destroy = library.symbol<decltype(&::pw_main_loop_destroy)>("pw_main_loop_destroy");
    main_loop_quit = library.symbol<decltype(&::pw_main_loop_quit)>("pw_main_loop_quit");
    main_loop_run = library.symbol<decltype(&::pw_main_loop_run)>("pw_main_loop_run");
    main_loop_get_loop
        = library.symbol<decltype(&::pw_main_loop_get_loop)>("pw_main_loop_get_loop");

    properties_new = library.symbol<decltype(&::pw_properties_new)>("pw_properties_new");
    properties_free = library.symbol<decltype(&::pw_properties_free)>("pw_properties_free");
    properties_get = library.symbol<decltype(&::pw_properties_get)>("pw_properties_get");

    filter_new_simple = library.symbol<decltype(&::pw_filter_new_simple)>("pw_filter_new_simple");
    filter_get_node_id
        = library.symbol<decltype(&::pw_filter_get_node_id)>("pw_filter_get_node_id");
    filter_get_properties
        = library.symbol<decltype(&::pw_filter_get_properties)>("pw_filter_get_properties");
    filter_add_port = library.symbol<decltype(&::pw_filter_add_port)>("pw_filter_add_port");
    filter_remove_port
        = library.symbol<decltype(&::pw_filter_remove_port)>("pw_filter_remove_port");
    filter_update_properties
        = library.symbol<decltype(&::pw_filter_update_properties)>("pw_filter_update_properties");
    filter_update_params
        = library.symbol<decltype(&::pw_filter_update_params)>("pw_filter_update_params");
    filter_get_time = library.symbol<decltype(&::pw_filter_get_time)>("pw_filter_get_time");
    filter_destroy = library.symbol<decltype(&::pw_filter_destroy)>("pw_filter_destroy");
    filter_connect = library.symbol<decltype(&::pw_filter_connect)>("pw_filter_connect");
    filter_get_dsp_buffer
        = library.symbol<decltype(&::pw_filter_get_dsp_buffer)>("pw_filter_get_dsp_buffer");
    filter_dequeue_buffer
        = library.symbol<decltype(&::pw_filter_dequeue_buffer)>("pw_filter_dequeue_buffer");
    filter_queue_buffer
        = library.symbol<decltype(&::pw_filter_queue_buffer)>("pw_filter_queue_buffer");
    filter_flush = library.symbol<decltype(&::pw_filter_flush)>("pw_filter_flush");

    assert(init);
    assert(deinit);

    assert(context_new);
    assert(context_connect);
    assert(context_destroy);

    assert(core_disconnect);

    assert(proxy_destroy);

    assert(main_loop_new);
    assert(main_loop_destroy);
    assert(main_loop_quit);
    assert(main_loop_run);
    assert(main_loop_get_loop);

    assert(properties_new);
    assert(properties_free);
    assert(properties_get);

    assert(filter_new_simple);
    assert(filter_get_node_id);
    assert(filter_get_properties);
    assert(filter_add_port);
    assert(filter_remove_port);
    assert(filter_update_properties);
    assert(filter_update_params);
    assert(filter_get_time);
    assert(filter_destroy);
    assert(filter_connect);
    assert(filter_get_dsp_buffer);
    assert(filter_dequeue_buffer);
    assert(filter_queue_buffer);
    assert(filter_flush);
  }
};
#pragma GCC diagnostic pop
}
