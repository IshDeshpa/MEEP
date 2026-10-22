// Plan:
// - State is resident on GPU
// - State -> World-space vertices, triangles, and colors
// - World -> Clip space 
// - Run clipping on near plane
// - Clipped -> NDC 
// - NDC -> Screen
// - Rasterize screen


