#!/bin/bash

ffmpeg -framerate 30 -i out/cubes%d.ppm -c:v libx264 -pix_fmt yuv420p out/orbit.mp4
