#pragma once

// Turns the expression-based config values into the numbers and matrices
// needed for a single frame.

#include "Config.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace varyn {

inline glm::vec3 evaluate(const Vec3Expression& v, const ExpressionContext& context) {
    return {float(v[0].evaluate(context)), float(v[1].evaluate(context)), float(v[2].evaluate(context))};
}

inline glm::vec4 evaluate(const Vec4Expression& v, const ExpressionContext& context) {
    return {float(v[0].evaluate(context)), float(v[1].evaluate(context)), float(v[2].evaluate(context)),
            float(v[3].evaluate(context))};
}

struct CameraMatrices {
    glm::vec3 position;
    glm::mat4 view;
    glm::mat4 projection;
};

inline CameraMatrices evaluateCamera(const CameraConfig& camera, const ExpressionContext& context, float aspect) {
    CameraMatrices result;
    result.position = evaluate(camera.position, context);
    result.view = glm::lookAt(result.position, evaluate(camera.target, context), evaluate(camera.up, context));
    result.projection = glm::perspective(glm::radians(float(camera.fov.evaluate(context))), aspect,
                                         float(camera.near.evaluate(context)), float(camera.far.evaluate(context)));
    return result;
}

// translation * rotationZ * rotationY * rotationX * scale
inline glm::mat4 evaluateModelMatrix(const InstanceConfig& instance, const ExpressionContext& context) {
    glm::vec3 rotation = glm::radians(evaluate(instance.rotation, context));

    glm::mat4 matrix = glm::translate(glm::mat4(1.0f), evaluate(instance.position, context));
    matrix = glm::rotate(matrix, rotation.z, {0.0f, 0.0f, 1.0f});
    matrix = glm::rotate(matrix, rotation.y, {0.0f, 1.0f, 0.0f});
    matrix = glm::rotate(matrix, rotation.x, {1.0f, 0.0f, 0.0f});
    return glm::scale(matrix, evaluate(instance.scale, context));
}

}  // namespace varyn
