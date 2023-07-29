#pragma once
#include <libremidi/api.hpp>
#include <libremidi/config.hpp>

#include <any>

namespace libremidi
{

class shared_context
{
public:
  shared_context() = default;
  virtual ~shared_context() = default;
  shared_context(const shared_context&) = delete;
  shared_context(shared_context&&) = delete;
  shared_context& operator=(const shared_context&) = delete;
  shared_context& operator=(shared_context&&) = delete;

  virtual void start_processing() = 0;
  virtual void stop_processing() = 0;
};

struct shared_configurations
{
  std::shared_ptr<shared_context> context;
  std::any observer, in, out;
};

LIBREMIDI_EXPORT
shared_configurations create_shared_context(libremidi::API api, std::string_view client_name);

}
