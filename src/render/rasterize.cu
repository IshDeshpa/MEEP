#include "render/render.hpp"
#include <limits>
#include <cstdio>
#include <fstream>
#include <stdexcept>

#define BACKGROUND_COLOR 25 // value of all r,g,b channel for bg

// want to tweak these when testing to see most efficient block size for rasterization
#define RASTER_BLOCK_X 16
#define RASTER_BLOCK_Y 16

void rasterizeLaunch() {
    Renderer &r = Renderer::instance();
    DeviceScreen &screen = DeviceScreen::instance();

    dim3 block(RASTER_BLOCK_X, RASTER_BLOCK_Y);
    dim3 grid((screen.WIDTH  + block.x - 1) / block.x, (screen.HEIGHT + block.y - 1) / block.y);



    rasterize<<<grid, block>>>(r.verts2d->data(), r.verts->data(), r.tris->data(), r.tris->size(), 
                                r.verts2d->size(), screen, r.entities->data(), 2 * HostCube::numTris(), r.tri_directions->data());

    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    Camera& camera = Camera::instance();

    CUDA_CHECK(cudaMemcpy(camera.framebuffer.data(), screen.framebuffer, screen.WIDTH * screen.HEIGHT * sizeof(Color), cudaMemcpyDeviceToHost));

#if (MEEP_DEBUG == 1)
    std::size_t colored_pixels = 0;

    for (const Color& pixel : camera.framebuffer) {
        if (pixel.r != BACKGROUND_COLOR || pixel.g != BACKGROUND_COLOR || pixel.b != BACKGROUND_COLOR)
            ++colored_pixels;
    }

    printf("Rasterizer: %zu / %zu pixels are not part of the background\n",
           colored_pixels, camera.framebuffer.size());

    const std::size_t sample_x[] = {
        screen.WIDTH / 4,
        screen.WIDTH / 2,
        3 * screen.WIDTH / 4
    };
    const std::size_t sample_y = screen.HEIGHT / 2;

    for (const std::size_t px : sample_x) {
        const Color& color =
            camera.framebuffer[sample_y * screen.WIDTH + px];

        printf("Pixel (%zu, %zu): RGBA=(%u, %u, %u, %u)\n",
               px, sample_y,
               static_cast<unsigned>(color.r),
               static_cast<unsigned>(color.g),
               static_cast<unsigned>(color.b),
               static_cast<unsigned>(color.a));
    }
#endif

#if (MEEP_DEBUG == 1)
    std::ofstream image("render_debug.ppm", std::ios::binary);
    if (!image)
        throw std::runtime_error("Cannot open render_debug.ppm");

    // P6 = binary RGB, followed by dimensions and maximum channel value.
    image << "P6\n"
          << screen.WIDTH << ' ' << screen.HEIGHT << "\n255\n";

    for (const Color& pixel : camera.framebuffer) {
        const unsigned char rgb[3] = {
            pixel.r, pixel.g, pixel.b
        };
        image.write(reinterpret_cast<const char*>(rgb), sizeof(rgb));
    }

    image.close();
    if (!image)
        throw std::runtime_error("Failed to write render_debug.ppm");
#endif


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
                            const CellRenderEntity* entities, std::size_t tris_per_entity, const Direction* tri_directions) {


    const std::size_t x = blockIdx.x * blockDim.x + threadIdx.x;
    const std::size_t y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= screen.WIDTH || y >= screen.HEIGHT) return;

    // idx is in row major order 
    const std::size_t idx = y * screen.WIDTH + x;

    // center of the pixel
    const float pixel_center_x = static_cast<float>(x) + 0.5f;
    const float pixel_center_y = static_cast<float>(y) + 0.5f;

    // transparency, max of 3 transparent colors to blend
    Color transparent_colors[3]{}; // initialize to background color
    float transparent_depths[3] = {std::numeric_limits<float>::max(), 
        std::numeric_limits<float>::max(), std::numeric_limits<float>::max()}; // initialize to max depth

    Color bg_color{BACKGROUND_COLOR, BACKGROUND_COLOR, BACKGROUND_COLOR, 255}; // initialize to background color
    float best_depth = std::numeric_limits<float>::max(); // initialize to max depth

    for (std::size_t t = 0; t < num_tris; ++t) {
        const Tri &tri = tris[t];
        
        // skip culled triangles
        if (tri.x >= num_verts || tri.y >= num_verts || tri.z >= num_verts) {
            continue;
        }

        const std::size_t entity_idx = t / tris_per_entity;
        Color color = entities[entity_idx].color;

        const Direction dir = tri_directions[t];

        const glm::vec3 normal = glm::vec3(DIRECTION_LUT[dir]);

        const glm::vec3 cube_center = glm::vec3(entities[entity_idx].coords());

        // For your unit cubes centered on their cell coordinates.
        const glm::vec3 face_center = cube_center + 0.5f * normal;

        const glm::vec3 light_position(0.0f, 0.0f, 0.0f);
        const glm::vec3 toward_light = light_position - face_center;

        const float distance_squared = glm::dot(toward_light, toward_light);

        float diffuse = 0.0f;
        if (distance_squared > 0.0f) {
            const glm::vec3 light_direction = toward_light / sqrtf(distance_squared);
            diffuse = glm::max(0.0f, glm::dot(normal, light_direction));
        }

        const float brightness = 0.6f + 0.4f * diffuse;

        

        color.r = static_cast<std::uint8_t>(color.r * brightness);
        color.g = static_cast<std::uint8_t>(color.g * brightness);
        color.b = static_cast<std::uint8_t>(color.b * brightness);

        if (color.a == 0) { // fully transparent, skip
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

        if (color.a == 255) { // fully opaque, just write to buffer
            if (depth < best_depth) {
                best_depth = depth;
                bg_color = color;
            }
        } else {
            // partially transparent, blend with existing color
            for (int i = 0; i < 3; ++i) {
                if (depth < transparent_depths[i]) {
                    // shift existing colors down
                    for (int j = 2; j > i; --j) {
                        transparent_colors[j] = transparent_colors[j - 1];
                        transparent_depths[j] = transparent_depths[j - 1];
                    }
                    // insert new color
                    transparent_colors[i] = color;
                    transparent_depths[i] = depth;
                    break;
                }
            }
        }
    }
    for (int i = 2; i >= 0; --i) {
        if (transparent_depths[i] < best_depth) {
            const Color &trans_color = transparent_colors[i];
            const Color &opaque_color = bg_color;
            const float opacity = trans_color.a / 255.0f;
            // do weighted avg of all channels
            bg_color.r = opacity * trans_color.r + (1 - opacity) * opaque_color.r; 
            bg_color.g = opacity * trans_color.g + (1 - opacity) * opaque_color.g;
            bg_color.b = opacity * trans_color.b + (1 - opacity) * opaque_color.b;
            bg_color.a = 255; // full opaque
            
        }
    }
    


    // write to buf
    screen.framebuffer[idx] = bg_color;
    screen.depthbuffer[idx] = best_depth;
}