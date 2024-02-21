#pragma once
#include <libremidi/backends/pipewire/config.hpp>
#include <libremidi/backends/pipewire/helpers.hpp>
#include <libremidi/detail/midi_out.hpp>

#include <readerwriterqueue.h>

#include <semaphore>

namespace libremidi
{
class midi_out_pipewire
    : public midi1::out_api
    , public pipewire_helpers
    , public error_handler
{
public:
  struct
      : output_configuration
      , pipewire_output_configuration
  {
  } configuration;

  midi_out_pipewire(output_configuration&& conf, pipewire_output_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    connect(*this);
  }

  ~midi_out_pipewire() override
  {
    midi_out_pipewire::close_port();

    disconnect(*this);
  }

  void set_client_name(std::string_view) override
  {
    warning(configuration, "midi_out_pipewire: set_client_name unsupported");
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::PIPEWIRE; }

  bool open_port(const output_port& out_port, std::string_view name) override
  {
    if (!create_local_port(*this, name, SPA_DIRECTION_OUTPUT))
      return false;

    if (!link_ports(*this, out_port))
      return false;

    start_thread();
    return true;
  }

  bool open_virtual_port(std::string_view name) override
  {
    if (!create_local_port(*this, name, SPA_DIRECTION_OUTPUT))
      return false;

    start_thread();
    return true;
  }

  void close_port() override { return do_close_port(); }

  void set_port_name(std::string_view port_name) override { rename_port(port_name); }

  int process(spa_io_position*)
  {
    const auto b = pw.filter_dequeue_buffer(this->filter->port);
    if (!b)
      return 1;

    const auto buf = b->buffer;
    const auto d = &buf->datas[0];

    if (d->data == nullptr)
      return 1;

    int n_frames = d->maxsize;
    spa_pod_builder build;
    spa_zero(build);
    spa_pod_builder_init(&build, d->data, n_frames);

    spa_pod_frame f;
    spa_pod_builder_push_sequence(&build, &f, 0);

    // for all events
    message m;
    while (m_queue.try_dequeue(m))
    {
      if (m.empty())
        continue;

      // TODO why
      if (m.bytes[0] == 0xff)
        continue;

      spa_pod_builder_control(&build, m.timestamp, SPA_CONTROL_Midi);
      spa_pod_builder_bytes(&build, m.bytes.data(), m.bytes.size());
    }
    spa_pod_builder_pop(&build, &f);

    int n_fill_frames = build.state.offset;
    if (n_fill_frames > 0)
    {
      d->chunk->offset = 0;
      d->chunk->stride = 1;
      d->chunk->size = n_fill_frames;
      b->size = n_fill_frames;

      pw.filter_queue_buffer(this->filter->port, b);
      return 0;
    }

    pw.filter_flush(this->filter->filter, true);

    return 0;
  }

  void send_message(const unsigned char* message, size_t size) override
  {
    m_queue.enqueue(libremidi::message(midi_bytes{message, message + size}, 0));
  }

  int convert_timestamp(int64_t user) const noexcept
  {
    switch (configuration.timestamps)
    {
      case timestamp_mode::AudioFrame:
        return static_cast<int>(user);

      default:
        // TODO
        return 0;
    }
  }

  void schedule_message(int64_t ts, const unsigned char* message, size_t size) override
  {
    m_queue.enqueue(
        libremidi::message(midi_bytes{message, message + size}, convert_timestamp(ts)));
  }

  moodycamel::ReaderWriterQueue<libremidi::message> m_queue;
};
}
