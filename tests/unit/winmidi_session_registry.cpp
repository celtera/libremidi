// How Windows MIDI Services sessions are shared and replaced, without creating one.
#include <libremidi/backends/winmidi/session.hpp>

#include <cstdio>
#include <stdexcept>

namespace w = libremidi::winmidi;

static void require(bool pass, const char* what)
{
  if (!pass) throw std::runtime_error{what};
}

struct session
{
  std::string name;
  bool open{true};
  bool connects{true};
};
struct busy { };

int main() try
{
  w::session_registry<session> all;
  int made{};
  const auto make = [&](const std::string& name) {
    ++made;
    return std::make_shared<session>(name);
  };
  const auto usable = [](const session& s) { return s.open; };
  const auto connect = [](const session& s) {
    if (!s.connects) throw std::runtime_error{"no connection"};
    return &s;
  };

  // One session for the process, one per name, as long as someone uses it.
  auto host = all.get({}, make, usable);
  require(all.get({}, make, usable) == host && made == 1, "process session not shared");
  auto named = all.get("a", make, usable);
  require(all.get("a", make, usable) == named && named != host && made == 2, "named session not shared");
  require(all.get("b", make, usable) != named && made == 3, "names share a session");
  std::weak_ptr<session> released = named;
  named.reset();
  require(released.expired(), "a named session outlived its users");
  require(all.get("a", make, usable) != nullptr && made == 4, "released name not created again");

  // A closed session is not handed out again.
  host->open = false;
  auto reopened = all.get({}, make, usable);
  require(reopened != host && reopened->open && made == 5, "closed session shared");

  // A working session is used as is.
  std::shared_ptr<session> current;
  require(all.use<busy>(current, {}, make, usable, connect) == reopened.get() && made == 5,
          "working session replaced");

  // A session that cannot connect any more is replaced by one that can.
  reopened->connects = false;
  const auto* connected = all.use<busy>(current, {}, make, usable, connect);
  require(made == 6 && connected == current.get() && current != reopened, "stale session kept");
  require(all.get({}, make, usable) == current && made == 6, "replacement not shared");

  // When a new session fails as well, the shared one stays and the first error is reported.
  const auto refuse = [](const session& s) -> const session* {
    throw std::runtime_error{s.name + "#" + std::to_string(s.connects)};
  };
  auto before = current;
  before->connects = false;
  try
  {
    all.use<busy>(current, {}, make, usable, refuse);
    require(false, "failure swallowed");
  }
  catch (const std::runtime_error& e)
  {
    require(std::string{e.what()} == "#0", "the error of the retry replaced the first one");
  }
  require(made == 7 && all.get({}, make, usable) == before, "a failing session replaced the shared one");

  // A busy endpoint says nothing about the session.
  try
  {
    all.use<busy>(current, {}, make, usable, [](const session&) -> const session* { throw busy{}; });
    require(false, "busy swallowed");
  }
  catch (const busy&) { }
  require(made == 7, "a busy endpoint created a session");

  std::puts("PASS: shared process and named sessions; closed and stale sessions replaced; "
            "failures and busy endpoints keep the shared session");
  return 0;
}
catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
