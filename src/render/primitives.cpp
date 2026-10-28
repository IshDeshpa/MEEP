#include "render/primitives.hpp"
#include <fstream>
#include <stdexcept>
#include <type_traits>

template <typename T>
static inline void saveVector(std::vector<T> &v, std::ofstream &outf){
  static_assert(std::is_trivially_copyable_v<T>); // can this be safely copied as raw bytes?
  
  const std::size_t len = v.size();
  outf.write(reinterpret_cast<const char *>(&len), sizeof(len));

  if(!v.empty()){
    outf.write(
      reinterpret_cast<const char *>(v.data()),
      v.size() * sizeof(T)
    );
  }
}

template <typename T>
static inline void loadVector(std::vector<T> &v, std::ifstream &inf){
  static_assert(std::is_trivially_copyable_v<T>);

  v.clear();

  std::size_t len;
  inf.read(reinterpret_cast<char *>(&len), sizeof(len)); // read vector length
  
  if(len > 0){
    v.resize(len);
    inf.read(reinterpret_cast<char *>(v.data()), len * sizeof(T));
  }
}

void Geometry::load(std::string &path){
  std::ifstream inf(path, std::ios::binary);

  if(!inf) throw std::runtime_error("Failed to open file");

  loadVector(this->verts, inf);
  loadVector(this->tris, inf);
  
  inf.close();
  if(!inf) throw std::runtime_error("Failed to close file");
}

void Geometry::save(std::string &path){
  std::ofstream outf(path, std::ios::binary);

  if(!outf) throw std::runtime_error("Failed to open file");

  saveVector(this->verts, outf); // save the verts!
  saveVector(this->tris, outf); // save the tris!

  outf.close();
  if(!outf) throw std::runtime_error("Failed to close file");
}
