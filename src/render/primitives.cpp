#include "render/primitives.hpp"
#include "extra/check.hpp"
#include <cuda_runtime.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// OBJ parser by Claude
bool readObj(const std::string &path, HostGeometryData &out){
  std::ifstream inf(std::filesystem::path(MEEP_SRC_DIR) / path);
  if(!inf) return false;

  std::vector<Vertex> &host_verts = out.verts;
  std::vector<Tri> &host_tris = out.tris;
  host_verts.clear();
  host_tris.clear();
  std::string line;
  while(std::getline(inf, line)){
    if(const std::size_t comment = line.find('#'); comment != std::string::npos)
      line.erase(comment);
    std::istringstream row(line);
    std::string tag;
    row >> tag;

    if(tag == "v"){
      Vertex v(0, 0, 0, 1);
      if(!(row >> v.x >> v.y >> v.z))
        throw std::runtime_error("Invalid OBJ vertex: " + line);
      // Optional w; OBJ defaults it to 1 when omitted.
      double w;
      if(row >> w) v.w = w;
      host_verts.push_back(v);
    } else if(tag == "f"){
      std::vector<unsigned int> face;
      std::string token;
      while(row >> token){
        // Only position indices are stored; ignore texture/normal indices.
        const std::string position = token.substr(0, token.find('/'));
        std::size_t end;
        const long long index = std::stoll(position, &end);
        const long long resolved = index > 0 ? index - 1
            : static_cast<long long>(host_verts.size()) + index;
        if(end != position.size() || index == 0 || resolved < 0 ||
           static_cast<std::size_t>(resolved) >= host_verts.size() ||
           static_cast<unsigned long long>(resolved) > std::numeric_limits<unsigned int>::max())
          throw std::runtime_error("Invalid OBJ face index: " + token);
        face.push_back(static_cast<unsigned int>(resolved));
      }
      if(face.size() < 3)
        throw std::runtime_error("OBJ face needs at least three vertices: " + line);
      // Fan triangulation for convex polygons, preserving winding.
      for(std::size_t i = 1; i + 1 < face.size(); ++i)
        host_tris.push_back({face[0], face[i], face[i + 1]});
    }
  }
  return !inf.bad();
}
Geometry::Geometry(const std::string &path){
  if(!load(path)) throw std::runtime_error("Failed to load geometry: " + path);
}

Geometry::~Geometry(){
  cudaFree(const_cast<Vertex *>(device_gd.verts)); // cudaFree(nullptr) is a no-op
  cudaFree(const_cast<Tri *>(device_gd.tris));
  cudaFree(const_cast<DeviceGeometryData *>(d_geom));
}

bool Geometry::load(const std::string &path){
  HostGeometryData parsed;
  if(!readObj(path, parsed)) return false;
  host_gd = std::move(parsed);
  return true;
}

// Writes the CPU-side copy as OBJ. Returns false if the file cannot be created or written.
bool Geometry::save(const std::string &path) const{
  const std::vector<Vertex> &host_verts = host_gd.verts;
  const std::vector<Tri> &host_tris = host_gd.tris;

  const std::filesystem::path output = std::filesystem::path(MEEP_SRC_DIR) / path;
  std::error_code ec;
  std::filesystem::create_directories(output.parent_path(), ec);
  if(ec) return false;
  std::ofstream outf(output);
  if(!outf) return false;

  outf << std::setprecision(std::numeric_limits<double>::max_digits10);
  for(const Vertex &v : host_verts)
    outf << "v " << v.x << ' ' << v.y << ' ' << v.z << ' ' << v.w << '\n';
  for(const Tri &t : host_tris)
    outf << "f " << static_cast<unsigned long long>(t.x) + 1 << ' '
         << static_cast<unsigned long long>(t.y) + 1 << ' '
         << static_cast<unsigned long long>(t.z) + 1 << '\n';

  outf.close();
  return !outf.fail();
}

VertRef Geometry::addVertex(Vertex v){
  const VertRef index = static_cast<VertRef>(host_gd.verts.size());
  host_gd.verts.push_back(v);
  return index;
}

void Geometry::addTriangle(Tri indices){
  host_gd.tris.push_back(indices);
}

void Geometry::addTriangle(Vertex a, Vertex b, Vertex c){
  const VertRef ia = addVertex(a);
  const VertRef ib = addVertex(b);
  const VertRef ic = addVertex(c);
  addTriangle(Tri{ia, ib, ic});
}

const DeviceGeometryData *Geometry::transfer(){
  const std::vector<Vertex> &host_verts = host_gd.verts;
  const std::vector<Tri> &host_tris = host_gd.tris;

  // Build the new buffers first, so the previous transfer stays valid if anything fails.
  const std::size_t vert_bytes = host_verts.size() * sizeof(Vertex);
  const std::size_t tri_bytes = host_tris.size() * sizeof(Tri);
  Vertex *d_verts = nullptr;
  Tri *d_tris = nullptr;
  if(vert_bytes){
    CUDA_CHECK(cudaMalloc(&d_verts, vert_bytes));
    CUDA_CHECK(cudaMemcpy(d_verts, host_verts.data(), vert_bytes, cudaMemcpyHostToDevice));
  }
  if(tri_bytes){
    CUDA_CHECK(cudaMalloc(&d_tris, tri_bytes));
    CUDA_CHECK(cudaMemcpy(d_tris, host_tris.data(), tri_bytes, cudaMemcpyHostToDevice));
  }

  DeviceGeometryData next;
  next.verts = d_verts;
  next.tris = d_tris;
  next.vertexCount = host_verts.size();
  next.triangleCount = host_tris.size();

  DeviceGeometryData *d_next = nullptr;
  CUDA_CHECK(cudaMalloc(&d_next, sizeof(DeviceGeometryData)));
  CUDA_CHECK(cudaMemcpy(d_next, &next, sizeof(DeviceGeometryData), cudaMemcpyHostToDevice));

  // Release the previous transfer.
  cudaFree(const_cast<Vertex *>(device_gd.verts));
  cudaFree(const_cast<Tri *>(device_gd.tris));
  cudaFree(const_cast<DeviceGeometryData *>(d_geom));

  device_gd = next;
  d_geom = d_next;
  return d_geom;
}
