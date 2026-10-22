#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace glm;

// Interface for anything with a pose that can be moved around.
// Implementers provide transform() and getPosition(); the rest is built on those.
class Movable{
public:
  virtual ~Movable() = default;

  virtual void transform(const dmat4x4 &trans) = 0;
  virtual dvec3 getPosition() const = 0;

  // Orbit about the pivot around the world up axis.
  void orbit(const dvec3 &pivot, double deg){
    transform(rotateAboutMat(pivot, angleAxis(glm::radians(deg), dvec3(0, 1, 0))));
  }

  void translate(const dvec3 &trans){
    transform(glm::translate(dmat4x4(1.0), trans));
  }

  // Rotate in place: orbit about the object's own position, so pos is fixed and look turns.
  void rotate(const dquat &q){
    transform(rotateAboutMat(getPosition(), q));
  }

  // deg = (pitch about x, yaw about y, roll about z) in degrees.
  // Roll has no effect since Pose::lookQuat() derives orientation from look with fixed up.
  void rotateDegAxis(const dvec3 &deg){
    rotate(dquat(glm::radians(deg)));
  }

private:
  // Rotation q about pivot: T(pivot) * R * T(-pivot).
  static dmat4x4 rotateAboutMat(const dvec3 &pivot, const dquat &q){
    return glm::translate(dmat4x4(1.0), pivot)
         * mat4_cast(q)
         * glm::translate(dmat4x4(1.0), -pivot);
  }
};
