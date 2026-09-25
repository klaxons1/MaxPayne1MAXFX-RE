#ifndef MAXFX_VIEWER_CAMERA_H
#define MAXFX_VIEWER_CAMERA_H

#include "maxfx/core/Math.h"

namespace maxfx {

struct Camera {
    Vec3 position;
    float yaw;    // radians, 0 looks along -Z after the X-mirror
    float pitch;  // radians
    float moveSpeed;
    float sprintMul;
    float sensitivity;

    Camera()
        : position(0.0f, 0.0f, 0.0f),
          yaw(0.0f),
          pitch(0.0f),
          moveSpeed(6.0f),
          sprintMul(3.0f),
          sensitivity(0.0022f) {}

    Vec3 forward() const {
        const float cp = std::cos(pitch);
        return Vec3(std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp);
    }

    Vec3 right() const {
        return normalize(cross(forward(), Vec3(0.0f, 1.0f, 0.0f)));
    }

    void addLook(float dx, float dy) {
        yaw += dx * sensitivity;
        pitch = clamp(pitch - dy * sensitivity, toRadians(-89.0f), toRadians(89.0f));
    }

    void fly(float forwardAmount, float rightAmount, float upAmount, float dt, bool sprint) {
        const float speed = moveSpeed * (sprint ? sprintMul : 1.0f);
        Vec3 f = forward();
        f.y = 0.0f;
        f = normalize(f);
        const Vec3 r = right();
        position += f * (forwardAmount * speed * dt);
        position += r * (rightAmount * speed * dt);
        position.y += upAmount * speed * dt;
    }

    Mat4 viewMatrix() const {
        return lookAtRH(position, position + forward(), Vec3(0.0f, 1.0f, 0.0f));
    }
};

}  // namespace maxfx

#endif  // MAXFX_VIEWER_CAMERA_H
