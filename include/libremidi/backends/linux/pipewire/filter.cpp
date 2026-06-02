#if defined(LIBREMIDI_PIPEWIRE)

  #if !defined(LIBREMIDI_HEADER_ONLY)
    #include <libremidi/backends/linux/pipewire/filter.hpp>
  #endif

  #include <string>

namespace libremidi::pipewire
{

// pw_core_create_object() is a 'static inline' (TU-local) pipewire symbol, so
// these wrappers are defined out-of-line: a non-inline function is emitted once
// and may legally name a TU-local entity, which a reachable inline function in a
// C++20 module may not.
LIBREMIDI_INLINE
pw_proxy* link_ports(context& ctx, std::uint32_t out_port, std::uint32_t in_port) noexcept
{
  if (!ctx.ok())
    return nullptr;
  auto& pw = load();
  pw_proxy* result = nullptr;
  ctx.with_lock([&] {
    auto* props = pw.properties_new(
        PW_KEY_LINK_OUTPUT_PORT, std::to_string(out_port).c_str(), PW_KEY_LINK_INPUT_PORT,
        std::to_string(in_port).c_str(), nullptr);
    if (!props)
      return;
    result = reinterpret_cast<pw_proxy*>(pw_core_create_object(
        ctx.pw_core_ptr(), "link-factory", PW_TYPE_INTERFACE_Link, PW_VERSION_LINK, &props->dict,
        0));
    pw.properties_free(props);
  });
  if (result)
    (void)ctx.synchronize();
  return result;
}

LIBREMIDI_INLINE
pw_proxy* link_nodes(context& ctx, std::uint32_t out_node, std::uint32_t in_node) noexcept
{
  if (!ctx.ok())
    return nullptr;
  auto& pw = load();
  pw_proxy* result = nullptr;
  ctx.with_lock([&] {
    auto* props = pw.properties_new(
        PW_KEY_LINK_OUTPUT_NODE, std::to_string(out_node).c_str(), PW_KEY_LINK_INPUT_NODE,
        std::to_string(in_node).c_str(), nullptr);
    if (!props)
      return;
    result = reinterpret_cast<pw_proxy*>(pw_core_create_object(
        ctx.pw_core_ptr(), "link-factory", PW_TYPE_INTERFACE_Link, PW_VERSION_LINK, &props->dict,
        0));
    pw.properties_free(props);
  });
  if (result)
    (void)ctx.synchronize();
  return result;
}

}

#endif
