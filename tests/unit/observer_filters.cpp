#include "../include_catch.hpp"

#include <libremidi/observer_configuration.hpp>

using libremidi::observer_configuration;
using libremidi::transport_type;

namespace
{
observer_configuration only(bool hw, bool sw, bool net, bool any = false)
{
  observer_configuration c;
  c.track_hardware = hw;
  c.track_virtual = sw;
  c.track_network = net;
  c.track_any = any;
  return c;
}
}

TEST_CASE("each flag admits its own group", "[observer]")
{
  const auto hw = only(true, false, false);
  CHECK(hw.accepts(transport_type::hardware));
  CHECK(!hw.accepts(transport_type::software));
  CHECK(!hw.accepts(transport_type::network));

  const auto sw = only(false, true, false);
  CHECK(!sw.accepts(transport_type::hardware));
  CHECK(sw.accepts(transport_type::software));
  CHECK(!sw.accepts(transport_type::network));

  const auto net = only(false, false, true);
  CHECK(!net.accepts(transport_type::hardware));
  CHECK(!net.accepts(transport_type::software));
  CHECK(net.accepts(transport_type::network));

  const auto none = only(false, false, false);
  CHECK(!none.accepts(transport_type::hardware));
  CHECK(!none.accepts(transport_type::software));
  CHECK(!none.accepts(transport_type::network));
}

TEST_CASE("transport_type is a bitmask and each bit answers for its group",
          "[observer]")
{
  // A USB interface is hardware|usb: the extra bit must not stop it matching.
  const auto hw = only(true, false, false);
  CHECK(hw.accepts(transport_type(transport_type::hardware | transport_type::usb)));
  CHECK(hw.accepts(transport_type(transport_type::hardware | transport_type::bluetooth)));
  CHECK(hw.accepts(transport_type(transport_type::hardware | transport_type::pci)));

  // `network` carries no companion bit of its own, so it stands alone.
  const auto net = only(false, false, true);
  CHECK(net.accepts(transport_type::network));

  const auto sw = only(false, true, false);
  CHECK(sw.accepts(transport_type(transport_type::software | transport_type::loopback)));
}

TEST_CASE("a port the backend could not classify is only taken by track_any",
          "[observer]")
{
  // `unknown` belongs to no group on purpose: a backend that cannot say what a
  // port is must not have that read as any particular answer.
  CHECK(!only(true, true, true).accepts(transport_type::unknown));
  CHECK(only(false, false, false, /*any=*/true).accepts(transport_type::unknown));
}

TEST_CASE("track_any admits everything, whatever the other flags say",
          "[observer]")
{
  const auto any = only(false, false, false, true);
  CHECK(any.accepts(transport_type::hardware));
  CHECK(any.accepts(transport_type::software));
  CHECK(any.accepts(transport_type::network));
  CHECK(any.accepts(transport_type::unknown));
}

TEST_CASE("the defaults lose nothing a backend used to report", "[observer]")
{
  // The flags exist to exclude. A default observer must still see every port
  // that was listed before they were interpreted uniformly, which is what
  // makes hardware and network both default-on.
  const observer_configuration def;
  CHECK(def.accepts(transport_type::hardware));
  CHECK(def.accepts(transport_type::network));
}
