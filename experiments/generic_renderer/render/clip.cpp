#include "camera.h"
#include <bit>
#include <cassert>

enum TriClipNear{
  TRI_CLIP_INSIDE = 0,
  TRI_CLIP_PARTIAL = 1,
  TRI_CLIP_OUTSIDE = 2
};

// signed distance to the near plane (negative = clipped)
static double nearDist(const dvec4 &v){
  return v.z + v.w;
}

// flag every vertex that is clipped by the near plane
static std::vector<uint8_t> findClippedVerts(const Vert4Array &verts){
  std::vector<uint8_t> z_clip(verts.size());
  for(unsigned int i=0; i<verts.size(); i++){
    z_clip[i] = (verts[i].z < -verts[i].w) & 0x1;
  }
  return z_clip;
}

// classify each triangle by how many of its vertices are clipped
static std::vector<TriClipNear> classifyTriangles(const TArray &tris, const std::vector<uint8_t> &z_clip){
  std::vector<TriClipNear> tri_clip(tris.size());
  for(unsigned int i=0; i<tris.size(); i++){
    const Triangle &t = tris[i];
    tri_clip[i] = (TriClipNear)std::popcount((uint8_t)(z_clip[t.x] + z_clip[t.y] + z_clip[t.z]));
  }
  return tri_clip;
}

// append a vertex and return its index
static unsigned int pushVert(Vert4Array &verts, const dvec4 &v){
  verts.push_back(v);
  return verts.size() - 1;
}

// one vertex clipped: the triangle becomes a quad, i.e. two triangles
static void clipOneVertex(Vert4Array &verts, const Triangle &t, const std::vector<uint8_t> &z_clip, TArray &out_tris){
  const unsigned int idx[3] = {t.x, t.y, t.z};

  // find the two unclipped verts in the triangle's original winding order,
  // starting after c, so the new triangles keep the same orientation
  const int ci = z_clip[idx[0]] ? 0 : z_clip[idx[1]] ? 1 : 2;
  const unsigned int ia = idx[(ci+1)%3];
  const unsigned int ib = idx[(ci+2)%3];
  const dvec4 c = verts[idx[ci]];
  const dvec4 a = verts[ia];
  const dvec4 b = verts[ib];

  const double d  = nearDist(c);
  const double da = nearDist(a);
  const double db = nearDist(b);

  // fraction of the way from the unclipped vert toward c where d == 0
  const double ta = da / (da - d);
  const double tb = db / (db - d);

  // two new verts, on edges c-a and c-b
  const unsigned int inda = pushVert(verts, a + ta * (c - a));
  const unsigned int indb = pushVert(verts, b + tb * (c - b));

  out_tris.push_back(Triangle(inda, ia, ib));
  out_tris.push_back(Triangle(inda, ib, indb));
}

// two vertices clipped: just replace them with the new points
static void clipTwoVertices(Vert4Array &verts, const Triangle &t, const std::vector<uint8_t> &z_clip, TArray &out_tris){
  const unsigned int idx[3] = {t.x, t.y, t.z};

  const int ui = !z_clip[idx[0]] ? 0 : !z_clip[idx[1]] ? 1 : 2;
  const unsigned int iu = idx[ui];
  const unsigned int ia = idx[(ui+1)%3];
  const unsigned int ib = idx[(ui+2)%3];
  const dvec4 u = verts[iu];
  const dvec4 a = verts[ia];
  const dvec4 b = verts[ib];

  const double du = nearDist(u);
  const double da = nearDist(a);
  const double db = nearDist(b);

  const double ta = du / (du - da);
  const double tb = du / (du - db);

  const unsigned int inda = pushVert(verts, u + ta * (a - u));
  const unsigned int indb = pushVert(verts, u + tb * (b - u));

  out_tris.push_back(Triangle(inda, indb, iu));
}

// reform a partially clipped triangle as zero, one, or two new triangles
static void clipPartialTriangle(Vert4Array &verts, const Triangle &t, const std::vector<uint8_t> &z_clip, TArray &out_tris){
  const unsigned int nc = z_clip[t.x] + z_clip[t.y] + z_clip[t.z];
  assert(nc == 1 || nc == 2);

  if(nc == 1){
    clipOneVertex(verts, t, z_clip, out_tris);
  } else {
    clipTwoVertices(verts, t, z_clip, out_tris);
  }
}

void Camera::clip(Mesh &clip_mesh){
  Vert4Array &clip_space_verts = clip_mesh.getVerts();
  const TArray tris = clip_mesh.getTris(); // copy, the mesh's triangles are replaced below
  TArray &out_tris = clip_mesh.getTris();
  const std::vector<Color> colors = clip_mesh.getColors(); // copy, replaced below like the triangles
  std::vector<Color> &out_colors = clip_mesh.getColors();

  // reserve enough space just in case nothing gets clipped
  out_tris.clear();
  out_tris.reserve(tris.size());
  out_colors.clear();
  out_colors.reserve(tris.size());

  const std::vector<uint8_t> z_clip = findClippedVerts(clip_space_verts);
  const std::vector<TriClipNear> tri_clip = classifyTriangles(tris, z_clip);

  for(unsigned int i=0; i<tris.size(); i++){
    if(tri_clip[i] == TRI_CLIP_INSIDE){
      // copy as-is to out
      out_tris.push_back(tris[i]);
    } else if(tri_clip[i] == TRI_CLIP_PARTIAL){
      const Triangle &t = tris[i];
      clipPartialTriangle(clip_space_verts, t, z_clip, out_tris);
    }

    // every triangle emitted from tris[i] keeps its color
    out_colors.resize(out_tris.size(), colors[i]);
  }
}
