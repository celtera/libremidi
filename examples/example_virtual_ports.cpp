#include <libremidi/libremidi.hpp>
#include "utils.hpp"
#include <iostream>
#include <chrono>
#include <thread>

int main()
{
    try {
        auto in_conf = libremidi::midi1::in_default_configuration();
        auto out_conf = libremidi::midi1::out_default_configuration();

        libremidi::midi_in midi_in{
            {
                .on_message = [](const libremidi::message& message) {
                    std::cout << "Received: " << message << std::endl;
                }
            },
            in_conf
        };

        libremidi::midi_out midi_out{
            libremidi::output_configuration{},
            out_conf
        };

        midi_in.open_virtual_port("my in");
        midi_out.open_virtual_port("my out");

        std::cout << "MIDI virtual ports created:\n";
        std::cout << "Client name: my app\n";
        std::cout << "Input port: my in\n"; 
        std::cout << "Output port: my out\n";
        std::cout << "Press Ctrl+C to exit...\n";

        libremidi::message test_note{0x90, 60, 127};
        midi_out.send_message(test_note);
        std::cout << "Sent test note: " << test_note << std::endl;

        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
