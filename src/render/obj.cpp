#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <obj.h>

void Obj::load(const std::string &path){
  std::ifstream f(path);
  if(!f) {
    std::cout << "ERROR LOADING OBJ " << path << std::endl;
    return;
  }
  load(f);
}

void Obj::load(std::istream &is){
  verts.clear();
  tris.clear();
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
std::istream& operator>>(std::istream& is, Obj& o){
  o.load(is);
  is.clear(is.rdstate() & ~std::ios::failbit); // getline hitting EOF sets failbit; the read itself succeeded
  return is;
}

// Writes the mesh back out in OBJ syntax (1-indexed faces)
std::ostream& operator<<(std::ostream& os, const Obj& o){
  if(!o.name.empty()) os << "o " << o.name << '\n';
  for(const dvec4 &v : o.verts) os << "v " << v.x << ' ' << v.y << ' ' << v.z << '\n';
  for(const Triangle &t : o.tris) os << "f " << t.x + 1 << ' ' << t.y + 1 << ' ' << t.z + 1 << '\n';
  return os;
}
