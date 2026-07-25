// Minimal stand-in: the oracle host never opens a socket, but timer.hpp
// declares a member function taking sockpp::tcp_socket by value.
#pragma once
namespace sockpp { class tcp_socket {}; }
