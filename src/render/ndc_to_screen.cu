#include "render/render.hpp"


// x = (CAMERA_SCREEN_WIDTH / 2) * (x + 1);
// y = (CAMERA_SCREEN_HEIGHT / 2) * (1 - y);

void ndcToScreenLaunch(){
    Renderer &r = Renderer::instance();
    std::size_t num_verts = r.verts->size();
    uint16_t TPB = 256; // threads per blk
    std::size_t blocks = (num_verts + TPB - 1) / TPB;


    r.verts2d = std::make_unique<DeviceBuffer<Vertex2D>>(num_verts);

    ndcToScreen<<<blocks, TPB>>>(r.verts->data(), r.verts2d->data(), num_verts);
    cudaDeviceSynchronize();

#if (MEEP_DEBUG == 1)
    std::vector<Vertex2D> host_verts2d(num_verts);
    CUDA_CHECK(cudaMemcpy(host_verts2d.data(), r.verts2d->data(), num_verts * sizeof(Vertex2D), cudaMemcpyDeviceToHost));

    printf("verts2d after ndc->screen:\n");
    for (std::size_t i = 0; i < num_verts; i++) {
        printf("  [%zu] %f %f\n", i, host_verts2d[i].x, host_verts2d[i].y);
    }
#endif

}

MEEP_GLOBAL void ndcToScreen(const Vertex* verts, Vertex2D *verts2d, std::size_t count){
    std::size_t idx = blockIdx.x * blockDim.x + threadIdx.x;

    if (idx >= count) return;

    float MAX = std::numeric_limits<float>::max();
    if (verts[idx].w == MAX) {
        verts2d[idx].x = MAX;
        verts2d[idx].y = MAX;
        return;
    }

    verts2d[idx].x = (static_cast<float>(CAMERA_SCREEN_WIDTH) / 2) * (verts[idx].x + 1);
    verts2d[idx].y = (static_cast<float>(CAMERA_SCREEN_HEIGHT) / 2) * (1 - verts[idx].y);

}