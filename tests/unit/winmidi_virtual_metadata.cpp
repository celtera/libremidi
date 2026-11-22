#include <libremidi/config.hpp>
#include <libremidi/backends/winmidi/virtual_port.hpp>

#include <iostream>

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
  std::cout << "Two same-name ports: unique service-valid IDs and one declared group each; "
               "configured ID kept\n";
}
catch (const hresult_error& error)
{
  std::cerr << to_string(error.message()) << '\n';
  return error.code() == REGDB_E_CLASSNOTREG ? 77 : 1;
}
