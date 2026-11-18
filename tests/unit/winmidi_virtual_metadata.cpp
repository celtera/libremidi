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
    auto check_info = [&](const auto& info) {
      hstring id;
      bool valid;
      if constexpr (requires { info.ProductInstanceId(); })
      {
        id = info.ProductInstanceId();
        valid = info.Name() == to_hstring(name) && info.HasStaticFunctionBlocks()
                && info.DeclaredFunctionBlockCount() == 1;
      }
      else
      {
        id = info.ProductInstanceId;
        valid = info.Name == to_hstring(name) && info.HasStaticFunctionBlocks
                && info.DeclaredFunctionBlockCount == 1;
      }
      valid = valid && !id.empty() && id.size() <= 42 && previous != id
              && std::ranges::all_of(id, [](wchar_t c) { return c >= 32 && c <= 126; });
      previous = id;
      return valid;
    };
    if (!check_info(config.DeclaredEndpointInfo()) || config.FunctionBlocks().Size() != 1)
    {
      std::cerr << "Invalid product instance ID or declared function-block count\n";
      return 1;
    }
    auto block = config.FunctionBlocks().GetAt(0);
    if (block.Name() != to_hstring(name) || !block.IsActive() || block.Number() != 0
        || block.FirstGroup().Index() != 0 || block.GroupCount() != 1 || block.Direction() != direction)
    {
      std::cerr << "Classic port did not declare exactly group 0 in the requested direction\n";
      return 1;
    }
  }
  std::cout << "Two same-name ports: unique printable IDs and one declared group each\n";
}
catch (const hresult_error& error)
{
  std::cerr << to_string(error.message()) << '\n';
  return error.code() == REGDB_E_CLASSNOTREG ? 77 : 1;
}
