#include "render/render.hpp"
#include <limits>

// want to tweak these when testing to see most efficient block size for rasterization
#define RASTER_BLOCK_X 16
#define RASTER_BLOCK_Y 16

void rasterizeLaunch() {
    Renderer &r = Renderer::instance();
    DeviceScreen &screen = DeviceScreen::instance();

    dim3 block(RASTER_BLOCK_X, RASTER_BLOCK_Y);
    dim3 grid((screen.WIDTH  + block.x - 1) / block.x, (screen.HEIGHT + block.y - 1) / block.y);



    rasterize<<<grid, block>>>(r.verts2d->data(), r.verts->data(), r.tris->data(), r.tris->size(), 
                                r.verts2d->size(), screen, r.entities->data(), 2 * HostCube::numTris());

    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    Camera& camera = Camera::instance();

    CUDA_CHECK(cudaMemcpy(camera.framebuffer.data(), screen.framebuffer, screen.WIDTH * screen.HEIGHT * sizeof(Color), cudaMemcpyDeviceToHost));

}

/*  helper for calculating barycentric coordinates
    this function finds which side of the edge the point p is on, relative to the edge from a to b
    positive: one side, negative: the other side, zero: on the edge
    this is used to determine if a point is inside a triangle by checking its position relative to all three edges of the triangle

    all signs need to be the same for the point to be inside the triangle: all 3 positive or all 3 negative
    if edge returns 0, then the other two edge must agree with sign

    this is also equiv to the cross product u = p-a, v = b-a. UxVy - UyVx = |u||v|sin(theta) 
*/
MEEP_DEVICE float edge(const Vertex2D &a, const Vertex2D &b, float p_x, float p_y) {
    return (p_x - a.x) * (b.y - a.y) - (p_y - a.y) * (b.x - a.x);
}


MEEP_GLOBAL void rasterize(const Vertex2D *verts2d, const Vertex *verts, const Tri *tris, 
                            std::size_t num_tris, std::size_t num_verts, DeviceScreenData screen,
                            const CellRenderEntity* entities, std::size_t tris_per_entity) {


    const std::size_t x = blockIdx.x * blockDim.x + threadIdx.x;
    const std::size_t y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= screen.WIDTH || y >= screen.HEIGHT) return;

    // idx is in row major order 
    const std::size_t idx = y * screen.WIDTH + x;

    // center of the pixel
    const float pixel_center_x = static_cast<float>(x) + 0.5f;
    const float pixel_center_y = static_cast<float>(y) + 0.5f;

    Color best_color{0, 0, 0, 255}; // black background
    float best_depth = std::numeric_limits<float>::max(); // initialize to max depth

    // use barycentric coordinates (explain to others)


    for (std::size_t t = 0; t < num_tris; ++t) {
        const Tri &tri = tris[t];
        
        // skip culled triangles
        if (tri.x >= num_verts || tri.y >= num_verts || tri.z >= num_verts) {
            continue;
        }

        const Vertex2D a = verts2d[tri.x];
        const Vertex2D b = verts2d[tri.y];
        const Vertex2D c = verts2d[tri.z];

        const float area2 = edge(a, b, c.x, c.y);
        if (area2 == 0) {
            continue;
        }

        // weight for opposite vertex of each edge
        const float e0 = edge(a, b, pixel_center_x, pixel_center_y); // edge AB --> weight for C
        const float e1 = edge(b, c, pixel_center_x, pixel_center_y); // edge BC --> weight for A
        const float e2 = edge(c, a, pixel_center_x, pixel_center_y); // edge CA --> weight for B

        const bool has_negative = e0 < 0 || e1 < 0 || e2 < 0;
        const bool has_positive = e0 > 0 || e1 > 0 || e2 > 0;

        if (has_negative && has_positive) { // signs dont match, pixel outside
            continue;
        }

        // compute barycentric coordinates
        const float alpha = e1 / area2;
        const float beta = e2 / area2;
        const float gamma = e0 / area2;

        const Vertex va = verts[tri.x];
        const Vertex vb = verts[tri.y];
        const Vertex vc = verts[tri.z];

        // P = alpha * A + beta * B + gamma * C
        const float depth = alpha * va.z + beta * vb.z + gamma * vc.z;
        
        if (!(depth >= -1.0f && depth <= 1.0f))
            continue;

        if (depth < best_depth) {
            best_depth = depth;
            const std::size_t entity_idx = t / tris_per_entity;
            best_color = entities[entity_idx].color;
        }
    }
    // write to buf
    screen.framebuffer[idx] = best_color;
    screen.depthbuffer[idx] = best_depth;
}