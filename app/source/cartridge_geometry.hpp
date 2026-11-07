#pragma once
#include <cstdint>
namespace CartridgeGeometry {
struct Point {float x,y,z;};
struct Triangle {uint16_t a,b,c;};
struct BspNode {Point normal;float distance;int front,back;unsigned start,count;};
struct Mesh {const Point *points;const Triangle *faces;const BspNode *nodes;unsigned pointCount,faceCount;};
}
