#if defined(LIBREMIDI_PIPEWIRE)

  #if !defined(LIBREMIDI_HEADER_ONLY)
    #include <libremidi/backends/linux/pipewire/context.hpp>
  #endif

  #include <mutex>

namespace libremidi::pipewire
{

LIBREMIDI_INLINE
std::shared_ptr<context> shared_context(context::config cfg) noexcept
{
  static std::mutex mtx;
  static std::weak_ptr<context> weak;

  std::lock_guard lock{mtx};
  if (auto p = weak.lock())
  {
    if (p->ok())
      return context::make_shared_holder(std::move(p));

    // The cache holds a broken context for as long as any holder refers to it,
    // so it has to be revived here or dropped. Rebuilding in place keeps the
    // existing holders' subscriptions; this is noexcept, hence the catch.
    bool revived = false;
    try
    {
      revived = p->reconnect();
    }
    catch (...)
    {
      revived = false;
    }
    if (revived)
      return context::make_shared_holder(std::move(p));

    // reconnect() refuses on the loop thread and fails when the daemon is
    // gone; a new context still serves this caller better than a dead one.
    weak.reset();
  }

  auto inst = shared_instance();
  if (!inst)
    return {};

  auto ctx = context::make(std::move(inst), cfg);
  if (!ctx)
    return {};
  weak = ctx;
  return context::make_shared_holder(std::move(ctx));
}

}

#endif
