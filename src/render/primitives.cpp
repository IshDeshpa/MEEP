#include "render/primitives.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

// Claude wrote this
void Geometry::load(const std::string &path){
  std::ifstream inf(std::filesystem::path(MEEP_SRC_DIR) / path);
  if(!inf) throw std::runtime_error("Failed to open OBJ: " + path);

  Geometry loaded;
  std::string line;
  while(std::getline(inf, line)){
    if(const auto comment = line.find('#'); comment != std::string::npos)
      line.erase(comment);
    std::istringstream row(line);
    std::string tag;
    row >> tag;

    if(tag == "v"){
      Vertex v(0, 0, 0);
      if(!(row >> v.x >> v.y >> v.z))
        throw std::runtime_error("Invalid OBJ vertex: " + line);
      loaded.verts.push_back(v);
    } else if(tag == "f"){
      std::vector<unsigned int> face;
      std::string token;
      while(row >> token){
        // Only position indices are stored; ignore texture/normal indices.
        const auto position = token.substr(0, token.find('/'));
        std::size_t end;
        const auto index = std::stoll(position, &end);
        const auto resolved = index > 0 ? index - 1
            : static_cast<long long>(loaded.verts.size()) + index;
        if(end != position.size() || index == 0 || resolved < 0 ||
           static_cast<std::size_t>(resolved) >= loaded.verts.size() ||
           static_cast<unsigned long long>(resolved) > std::numeric_limits<unsigned int>::max())
          throw std::runtime_error("Invalid OBJ face index: " + token);
        face.push_back(static_cast<unsigned int>(resolved));
      }
      if(face.size() < 3)
        throw std::runtime_error("OBJ face needs at least three vertices: " + line);
      // Fan triangulation for convex polygons, preserving winding.
      for(std::size_t i = 1; i + 1 < face.size(); ++i)
        loaded.tris.push_back({face[0], face[i], face[i + 1]});
    }
  }
  if(inf.bad()) throw std::runtime_error("Failed to read OBJ: " + path);
  verts.swap(loaded.verts);
  tris.swap(loaded.tris);
}

// Claude wrote this
void Geometry::save(const std::string &path){
  const auto output = std::filesystem::path(MEEP_SRC_DIR) / path;
  std::filesystem::create_directories(output.parent_path());
  std::ofstream outf(output);
  if(!outf) throw std::runtime_error("Failed to open OBJ: " + path);

  outf << std::setprecision(std::numeric_limits<double>::max_digits10);
  for(const auto &v : verts)
    outf << "v " << v.x << ' ' << v.y << ' ' << v.z << '\n';
  for(const auto &t : tris)
    outf << "f " << static_cast<unsigned long long>(t.x) + 1 << ' '
         << static_cast<unsigned long long>(t.y) + 1 << ' '
         << static_cast<unsigned long long>(t.z) + 1 << '\n';

  outf.close();
  if(!outf) throw std::runtime_error("Failed to write OBJ: " + path);
}

void Geometry::print(){
  for(Tri &t : this->tris){
    std::printf("(%0.2f %0.2f %0.2f) - (%0.2f %0.2f %0.2f) - (%0.2f %0.2f %0.2f)\n", 
                this->verts[t.x].x, this->verts[t.x].y, this->verts[t.x].z,
                this->verts[t.y].x, this->verts[t.y].y, this->verts[t.y].z,
                this->verts[t.z].x, this->verts[t.z].y, this->verts[t.z].z);
  }
}
