#include "state/state.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t WORLD_N = 100;
constexpr std::size_t WORLD_M = 1;
constexpr std::size_t WORLD_K = 100;

struct ElementName {
  std::string_view name;
  Element element;
};

constexpr std::array ELEMENTS{
    ElementName{"air", Element::AIR},
    ElementName{"water", Element::WATER},
    ElementName{"earth", Element::EARTH},
    ElementName{"lava", Element::LAVA},
    ElementName{"block", Element::BLOCK},
    ElementName{"emitter", Element::EMITTER},
    ElementName{"void", Element::VOID},
};

bool parseElement(std::string value, Element& element) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  for (const ElementName& candidate : ELEMENTS) {
    if (value == candidate.name) {
      element = candidate.element;
      return true;
    }
  }
  return false;
}

void printUsage(const char* program) {
  std::cerr << "Usage: " << program
            << " <air|water|earth|lava|block|emitter|void> [output-file]\n"
            << "Writes a 100x1x100 world filled with the selected element.\n";
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 2 || argc > 3) {
    printUsage(argv[0]);
    return 1;
  }

  Element element{};
  if (!parseElement(argv[1], element)) {
    std::cerr << "Unknown element: " << argv[1] << '\n';
    printUsage(argv[0]);
    return 1;
  }

  const char* output_path = argc == 3 ? argv[2] : "world.bin";
  std::vector<Cell> cells(WORLD_N * WORLD_M * WORLD_K, Cell{element, 0});

  std::ofstream output(output_path, std::ios::binary);
  if (!output) {
    std::cerr << "Could not open " << output_path << " for writing\n";
    return 1;
  }

  output.write(reinterpret_cast<const char*>(&WORLD_N), sizeof(WORLD_N));
  output.write(reinterpret_cast<const char*>(&WORLD_M), sizeof(WORLD_M));
  output.write(reinterpret_cast<const char*>(&WORLD_K), sizeof(WORLD_K));
  output.write(reinterpret_cast<const char*>(cells.data()),
               static_cast<std::streamsize>(cells.size() * sizeof(Cell)));
  if (!output) {
    std::cerr << "Failed while writing " << output_path << '\n';
    return 1;
  }

  std::cout << "Wrote " << output_path << ": " << WORLD_N << 'x' << WORLD_M
            << 'x' << WORLD_K << " filled with " << argv[1] << " cells\n";
  return 0;
}
