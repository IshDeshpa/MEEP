#include "state/state.hpp"

#include <charconv>
#include <exception>
#include <iostream>
#include <string_view>

ETileLUT et_lut;

int main(int argc, char* argv[]){
  dim_t n = 100, m = 100, k = 100;

  for (int i = 1; i < argc; ++i) {
    std::string_view option = argv[i];
    if (option == "--help" || option == "-h") {
      std::cout << "Usage: " << argv[0]
                << " [-n N] [-m M] [-k K]\n"
                << "Aliases: -x for -n, -y for -m, -z for -k.\n"
                << "Dimensions must be positive integers; each defaults to 100.\n";
      return 0;
    }

    dim_t* dimension = nullptr;
    if (option == "-n" || option == "-x") {
      dimension = &n;
    } else if (option == "-m" || option == "-y") {
      dimension = &m;
    } else if (option == "-k" || option == "-z") {
      dimension = &k;
    } else {
      std::cerr << "Unknown option: " << option << '\n';
      return 1;
    }

    if (++i == argc) {
      std::cerr << "Missing value for " << option << '\n';
      return 1;
    }
    const std::string_view value = argv[i];
    dim_t parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || parsed == 0) {
      std::cerr << "Invalid dimension for " << option << ": " << value
                << " (expected a positive integer)\n";
      return 1;
    }
    *dimension = parsed;
  }

  try {
    WorldState world(n, m, k);

    



  } catch (const std::exception& error) {
    std::cerr << "Could not create world: " << error.what() << '\n';
    return 1;
  }
}
