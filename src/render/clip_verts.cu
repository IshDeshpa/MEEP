#include "render/render.hpp"
#include "render/cell.hpp"

struct EdgeCacheEntry{ 
    std::size_t v1;
    std::size_t v2;
    std::size_t new_v_idx;
};


void clipVertsLaunch() {
    Renderer &r = Renderer::instance();
    std::size_t num_entities = r.entities->size();
    uint16_t TPB = 256; // Threads per block
    std::size_t blocks = (num_entities + TPB - 1) / TPB; // Number of blocks needed

    clipVertsByNear<<<blocks, TPB>>>(r.verts->data(), r.tris->data(), num_entities);
    cudaDeviceSynchronize();

#if (MEEP_DEBUG == 1)
    std::size_t verts_sz = r.verts->size();   // full 2x capacity, so you see real + headroom
    std::size_t tris_sz  = r.tris->size();

    std::vector<Vertex> host_verts(verts_sz);
    CUDA_CHECK(cudaMemcpy(host_verts.data(), r.verts->data(), verts_sz * sizeof(Vertex), cudaMemcpyDeviceToHost));

    printf("verts after clip:\n");
    for (std::size_t i = 0; i < verts_sz; i++) {
        printf("  [%zu] %f %f %f %f\n", i, host_verts[i].x, host_verts[i].y, host_verts[i].z, host_verts[i].w);
    }

    std::vector<Tri> host_tris(tris_sz);
    CUDA_CHECK(cudaMemcpy(host_tris.data(), r.tris->data(), tris_sz * sizeof(Tri), cudaMemcpyDeviceToHost));

    printf("tris after clip:\n");
    for (std::size_t i = 0; i < tris_sz; i++) {
        printf("  [%zu] %u %u %u\n", i, host_tris[i].x, host_tris[i].y, host_tris[i].z);
    }
#endif

}

MEEP_DEVICE std::size_t searchEdgeCache(EdgeCacheEntry *cache, std::size_t cache_size, std::size_t v1, std::size_t v2) {
    for (std::size_t i = 0; i < cache_size; ++i) {
        if ((cache[i].v1 == v1 && cache[i].v2 == v2) || (cache[i].v1 == v2 && cache[i].v2 == v1)) {
            return cache[i].new_v_idx;
        }
    }
    return std::numeric_limits<std::size_t>::max();
}

#define MAX_NEW_VERTS 8

MEEP_GLOBAL void clipVertsByNear(Vertex *verts, Tri *tris, std::size_t num_entities) {
    std::size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= num_entities) return;
    
    std::size_t tri_ct = DEVICE_CUBE.triangleCount;
    std::size_t vert_ct = DEVICE_CUBE.vertexCount;

    std::size_t t_base = idx * tri_ct * 2; // 2 for extra space
    std::size_t v_base = idx * vert_ct * 2;

    Tri::value_type max = std::numeric_limits<Tri::value_type>::max();

    std::size_t new_v_count = 0; // will also be the cache count
    std::size_t new_t_count = 0;

    EdgeCacheEntry edge_cache[MAX_NEW_VERTS];


    for (std::size_t i = 0; i < tri_ct; ++i) {
        Tri &t = tris[t_base + i];

        // fetch the 3 actual Vertex values this triangle references
        Vertex a = verts[t.x];
        Vertex b = verts[t.y];
        Vertex c = verts[t.z];

        bool a_out = false, b_out = false, c_out = false;

        // classify each of the 3 as "in front" or "behind" using (w - Z_NEAR)
        a_out = (a.w - CAMERA_Z_NEAR) < 0;
        b_out = (b.w - CAMERA_Z_NEAR) < 0;
        c_out = (c.w - CAMERA_Z_NEAR) < 0;

        // count how many of the 3 are "behind"

        int behind_count = a_out + b_out + c_out;

        // easy cases
        // if count == 0, do nothing — just let the loop move to the next i
        // if count == 3, invalidate this triangle: t.x = t.y = t.z = max_tri
        switch (behind_count) {
            case 0:
                continue;
            case 3:
                t.x = t.y = t.z = max;
                continue;
            case 1:
                if (a_out){
                    // new vertex 1
                    float w_in = b.w;
                    float w_out = a.w;
                    float lerp = (CAMERA_Z_NEAR - w_out) / (w_in - w_out);
                    Vertex N_a1;
                    N_a1.x = a.x + lerp * (b.x - a.x);
                    N_a1.y = a.y + lerp * (b.y - a.y);
                    N_a1.z = a.z + lerp * (b.z - a.z);
                    N_a1.w = a.w + lerp * (b.w - a.w);

                    std::size_t N_a1_idx = searchEdgeCache(edge_cache, new_v_count, t.y, t.x);
                    if (N_a1_idx == std::numeric_limits<std::size_t>::max()) {
                        N_a1_idx = v_base + vert_ct + new_v_count;
                        edge_cache[new_v_count].v1 = t.y;
                        edge_cache[new_v_count].v2 = t.x;
                        edge_cache[new_v_count].new_v_idx = N_a1_idx;
                        new_v_count++;
                    }
                    verts[N_a1_idx] = N_a1;

                    // new vertex 2
                    w_in = c.w;
                    lerp = (CAMERA_Z_NEAR - w_out) / (w_in - w_out);
                    Vertex N_a2;
                    N_a2.x = a.x + lerp * (c.x - a.x);
                    N_a2.y = a.y + lerp * (c.y - a.y);
                    N_a2.z = a.z + lerp * (c.z - a.z);
                    N_a2.w = a.w + lerp * (c.w - a.w);
                    std::size_t N_a2_idx = searchEdgeCache(edge_cache, new_v_count, t.z, t.x);
                    if (N_a2_idx == std::numeric_limits<std::size_t>::max()) {
                        N_a2_idx = v_base + vert_ct + new_v_count;
                        edge_cache[new_v_count].v1 = t.x;
                        edge_cache[new_v_count].v2 = t.z;
                        edge_cache[new_v_count].new_v_idx = N_a2_idx;
                        new_v_count++;
                    }
                    verts[N_a2_idx] = N_a2;

                    // a --> b --> c turns into N_a1 --> b --> c --> N_a2 cycle 
                    // new triangles - {N_a1, b, c} and {N_a1, c, N_a2}

                    t.x = N_a1_idx;
                    Tri &new_t = tris[t_base + tri_ct + new_t_count];
                    new_t.x = N_a1_idx;
                    new_t.y = t.z;
                    new_t.z = N_a2_idx;
                    new_t_count++;
                
                } else if (b_out) {
                    // v1
                    float w_in = a.w;
                    float w_out = b.w;
                    float lerp = (CAMERA_Z_NEAR - w_out) / (w_in - w_out);
                    Vertex N_b1;
                    N_b1.x = b.x + lerp * (a.x - b.x);
                    N_b1.y = b.y + lerp * (a.y - b.y);
                    N_b1.z = b.z + lerp * (a.z - b.z);
                    N_b1.w = b.w + lerp * (a.w - b.w);
                    std::size_t N_b1_idx = searchEdgeCache(edge_cache, new_v_count, t.x, t.y);
                    if (N_b1_idx == std::numeric_limits<std::size_t>::max()) {
                        N_b1_idx = v_base + vert_ct + new_v_count;
                        edge_cache[new_v_count].v1 = t.x;
                        edge_cache[new_v_count].v2 = t.y;
                        edge_cache[new_v_count].new_v_idx = N_b1_idx;
                        new_v_count++;
                    }
                    verts[N_b1_idx] = N_b1;
                    // v2
                    w_in = c.w;
                    lerp = (CAMERA_Z_NEAR - w_out) / (w_in - w_out);
                    Vertex N_b2;
                    N_b2.x = b.x + lerp * (c.x - b.x);
                    N_b2.y = b.y + lerp * (c.y - b.y);
                    N_b2.z = b.z + lerp * (c.z - b.z);
                    N_b2.w = b.w + lerp * (c.w - b.w);
                    std::size_t N_b2_idx = searchEdgeCache(edge_cache, new_v_count, t.z, t.y);
                    if (N_b2_idx == std::numeric_limits<std::size_t>::max()) {
                        N_b2_idx = v_base + vert_ct + new_v_count;
                        edge_cache[new_v_count].v1 = t.z;
                        edge_cache[new_v_count].v2 = t.y;
                        edge_cache[new_v_count].new_v_idx = N_b2_idx;
                        new_v_count++;
                    }
                    verts[N_b2_idx] = N_b2;

                    // a --> b --> c turns into a --> N_b1 --> N_b2 --> c cycle
                    // new triangles - {a, N_b2, c} and {a, N_b1, N_b2}
                    t.y = N_b2_idx;
                    Tri &new_t = tris[t_base + tri_ct + new_t_count];
                    new_t.x = t.x;
                    new_t.y = N_b1_idx;
                    new_t.z = N_b2_idx;
                    new_t_count++;

                } else if (c_out) {
                    // v1
                    float w_in = b.w;
                    float w_out = c.w;
                    float lerp = (CAMERA_Z_NEAR - w_out) / (w_in - w_out);
                    Vertex N_c1;
                    N_c1.x = c.x + lerp * (b.x - c.x);
                    N_c1.y = c.y + lerp * (b.y - c.y);
                    N_c1.z = c.z + lerp * (b.z - c.z);
                    N_c1.w = c.w + lerp * (b.w - c.w);
                    std::size_t N_c1_idx = searchEdgeCache(edge_cache, new_v_count, t.y, t.z);
                    if (N_c1_idx == std::numeric_limits<std::size_t>::max()) {
                        N_c1_idx = v_base + vert_ct + new_v_count;
                        edge_cache[new_v_count].v1 = t.y;
                        edge_cache[new_v_count].v2 = t.z;
                        edge_cache[new_v_count].new_v_idx = N_c1_idx;
                        new_v_count++;
                    }
                    verts[N_c1_idx] = N_c1;

                    // v2
                    w_in = a.w;
                    lerp = (CAMERA_Z_NEAR - w_out) / (w_in - w_out);
                    Vertex N_c2;
                    N_c2.x = c.x + lerp * (a.x - c.x);
                    N_c2.y = c.y + lerp * (a.y - c.y);
                    N_c2.z = c.z + lerp * (a.z - c.z);
                    N_c2.w = c.w + lerp * (a.w - c.w);
                    std::size_t N_c2_idx = searchEdgeCache(edge_cache, new_v_count, t.x, t.z);
                    if (N_c2_idx == std::numeric_limits<std::size_t>::max()) {
                        N_c2_idx = v_base + vert_ct + new_v_count;
                        edge_cache[new_v_count].v1 = t.x;
                        edge_cache[new_v_count].v2 = t.z;
                        edge_cache[new_v_count].new_v_idx = N_c2_idx;
                        new_v_count++;
                    }
                    verts[N_c2_idx] = N_c2;

                    // a --> b --> c turns into a --> b --> N_c1 --> N_c2 --> cycle
                    // new tri - {a, b, N_c1} and {a, N_c1, N_c2}
                    t.z = N_c1_idx;
                    Tri &new_t = tris[t_base + tri_ct + new_t_count];
                    new_t.x = t.x;
                    new_t.y = N_c1_idx;
                    new_t.z = N_c2_idx;
                    new_t_count++;
                }
                break;

            case 2:
                if (a_out && b_out) {
                    float w_in = c.w;
                    float w_out = a.w;
                    float lerp = (CAMERA_Z_NEAR - w_out) / (w_in - w_out);
                    Vertex N_a;
                    N_a.x = a.x + lerp * (c.x - a.x);
                    N_a.y = a.y + lerp * (c.y - a.y);
                    N_a.z = a.z + lerp * (c.z - a.z);
                    N_a.w = a.w + lerp * (c.w - a.w);
                    std::size_t N_a_idx = searchEdgeCache(edge_cache, new_v_count, t.z, t.x);
                    if (N_a_idx == std::numeric_limits<std::size_t>::max()) {
                        N_a_idx = v_base + vert_ct + new_v_count;
                        edge_cache[new_v_count].v1 = t.z;
                        edge_cache[new_v_count].v2 = t.x;
                        edge_cache[new_v_count].new_v_idx = N_a_idx;
                        new_v_count++;
                    }
                    verts[N_a_idx] = N_a;

                    w_out = b.w;
                    lerp = (CAMERA_Z_NEAR - w_out) / (w_in - w_out);
                    Vertex N_b;
                    N_b.x = b.x + lerp * (c.x - b.x);
                    N_b.y = b.y + lerp * (c.y - b.y);
                    N_b.z = b.z + lerp * (c.z - b.z);
                    N_b.w = b.w + lerp * (c.w - b.w);
                    std::size_t N_b_idx = searchEdgeCache(edge_cache, new_v_count, t.z, t.y);
                    if (N_b_idx == std::numeric_limits<std::size_t>::max()) {
                        N_b_idx = v_base + vert_ct + new_v_count;
                        edge_cache[new_v_count].v1 = t.z;
                        edge_cache[new_v_count].v2 = t.y;
                        edge_cache[new_v_count].new_v_idx = N_b_idx;
                        new_v_count++;
                    }
                    verts[N_b_idx] = N_b;
                    t.x = N_a_idx;
                    t.y = N_b_idx;

                } else if (a_out && c_out) {
                    float w_in = b.w;
                    float w_out = a.w;
                    float lerp = (CAMERA_Z_NEAR - w_out) / (w_in - w_out);
                    Vertex N_a;
                    N_a.x = a.x + lerp * (b.x - a.x);
                    N_a.y = a.y + lerp * (b.y - a.y);
                    N_a.z = a.z + lerp * (b.z - a.z);
                    N_a.w = a.w + lerp * (b.w - a.w);
                    std::size_t N_a_idx = searchEdgeCache(edge_cache, new_v_count, t.y, t.x);
                    if (N_a_idx == std::numeric_limits<std::size_t>::max()) {
                        N_a_idx = v_base + vert_ct + new_v_count;
                        edge_cache[new_v_count].v1 = t.y;
                        edge_cache[new_v_count].v2 = t.x;
                        edge_cache[new_v_count].new_v_idx = N_a_idx;
                        new_v_count++;
                    }
                    verts[N_a_idx] = N_a;

                    w_out = c.w;
                    lerp = (CAMERA_Z_NEAR - w_out) / (w_in - w_out);
                    Vertex N_c;
                    N_c.x = c.x + lerp * (b.x - c.x);
                    N_c.y = c.y + lerp * (b.y - c.y);
                    N_c.z = c.z + lerp * (b.z - c.z);
                    N_c.w = c.w + lerp * (b.w - c.w);
                    std::size_t N_c_idx = searchEdgeCache(edge_cache, new_v_count, t.y, t.z);
                    if (N_c_idx == std::numeric_limits<std::size_t>::max()) {
                        N_c_idx = v_base + vert_ct + new_v_count;
                        edge_cache[new_v_count].v1 = t.y;
                        edge_cache[new_v_count].v2 = t.z;
                        edge_cache[new_v_count].new_v_idx = N_c_idx;
                        new_v_count++;
                    }
                    verts[N_c_idx] = N_c;
                    t.x = N_a_idx;
                    t.z = N_c_idx;

                } else if (b_out && c_out) {
                    float w_in = a.w;
                    float w_out = b.w;
                    float lerp = (CAMERA_Z_NEAR - w_out) / (w_in - w_out);
                    Vertex N_b;
                    N_b.x = b.x + lerp * (a.x - b.x);
                    N_b.y = b.y + lerp * (a.y - b.y);
                    N_b.z = b.z + lerp * (a.z - b.z);
                    N_b.w = b.w + lerp * (a.w - b.w);
                    std::size_t N_b_idx = searchEdgeCache(edge_cache, new_v_count, t.x, t.y);
                    if (N_b_idx == std::numeric_limits<std::size_t>::max()) {
                        N_b_idx = v_base + vert_ct + new_v_count;
                        edge_cache[new_v_count].v1 = t.x;
                        edge_cache[new_v_count].v2 = t.y;
                        edge_cache[new_v_count].new_v_idx = N_b_idx;
                        new_v_count++;
                    }
                    verts[N_b_idx] = N_b;

                    w_out = c.w;
                    lerp = (CAMERA_Z_NEAR - w_out) / (w_in - w_out);
                    Vertex N_c;
                    N_c.x = c.x + lerp * (a.x - c.x);
                    N_c.y = c.y + lerp * (a.y - c.y);
                    N_c.z = c.z + lerp * (a.z - c.z);
                    N_c.w = c.w + lerp * (a.w - c.w);
                    std::size_t N_c_idx = searchEdgeCache(edge_cache, new_v_count, t.x, t.z);
                    if (N_c_idx == std::numeric_limits<std::size_t>::max()) {
                        N_c_idx = v_base + vert_ct + new_v_count;
                        edge_cache[new_v_count].v1 = t.x;
                        edge_cache[new_v_count].v2 = t.z;
                        edge_cache[new_v_count].new_v_idx = N_c_idx;
                        new_v_count++;
                    }
                    verts[N_c_idx] = N_c;
                    t.y = N_b_idx;
                    t.z = N_c_idx;
                }
                break;
        }
    }

}