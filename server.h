#pragma once

// Start the HTTPS server and block until it exits.
// `port` is the TCP port to bind (default 443).
void runServer(unsigned short port = 443);
