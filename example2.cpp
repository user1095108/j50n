#include "j50n.hpp"
#include <iostream>
#include <string_view>

static constexpr std::string_view kConfig = R"({
  "server": {
    "host": "0.0.0.0",
    "port": 8080,
    "tls": {
      "enabled": true,
      "cert": "/etc/ssl/server.crt"
    }
  },
  "limits": {
    "max_connections": 1000,
    "timeout_ms": 30.5
  },
  "features": ["logging", "metrics", "compression"]
})";

int main()
{
  j50n cfg(kConfig.data(), kConfig.size());

  auto [port, port_err] = cfg.get<unsigned>("server", "port");
  if (port_err) {
    std::cerr << "Config missing port, using default 3000\n";
    port = 3000;
  }

  std::string_view host = cfg.get("server", "host");
  if (host.empty()) host = "localhost";

  auto [tls, _] = cfg.get<bool>("server", "tls", "enabled");

  auto [timeout, terr] = cfg.get<double>("limits", "timeout_ms");
  if (terr)
    timeout = 10.0; // seconds

  std::cout << "Enabled features: ";
  cfg["features"].feach([](j50n const& feat, std::size_t const i) {
    std::cout << (i ? ", " : "") << feat; // feat auto-converts to string_view
  });
  std::cout << "\n";

  std::cout << "Starting server on " << host << ":" << port 
            << " (TLS: " << (tls ? "on" : "off") 
            << ", timeout: " << timeout << "s)\n";

  return 0;
}
