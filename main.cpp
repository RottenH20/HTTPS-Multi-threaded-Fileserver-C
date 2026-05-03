#include "server.h"
#include <iostream>
#include <string>

// Simple entry point: ask for optional port then start the server.
int main() {
    unsigned short port = 443;
    std::cout << "Enter port to bind (default 443): ";
    std::string input;
    if (std::getline(std::cin, input)) {
        try {
            if (!input.empty()) {
                int p = std::stoi(input);
                if (p > 0 && p <= 65535) port = static_cast<unsigned short>(p);
                else std::cout << "Invalid port; using default 443\n";
            }
        } catch (...) {
            std::cout << "Invalid input; using default 443\n";
        }
    }

    runServer(port);
    return 0;
}
