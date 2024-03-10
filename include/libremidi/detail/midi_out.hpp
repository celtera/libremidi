#pragma once
#include <libremidi/cmidi2.hpp>
#include <libremidi/detail/midi_api.hpp>
#include <libremidi/output_configuration.hpp>
#include <libremidi/error_handler.hpp>

#include <string_view>

namespace libremidi
{

class midi_out_api : public midi_api
{
public:
  midi_out_api() = default;
  ~midi_out_api() override = default;
  midi_out_api(const midi_out_api&) = delete;
  midi_out_api(midi_out_api&&) = delete;
  midi_out_api& operator=(const midi_out_api&) = delete;
  midi_out_api& operator=(midi_out_api&&) = delete;

  [[nodiscard]] virtual stdx::error
  open_port(const output_port& pt, std::string_view local_port_name)
      = 0;

  [[nodiscard]] virtual int64_t current_time() const noexcept { return 0; }

  virtual stdx::error send_message(const unsigned char* message, std::size_t size) = 0;
  virtual stdx::error
  schedule_message(int64_t /*ts*/, const unsigned char* message, std::size_t size)
  {
    return send_message(message, size);
  }

  virtual stdx::error send_ump(const uint32_t* message, std::size_t size) = 0;
  virtual stdx::error schedule_ump(int64_t /*ts*/, const uint32_t* ump, std::size_t size)
  {
    return send_ump(ump, size);
  }
};

namespace midi1
{
class out_api : public midi_out_api
{
  friend struct midi_stream_decoder;

public:
  using midi_out_api::midi_out_api;

  stdx::error send_ump(const uint32_t* message, std::size_t /*size*/)
  {
    uint8_t midi[65536];
    const auto n
        = cmidi2_convert_single_ump_to_midi1(midi, sizeof(midi), const_cast<uint32_t*>(message));
    if (n > 0)
      return send_message(midi, n);
    else
      return std::errc::no_buffer_space;
  }
};
}

namespace midi2
{
class out_api : public midi_out_api
{
  friend struct midi_stream_decoder;

public:
  using midi_out_api::midi_out_api;

  stdx::error send_message(const unsigned char* message, std::size_t size)
  {
    cmidi2_midi_conversion_context context{};
    cmidi2_midi_conversion_context_initialize(&context);

    uint32_t ump[65536 / 4];

    context.midi1 = const_cast<unsigned char*>(message);
    context.midi1_num_bytes = size;
    context.midi1_proceeded_bytes = 0;
    context.ump = ump;
    context.ump_num_bytes = sizeof(ump);
    context.ump_proceeded_bytes = 0;

    if (auto res = cmidi2_convert_midi1_to_ump(&context); res != CMIDI2_CONVERSION_RESULT_OK)
      return std::errc::invalid_argument;

    return send_ump(context.ump, context.ump_proceeded_bytes / 4);
  }
};
}

template <typename T, typename Arg>
std::unique_ptr<midi_out_api> make(libremidi::output_configuration&& conf, Arg&& arg)
{
  return std::make_unique<T>(std::move(conf), std::forward<Arg>(arg));
}
}
