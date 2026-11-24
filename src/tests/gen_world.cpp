#include "state/state.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

// The default camera starts at the origin looking down positive Z. Keep this
// compact voxel sphere at the origin so it is immediately in view.
constexpr std::size_t WORLD_N = 5;
constexpr std::size_t WORLD_M = 5;
constexpr std::size_t WORLD_K = 5;
constexpr int SPHERE_RADIUS = 2;

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

bool parseElement(std::string value, Element& element, std::string_view& element_name) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  for (const ElementName& candidate : ELEMENTS) {
    if (value == candidate.name) {
      element = candidate.element;
      element_name = candidate.name;
      return true;
    }
  }
  return false;
}

void printUsage(const char* program) {
  std::cerr << "Usage: " << program
            << " <air|water|earth|lava|block|emitter|void> [output-filename]\n"
            << "Writes a 5x5x5 voxel sphere of the selected element near the origin.\n"
            << "Writes into src/worlds/; default filename: world_<element>_sphere_5x5x5.bin\n";
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 2 || argc > 3) {
    printUsage(argv[0]);
    return 1;
  }

  Element element{};
  std::string_view element_name;
  if (!parseElement(argv[1], element, element_name)) {
    std::cerr << "Unknown element: " << argv[1] << '\n';
    printUsage(argv[0]);
    return 1;
  }

  const std::filesystem::path output_name = argc == 3 ? argv[2]
      : "world_" + std::string(element_name) + "_sphere_" + std::to_string(WORLD_N) +
            "x" + std::to_string(WORLD_M) + "x" + std::to_string(WORLD_K) + ".bin";
  if (output_name.empty() || output_name.has_parent_path() ||
      output_name == "." || output_name == "..") {
    std::cerr << "Output must be a filename without directories; worlds are saved in src/worlds/\n";
    return 1;
  }
  const std::filesystem::path worlds_dir = std::filesystem::path(MEEP_SRC_DIR) / "worlds";
  std::error_code directory_error;
  std::filesystem::create_directories(worlds_dir, directory_error);
  if (directory_error) {
    std::cerr << "Could not create " << worlds_dir << ": " << directory_error.message() << '\n';
    return 1;
  }
  const auto output_path = worlds_dir / output_name;
  std::vector<Cell> cells(WORLD_N * WORLD_M * WORLD_K, Cell{Element::AIR, 0});
  const int center_x = static_cast<int>(WORLD_N / 2);
  const int center_y = static_cast<int>(WORLD_M / 2);
  const int center_z = static_cast<int>(WORLD_K / 2);

  for (std::size_t x = 0; x < WORLD_N; ++x) {
    for (std::size_t y = 0; y < WORLD_M; ++y) {
      for (std::size_t z = 0; z < WORLD_K; ++z) {
        const int dx = static_cast<int>(x) - center_x;
        const int dy = static_cast<int>(y) - center_y;
        const int dz = static_cast<int>(z) - center_z;
        if (dx * dx + dy * dy + dz * dz <= SPHERE_RADIUS * SPHERE_RADIUS) {
          cells[z + WORLD_K * (y + WORLD_M * x)] = Cell{element, 0};
        }
      }
    }
  }

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
            << 'x' << WORLD_K << " with a voxel sphere of " << element_name << "\n";
  return 0;
}
