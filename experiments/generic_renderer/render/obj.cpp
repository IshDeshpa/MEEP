#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <obj.h>

Mesh::Mesh(const std::string &path){
  load(path);
}

void Mesh::load(const std::string &path){
  std::ifstream f(path);
  if(!f) {
    std::cout << "ERROR LOADING OBJ " << path << std::endl;
    return;
  }
  load(f);
}

void Mesh::load(std::istream &is){
  verts.clear();
  tris.clear();
  colors.clear();
  name.clear();

  std::string line;
  while(std::getline(is, line)){
    std::istringstream ss(line);
    std::string tag;
    ss >> tag;

    if(tag == "v"){
      // add vertex
      dvec4 vert;
      ss >> vert.x >> vert.y >> vert.z;
      vert.w = 1.0;
      verts.push_back(vert);
    } else if(tag == "f"){
      // add tri
      Triangle t;
      ss >> t.x >> t.y >> t.z;
      t.x--; t.y--; t.z--; // objs are 1-indexed 
      tris.push_back(t);
    } else if(tag == "o"){
      // name
      ss >> name;
    }
  }
}

// Reads OBJ syntax from the stream, replacing the current contents
std::istream& operator>>(std::istream& is, Mesh& o){
  o.load(is);
  is.clear(is.rdstate() & ~std::ios::failbit); // getline hitting EOF sets failbit; the read itself succeeded
  return is;
}

// Writes the mesh back out in OBJ syntax (1-indexed faces)
std::ostream& operator<<(std::ostream& os, const Mesh& o){
  if(!o.name.empty()) os << "o " << o.name << '\n';
  for(const dvec4 &v : o.verts) os << "v " << v.x << ' ' << v.y << ' ' << v.z << '\n';
  for(const Triangle &t : o.tris) os << "f " << t.x + 1 << ' ' << t.y + 1 << ' ' << t.z + 1 << '\n';
  return os;
}

dvec4 Mesh::getCentroid(){
  dvec4 c(0);
  for(auto &v : verts){
    c += v;
  }
  c /= verts.size();
  c.w = 1.0;

  return c;
}

Mesh Mesh::operator+(const Mesh& b) const{
  const Vert4Array &av = this->verts;
  const Vert4Array &bv = b.verts;
  const TArray &at = this->tris;
  const TArray &bt = b.tris;

  Mesh c;
  Vert4Array &cv = c.verts;
  TArray &ct = c.tris;
  std::vector<Color> &cc = c.colors;

  cv.reserve(av.size() + bv.size());
  ct.reserve(at.size() + bt.size());
  cc.reserve(at.size() + bt.size());
  
  // concat verts
  cv.insert(cv.end(), av.begin(), av.end());
  cv.insert(cv.end(), bv.begin(), bv.end());

  // concat tris
  ct.insert(ct.end(), at.begin(), at.end());
  unsigned int ofs = av.size(); // b's indices now sit after a's verts
  for(const Triangle &t : bt){
    ct.push_back(t + ofs);
  }

  // concat colors, one per triangle, same order as the tris
  cc.insert(cc.end(), this->colors.begin(), this->colors.end());
  cc.insert(cc.end(), b.colors.begin(), b.colors.end());

  return c;
}

Mesh Mesh::transformed(const dmat4x4 &m) const{
  Mesh out(*this);
  for(dvec4 &v : out.verts) v = m * v;
  return out;
}

Mesh Obj::toWorld() const{
  const dmat4x4 model = glm::translate(dmat4x4(1.0), dvec3(pose.pos)) * mat4_cast(pose.lookQuat());
  return mesh.transformed(model);
}
