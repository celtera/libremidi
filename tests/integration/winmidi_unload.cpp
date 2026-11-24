// Unloading a module that embeds libremidi must not end the host process:
// COM usage cannot be released, nor a session closed, from DllMain.
#include <windows.h>

#include <cstdio>

int main(int argc, char** argv)
{
  if (argc != 2)
    return 2;
  for (int cycle = 0; cycle < 3; ++cycle)
  {
    const HMODULE plugin = LoadLibraryA(argv[1]);
    if (!plugin)
    {
      std::fprintf(stderr, "could not load %s: %lu\n", argv[1], GetLastError());
      return 1;
    }
    const auto run = reinterpret_cast<int (*)()>(
        reinterpret_cast<void (*)()>(GetProcAddress(plugin, "libremidi_plugin_run")));
    if (!run || run() != 0)
    {
      std::fprintf(stderr, "plug-in failed in cycle %d\n", cycle);
      return 1;
    }
    if (!FreeLibrary(plugin) || GetModuleHandleA(argv[1]))
    {
      std::fprintf(stderr, "plug-in not unloaded in cycle %d\n", cycle);
      return 1;
    }
  }
  std::puts("PASS: three load/unload cycles with libremidi objects left to static destruction");
  return 0;
}
