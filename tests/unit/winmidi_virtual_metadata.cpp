#include <libremidi/config.hpp>
#include <libremidi/backends/winmidi/virtual_port.hpp>

#include <iostream>
#include <string>
#include <string_view>

using namespace libremidi::winmidi;

int main()
try
{
  init_apartment();
  // Same display name for two instances, not representable as a MIDI product ID.
  const std::string name = std::string(50, 'x') + "\xc3\xa9";
  hstring previous;
  for (auto direction : {MidiFunctionBlockDirection::BlockInput, MidiFunctionBlockDirection::BlockOutput})
  {
    auto config = setup_virtualdevice_config("", name, direction);
    auto info = config.DeclaredEndpointInfo();
    auto id = info.ProductInstanceId();
    if (info.Name() != to_hstring(name) || !info.HasStaticFunctionBlocks()
        || info.DeclaredFunctionBlockCount() != 1 || config.FunctionBlocks().Size() != 1
        || id.size() != virtual_endpoint_id_max_length || previous == id
        || !valid_virtual_endpoint_id(to_string(id)))
    {
      std::cerr << "Invalid product instance ID or declared function-block count\n";
      return 1;
    }
    previous = id;
    auto block = config.FunctionBlocks().GetAt(0);
    if (block.Name() != to_hstring(name) || !block.IsActive() || block.Number() != 0
        || block.FirstGroup().Index() != 0 || block.GroupCount() != 1 || block.Direction() != direction)
    {
      std::cerr << "Classic port did not declare exactly group 0 in the requested direction\n";
      return 1;
    }
  }

  // A configured identifier is announced as the service stores it.
  static_assert(valid_virtual_endpoint_id("My_Synth-01"));
  static_assert(valid_virtual_endpoint_id(std::string_view{"0123456789abcdef0123456789abcdef"}));
  static_assert(!valid_virtual_endpoint_id(""));
  static_assert(!valid_virtual_endpoint_id(std::string_view{"0123456789abcdef0123456789abcdef0"}));
  static_assert(!valid_virtual_endpoint_id("my synth"));
  static_assert(!valid_virtual_endpoint_id("synth\xc3\xa9"));
  static_assert(!valid_virtual_endpoint_id(std::string_view{"a\0b", 3}));
  for (int run = 0; run < 2; ++run)
  {
    auto config = setup_virtualdevice_config(
        "", name, MidiFunctionBlockDirection::BlockInput, "My_Synth-01");
    if (config.DeclaredEndpointInfo().ProductInstanceId() != L"my_synth-01")
    {
      std::cerr << "Configured endpoint identifier was not kept\n";
      return 1;
    }
  }

  // The endpoint, its device entry and its function block announce one name.
  const std::string non_bmp = [] {
    std::string s;
    for (int i = 0; i < 30; ++i)
      s += "\xf0\x9f\x8e\xb9";
    return s;
  }();
  const struct
  {
    std::string requested;
    std::wstring expected;
  } names[] = {
      {"  padded \t\r\n", L"padded"},
      {std::string{"ab\0cd", 5}, L"ab"},
      {std::string(1024, 'n'), std::wstring(91, L'n')},
      {std::string(99, 'n'), std::wstring(91, L'n')},
      {std::string(90, 'n') + "\xc3\xa9", std::wstring(90, L'n')},
      {std::string(90, 'n') + "  tail", std::wstring(90, L'n')},
      {non_bmp, std::wstring{to_hstring(non_bmp)}.substr(0, 2 * (91 / 4))},
      {"\xff\xfe\x80"
       "abc",
       L"\xfffd\xfffd\xfffd"
       L"abc"},
  };
  for (const auto& [requested, expected] : names)
  {
    const auto config = setup_virtualdevice_config(
        "", requested, MidiFunctionBlockDirection::BlockOutput);
    const auto block = config.FunctionBlocks().GetAt(0);
    if (config.Name() != expected || config.DeclaredEndpointInfo().Name() != expected
        || block.Name() != expected || to_string(block.Name()).size() > virtual_port_name_max_bytes
        || block.UIHint() != MidiFunctionBlockUIHint::Sender)
    {
      std::cerr << "Names differ or exceed the function block limit for a "
                << requested.size() << "-byte request\n";
      return 1;
    }
  }
  if (setup_virtualdevice_config("", "x", MidiFunctionBlockDirection::BlockInput)
          .FunctionBlocks().GetAt(0).UIHint() != MidiFunctionBlockUIHint::Receiver)
  {
    std::cerr << "An input port is not announced as a receiver\n";
    return 1;
  }

  for (std::string_view unnamed : {std::string_view{}, std::string_view{" \t\r\n"},
                                   std::string_view{"\0name", 5}})
  {
    bool rejected = virtual_port_name(unnamed).empty();
    try
    {
      setup_virtualdevice_config("", unnamed, MidiFunctionBlockDirection::BlockInput);
      rejected = false;
    }
    catch (const hresult_invalid_argument&)
    {
    }
    // Before any session is used: open reports it without reaching the service.
    virtual_port port;
    if (!rejected
        || port.open(nullptr, "", unnamed, "", MidiFunctionBlockDirection::BlockInput)
               != stdx::error{std::errc::invalid_argument})
    {
      std::cerr << "A port without a name was not rejected\n";
      return 1;
    }
  }
  std::cout << "Two same-name ports: unique service-valid IDs and one declared group each; "
               "configured ID kept; one normalised name per port\n";
}
catch (const hresult_error& error)
{
  std::cerr << to_string(error.message()) << '\n';
  return error.code() == REGDB_E_CLASSNOTREG ? 77 : 1;
}
