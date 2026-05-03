#pragma once
#include <string>

// Returns the HTML content for the login page.
// Keeping page HTML in a single function makes it easy to test and maintain.
std::string getLoginPage();
