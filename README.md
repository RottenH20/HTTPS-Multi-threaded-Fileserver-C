# Simple C++ Web Server

A minimal multithreaded HTTP server for Windows. This project demonstrates serving static HTML, simple username/password authentication, per-user file directories for learning and experimentation, and some fun API usage.

---

## Quick Start

### Prerequisites

- Windows (tested on Windows 10/11)
- A C++17-capable compiler (MSVC or MinGW/g++)
- CMake (optional, recommended)
- OpenSSL for HTTPS / TLS support
- Festivo API key (for holiday/calendar features)

---

## Build with g++

g++ -std=c++17 main.cpp server.cpp pages_login.cpp pages_dashboard.cpp pages_holiday.cpp -o server.exe -I"C:\Program Files\OpenSSL-Win64\include" "C:\Program Files\OpenSSL-Win64\lib\VC\x64\MD\libssl.lib" "C:\Program Files\OpenSSL-Win64\lib\VC\x64\MD\libcrypto.lib" -lws2_32

---

## Run the Server

.\server.exe

Then open your browser:

https://localhost:443

(Default port: 443)

Note: On Windows, you may need to run the terminal as Administrator to bind to low-numbered ports or allow firewall access.

---

# Festivo API Setup (Holiday / Calendar Features)

This project uses the Festivo API to retrieve holiday and calendar date information.

## Getting an API Key

1. Visit the Festivo website.
2. Create a free account or sign in.
3. Navigate to the developer / API dashboard.
4. Generate a new API key.
5. Copy the key for use in this project.

---

## Testing API Connection

This project includes:

test_holiday_api.py

A small Python script used to verify that your Festivo API key works and that the server can successfully retrieve holiday data.

### Run the Test

python test_holiday_api.py

### Expected Result

- Successful API response
- Holiday JSON data returned
- Confirms internet/API connectivity

If it fails:

- Check API key
- Check internet connection
- Check Festivo service status

---

# Project Structure

main.cpp                Program entry point
server.cpp              Server implementation
server.h                Server declarations
pages_login.cpp         Login page handling
pages_dashboard.cpp     Dashboard page handling
pages_holiday.cpp       Holiday API page
login.html              Example login page
dashboard.html          Example protected page
users.txt               Username/password file
user_data/              Per-user directories
test_holiday_api.py     API connection tester

---

# Configuration

- Default listening port: 443
- Change port by running server and entering a new port when prompted.

---

# User Management

To add a user:

1. Add a line to users.txt

username:password

2. Place files for that user inside the folder.

---

# Security Notice

This project is for educational purposes only.

It does not include:

- Secure password hashing
- Production-grade TLS setup
- Input sanitization
- Session security
- Rate limiting
- Advanced authentication protections

Do not deploy publicly or use with sensitive data.

---

# Troubleshooting

ERR_CONNECTION_REFUSED

- Ensure server is running
- Check firewall
- Run as Administrator
- Ensure using HTTP vs HTTPS correctly

cmake is not recognized

Install CMake or add it to PATH.

Port Already in Use

Use a different port.

API Not Working

- Verify Festivo API key
- Run test_holiday_api.py
- Check internet connection

---

# Stopping the Server

Ctrl + C

The server installs a console handler and closes the listening socket cleanly.

---

# Notes

- Educational implementation
- Great beginner networking project
- Expandable into HTTPS, sessions, databases, REST APIs, etc.
