#include <cstdio>
#include <stddef.h>

__global__ void helloWorldKernel(){
  printf("Hello World!\n");
}

void helloWorld(){
  helloWorldKernel<<<1,1>>>();
  cudaDeviceSynchronize();
}

