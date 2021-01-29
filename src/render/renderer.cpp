#include "renderer.h"
#include <algorithm>
#include <algorithm> // std::fill
#include <cmath>
#include <functional>
#include <glm/common.hpp>
#include <glm/gtx/component_wise.hpp>
#include <iostream>
#include <tbb/blocked_range2d.h>
#include <tbb/parallel_for.h>
#include <tuple>

namespace render {

// The renderer is passed a pointer to the volume, gradinet volume, camera and an initial renderConfig.
// The camera being pointed to may change each frame (when the user interacts). When the renderConfig
// changes the setConfig function is called with the updated render config. This gives the Renderer an
// opportunity to resize the framebuffer.
Renderer::Renderer(
    const volume::Volume* pVolume,
    const volume::GradientVolume* pGradientVolume,
    const render::RayTraceCamera* pCamera,
    const RenderConfig& initialConfig)
    : m_pVolume(pVolume)
    , m_pGradientVolume(pGradientVolume)
    , m_pCamera(pCamera)
    , m_config(initialConfig)
{
    resizeImage(initialConfig.renderResolution);
}

// Set a new render config if the user changed the settings.
void Renderer::setConfig(const RenderConfig& config)
{
    if (config.renderResolution != m_config.renderResolution)
        resizeImage(config.renderResolution);

    m_config = config;
}

// Resize the framebuffer and fill it with black pixels.
void Renderer::resizeImage(const glm::ivec2& resolution)
{
    m_frameBuffer.resize(size_t(resolution.x) * size_t(resolution.y), glm::vec4(0.0f));
}

// Clear the framebuffer by setting all pixels to black.
void Renderer::resetImage()
{
    std::fill(std::begin(m_frameBuffer), std::end(m_frameBuffer), glm::vec4(0.0f));
}

// Return a VIEW into the framebuffer. This view is merely a reference to the m_frameBuffer member variable.
// This does NOT make a copy of the framebuffer.
gsl::span<const glm::vec4> Renderer::frameBuffer() const
{
    return m_frameBuffer;
}

// Main render function. It computes an image according to the current renderMode.
// Multithreading is enabled in Release/RelWithDebInfo modes. In Debug mode multithreading is disabled to make debugging easier.
void Renderer::render()
{
    resetImage();

    static constexpr float sampleStep = 1.0f;
    const glm::vec3 planeNormal = -glm::normalize(m_pCamera->forward());
    const glm::vec3 volumeCenter = glm::vec3(m_pVolume->dims()) / 2.0f;
    const Bounds bounds { glm::vec3(0.0f), glm::vec3(m_pVolume->dims() - glm::ivec3(1)) };

    // 0 = sequential (single-core), 1 = TBB (multi-core)
#if true // TODO change back to #ifdef NDEBUG
    // If NOT in debug mode then enable parallelism using the TBB library (Intel Threaded Building Blocks).
#define PARALLELISM 1
#else
    // Disable multithreading in debug mode.
#define PARALLELISM 0
#endif

#if PARALLELISM == 0
    // Regular (single threaded) for loops.
    for (int x = 0; x < m_config.renderResolution.x; x++) {
        for (int y = 0; y < m_config.renderResolution.y; y++) {
#else
    // Parallel for loop (in 2 dimensions) that subdivides the screen into tiles.
    const tbb::blocked_range2d<int> screenRange { 0, m_config.renderResolution.y, 0, m_config.renderResolution.x };
        tbb::parallel_for(screenRange, [&](tbb::blocked_range2d<int> localRange) {
        // Loop over the pixels in a tile. This function is called on multiple threads at the same time.
        for (int y = std::begin(localRange.rows()); y != std::end(localRange.rows()); y++) {
            for (int x = std::begin(localRange.cols()); x != std::end(localRange.cols()); x++) {
#endif
            // Compute a ray for the current pixel.
            const glm::vec2 pixelPos = glm::vec2(x, y) / glm::vec2(m_config.renderResolution);
            Ray ray = m_pCamera->generateRay(pixelPos * 2.0f - 1.0f);

            // Compute where the ray enters and exists the volume.
            // If the ray misses the volume then we continue to the next pixel.
            if (!instersectRayVolumeBounds(ray, bounds))
                continue;

            // Get a color for the current pixel according to the current render mode.
            glm::vec4 color {};
            switch (m_config.renderMode) {
            case RenderMode::RenderSlicer: {
                color = traceRaySlice(ray, volumeCenter, planeNormal);
                break;
            }
            case RenderMode::RenderMIP: {
                color = traceRayMIP(ray, sampleStep);
                break;
            }
            case RenderMode::RenderComposite: {
                color = traceRayComposite(ray, sampleStep);
                break;
            }
            case RenderMode::RenderIso: {
                color = traceRayISO(ray, sampleStep);
                break;
            }
            case RenderMode::RenderTF2D: {
                color = traceRayTF2D(ray, sampleStep);
                break;
            }
            case RenderMode::RenderTF2DV2: {
                color = traceRayTF2DV2(ray, sampleStep);
                break;
            }
            };
            // Write the resulting color to the screen.
            fillColor(x, y, color);

#if PARALLELISM == 1
        }
    }
});
#else
            }
        }
#endif
}

// ======= DO NOT MODIFY THIS FUNCTION ========
// This function generates a view alongside a plane perpendicular to the camera through the center of the volume
//  using the slicing technique.
glm::vec4 Renderer::traceRaySlice(const Ray& ray, const glm::vec3& volumeCenter, const glm::vec3& planeNormal) const
{
    const float t = glm::dot(volumeCenter - ray.origin, planeNormal) / glm::dot(ray.direction, planeNormal);
    const glm::vec3 samplePos = ray.origin + ray.direction * t;
    const float val = m_pVolume->getVoxelInterpolate(samplePos);
    return glm::vec4(glm::vec3(std::max(val / m_pVolume->maximum(), 0.0f)), 1.f);
}

// ======= DO NOT MODIFY THIS FUNCTION ========
// Function that implements maximum-intensity-projection (MIP) raycasting.
// It returns the color assigned to a ray/pixel given it's origin, direction and the distances
// at which it enters/exits the volume (ray.tmin & ray.tmax respectively).
// The ray must be sampled with a distance defined by the sampleStep
glm::vec4 Renderer::traceRayMIP(const Ray& ray, float sampleStep) const
{
    float maxVal = 0.0f;

    // Incrementing samplePos directly instead of recomputing it each frame gives a measureable speed-up.
    glm::vec3 samplePos = ray.origin + ray.tmin * ray.direction;
    const glm::vec3 increment = sampleStep * ray.direction;
    for (float t = ray.tmin; t <= ray.tmax; t += sampleStep, samplePos += increment) {
        const float val = m_pVolume->getVoxelInterpolate(samplePos);
        maxVal = std::max(val, maxVal);
    }

    // Normalize the result to a range of [0 to mpVolume->maximum()].
    return glm::vec4(glm::vec3(maxVal) / m_pVolume->maximum(), 1.0f);
}

// This function should find the position where the ray intersects with the volume's isosurface.
// If volume shading is DISABLED then simply return the isoColor.
// If volume shading is ENABLED then return the phong-shaded color at that location using the local gradient (from m_pGradientVolume).
//   Use the camera position (m_pCamera->position()) as the light position.
// Use the bisectionAccuracy function (to be implemented) to get a more precise isosurface location between two steps.
glm::vec4 Renderer::traceRayISO(const Ray& ray, float sampleStep) const
{
    glm::vec4 color(0.0f);
    if (!this->m_config.volumeShading) {
        glm::vec3 sample_pos = ray.origin + ray.tmin * ray.direction;
        const glm::vec3 increment = sampleStep * ray.direction;

        for (float t = ray.tmin; t <= ray.tmax; t += sampleStep, sample_pos += increment) {
            auto voxel_value = this->m_pVolume->getVoxelInterpolate(sample_pos);
            if (voxel_value > this->m_config.isoValue) {
                // color = this->getTFValue(voxel_value);
                color = glm::vec4 { 0.8f, 0.8f, 0.2f, 1.0f };
                break;
            }
        }
    } else {

        glm::vec3 sample_pos = ray.origin + ray.tmin * ray.direction;
        const glm::vec3 increment = sampleStep * ray.direction;
        bool atLeastTwoSteps = false;

        for (float t = ray.tmin; t <= ray.tmax; t += sampleStep, sample_pos += increment) {
            auto voxel_value = this->m_pVolume->getVoxelInterpolate(sample_pos);
            if (voxel_value > this->m_config.isoValue) {
                if (atLeastTwoSteps) {
                    t = this->bisectionAccuracy(ray, t - sampleStep, t, this->m_config.isoValue);
                    sample_pos = ray.origin + ray.direction * t;
                }
                auto _color = glm::vec4 { 0.8f, 0.8f, 0.2f, 1.0f };
                auto gradient = this->m_pGradientVolume->getGradientVoxel(sample_pos);
                color = glm::vec4(this->computePhongShading(_color, gradient, this->m_pCamera->position(), ray.direction), 1.0f);
                break;
            }
            atLeastTwoSteps = true;
        }
    }

    return color;
}

// Given that the iso value lies somewhere between t0 and t1, find a t for which the value
// closely matches the iso value (less than 0.01 difference). Add a limit to the number of
// iterations such that it does not get stuck in degerate cases.
float Renderer::bisectionAccuracy(const Ray& ray, float t0, float t1, float isoValue) const
{
    glm::vec3 right = ray.origin + ray.tmax * ray.direction;

    int maxIterations = 500;
    const float minDifference = 0.0001;

    float voxelFinalValue = this->m_pVolume->getVoxelInterpolate(right);
    float tMiddle = (t0 + t1) / 2;

    while (maxIterations > 0) {

        maxIterations--;
        tMiddle = (t0 + t1) / 2;

        glm::vec3 middle = ray.origin + tMiddle * ray.direction;
        float voxelValue = this->m_pVolume->getVoxelInterpolate(middle);

        if (std::abs(voxelValue - isoValue) < minDifference) {
            return tMiddle;
        } else if (voxelValue < isoValue) {
            t0 = tMiddle;
        } else {
            t1 = tMiddle;
        }
    }

    return tMiddle;
}

// In this function, implement 1D transfer function raycasting.
// Use getTFValue to compute the color for a given volume value according to the 1D transfer function.
glm::vec4 Renderer::traceRayComposite(const Ray& ray, float sampleStep) const
{
    return this->backToFrontComposite(ray, sampleStep);
}

glm::vec4 Renderer::backToFrontComposite(const Ray& ray, float sampleStep) const
{
    glm::vec3 samplePos = ray.origin + ray.tmax * ray.direction;
    const glm::vec3 increment = sampleStep * ray.direction;

    glm::vec3 color(0.0f);

    for (float t = ray.tmax; t >= ray.tmin; t -= sampleStep, samplePos -= increment) {
        glm::vec4 tf_value = this->getTFValue(this->m_pVolume->getVoxelInterpolate(samplePos));
        glm::vec3 current_color = glm::vec3(tf_value);
        if (this->m_config.volumeShading) {
            auto gradient = this->m_pGradientVolume->getGradientVoxel(samplePos);
            current_color = computePhongShading(current_color, gradient, this->m_pCamera->position(), ray.direction);
        }
        color = tf_value[3] * current_color + (1 - tf_value[3]) * color;
    }

    return glm::vec4(color, 1);
}

// In this function, implement 2D transfer function raycasting.
// Use the getTF2DOpacity function that you implemented to compute the opacity according to the 2D transfer function.
glm::vec4 Renderer::traceRayTF2D(const Ray& ray, float sampleStep) const
{
    glm::vec3 samplePos = ray.origin + ray.tmax * ray.direction;
    const glm::vec3 increment = sampleStep * ray.direction;

    glm::vec3 color(0.0f);

    for (float t = ray.tmax; t >= ray.tmin; t -= sampleStep, samplePos -= increment) {
        float intensity = this->m_pVolume->getVoxelInterpolate(samplePos);
        auto gradient = this->m_pGradientVolume->getGradientVoxel(samplePos);
        float opacity = this->getTF2DOpacity(intensity, gradient.magnitude) * this->m_config.TF2DColor.w;
        auto _color = glm::vec3(this->m_config.TF2DColor);

        if (this->m_config.volumeShading) {
            _color = computePhongShading(_color, gradient, m_pCamera->position(), ray.direction);
        }

        color = opacity * _color + (1 - opacity) * color;
    }

    return glm::vec4(color, 1.0f);
}

// Compute Phong Shading given the voxel color (material color), the gradient, the light vector and view vector.
// You can find out more about the Phong shading model at:
// https://en.wikipedia.org/wiki/Phong_reflection_model
//
// Use the given color for the ambient/specular/diffuse (you are allowed to scale these constants by a scalar value).
// You are free to choose any specular power that you'd like.
glm::vec3 Renderer::computePhongShading(const glm::vec3& color, const volume::GradientVoxel& gradient, const glm::vec3& L, const glm::vec3& V)
{
    const float ka = 0.1;
    const float kd = 0.7;
    const float ks = 0.2;
    const int alpha = 100;
    const int n = 100;
    const float eps = 0.0001;

    const float theta = acos(glm::dot(gradient.dir, -L) / (gradient.magnitude * glm::length(L) + eps));
    const float phi = acos(glm::dot(gradient.dir, V) / (gradient.magnitude * glm::length(V) + eps)) - theta;

    return (ka + kd * cos(theta) + ks * float(pow(cos(phi), n))) * color;
}

// ======= DO NOT MODIFY THIS FUNCTION ========
// Looks up the color+opacity corresponding to the given volume value from the 1D tranfer function LUT (m_config.tfColorMap).
// The value will initially range from (m_config.tfColorMapIndexStart) to (m_config.tfColorMapIndexStart + m_config.tfColorMapIndexRange) .
glm::vec4 Renderer::getTFValue(float val) const
{
    // Map value from [m_config.tfColorMapIndexStart, m_config.tfColorMapIndexStart + m_config.tfColorMapIndexRange) to [0, 1) .
    const float range01 = (val - m_config.tfColorMapIndexStart) / m_config.tfColorMapIndexRange;
    const size_t i = std::min(static_cast<size_t>(range01 * static_cast<float>(m_config.tfColorMap.size())), m_config.tfColorMap.size() - 1);
    return m_config.tfColorMap[i];
}

/**
 *  Checks if the point (intensity, magnitude) is in the triangle defined by the points 
 *          (leftIntensity, 255), (midIntensity, 0), (rightIntensity, 255)
*/
bool inTriangle(float leftIntensity, float midIntensity, float rightIntensity, float intensity, float magnitude)
{
    // TODO explain
    if (intensity <= leftIntensity || intensity >= rightIntensity || magnitude <= 0) {
        return false;
    }

    if (intensity == midIntensity) {
        return true;
    } else if (intensity < midIntensity) { // left side
        return magnitude > (255 * ((midIntensity - intensity) / (midIntensity - leftIntensity)));
    } else { // right side
        return magnitude > (255 * ((intensity - midIntensity) / (rightIntensity - midIntensity)));
    }
}

/**
 *  Computes the opacity based on the in-triangle position 
 * 
*/
float linearOpacity(float intensityCenter, float radius, float intensity, float magnitude)
{
    // TODO explain
    float horizontalWidth = radius * (magnitude / 255);
    return 1 - (abs(intensityCenter - intensity) / horizontalWidth);
}

// This function should return an opacity value for the given intensity and gradient according to the 2D transfer function.
// Calculate whether the values are within the radius/intensity triangle defined in the 2D transfer function widget.
// If so: return a tent weighting as described in the assignment
// Otherwise: return 0.0f
//
// The 2D transfer function settings can be accessed through m_config.TF2DIntensity and m_config.TF2DRadius.
float Renderer::getTF2DOpacity(float intensity, float gradientMagnitude) const
{
    if (inTriangle(
            this->m_config.TF2DIntensity - this->m_config.TF2DRadius,
            this->m_config.TF2DIntensity,
            this->m_config.TF2DIntensity + this->m_config.TF2DRadius,
            intensity,
            gradientMagnitude)) { // inside the triangle
        // at this moment triangleGradientBoundary is the point above intensity where the triangle gets intersected
        return linearOpacity(this->m_config.TF2DIntensity, this->m_config.TF2DRadius, intensity, gradientMagnitude);
        // return 1;
    } else {
        return 0.0f;
    }
    return 0.0f;
}

// This function computes if a ray intersects with the axis-aligned bounding box around the volume.
// If the ray intersects then tmin/tmax are set to the distance at which the ray hits/exists the
// volume and true is returned. If the ray misses the volume the the function returns false.
//
// If you are interested you can learn about it at.
// https://www.scratchapixel.com/lessons/3d-basic-rendering/minimal-ray-tracer-rendering-simple-shapes/ray-box-intersection
bool Renderer::instersectRayVolumeBounds(Ray& ray, const Bounds& bounds) const
{
    const glm::vec3 invDir = 1.0f / ray.direction;
    const glm::bvec3 sign = glm::lessThan(invDir, glm::vec3(0.0f));

    float tmin = (bounds.lowerUpper[sign[0]].x - ray.origin.x) * invDir.x;
    float tmax = (bounds.lowerUpper[!sign[0]].x - ray.origin.x) * invDir.x;
    const float tymin = (bounds.lowerUpper[sign[1]].y - ray.origin.y) * invDir.y;
    const float tymax = (bounds.lowerUpper[!sign[1]].y - ray.origin.y) * invDir.y;

    if ((tmin > tymax) || (tymin > tmax))
        return false;
    tmin = std::max(tmin, tymin);
    tmax = std::min(tmax, tymax);

    const float tzmin = (bounds.lowerUpper[sign[2]].z - ray.origin.z) * invDir.z;
    const float tzmax = (bounds.lowerUpper[!sign[2]].z - ray.origin.z) * invDir.z;

    if ((tmin > tzmax) || (tzmin > tmax))
        return false;

    ray.tmin = std::max(tmin, tzmin);
    ray.tmax = std::min(tmax, tzmax);
    return true;
}

// This function inserts a color into the framebuffer at position x,y
void Renderer::fillColor(int x, int y, const glm::vec4& color)
{
    const size_t index = static_cast<size_t>(m_config.renderResolution.x * y + x);
    m_frameBuffer[index] = color;
}

// TODO change to our needs
// In this function, implement 2D transfer function raycasting.
// Use the getTF2DOpacity function that you implemented to compute the opacity according to the 2D transfer function.
glm::vec4 Renderer::traceRayTF2DV2(const Ray& ray, float sampleStep) const
{
    glm::vec3 samplePos = ray.origin + ray.tmax * ray.direction;
    const glm::vec3 increment = sampleStep * ray.direction;

    glm::vec3 color(0.0f);

    for (float t = ray.tmax; t >= ray.tmin; t -= sampleStep, samplePos -= increment) {
        float intensity = this->m_pVolume->getVoxelInterpolate(samplePos);
        auto gradient = this->m_pGradientVolume->getGradientVoxel(samplePos);
        float opacity = this->getTF2DV2Opacity(intensity, gradient.magnitude);

        auto _color = this->getTF2DV2Color(intensity, gradient.magnitude);

        opacity *= _color.w;

            color = opacity * glm::vec3(_color) + (1 - opacity) * color;
    }

    return glm::vec4(color, 1.0f);
}

// TODO comment
float Renderer::getTF2DV2Opacity(float val, float gradientMagnitude) const
{

    bool inTriangle_0 = inTriangle(
        this->m_config.TF2DV2Intensity_0 - this->m_config.TF2DV2Radius_0,
        this->m_config.TF2DV2Intensity_0,
        this->m_config.TF2DV2Intensity_0 + this->m_config.TF2DV2Radius_0,
        val,
        gradientMagnitude);

    bool inTriangle_1 = inTriangle(
        this->m_config.TF2DV2Intensity_1 - this->m_config.TF2DV2Radius_1,
        this->m_config.TF2DV2Intensity_1,
        this->m_config.TF2DV2Intensity_1 + this->m_config.TF2DV2Radius_1,
        val,
        gradientMagnitude);

    if (inTriangle_0) { // inside triangle_0
        // at this moment triangleGradientBoundary is the point above intensity where the triangle gets intersected
        return linearOpacity(this->m_config.TF2DV2Intensity_0, this->m_config.TF2DV2Radius_0, val, gradientMagnitude);
        // return 1;
    } else if (inTriangle_1) { // inside triangle_1
        // at this moment triangleGradientBoundary is the point above intensity where the triangle gets intersected
        return linearOpacity(this->m_config.TF2DV2Intensity_1, this->m_config.TF2DV2Radius_1, val, gradientMagnitude);
        // return 1;
    } else if (inTriangle_0 && inTriangle_1) {
        return std::max(
            linearOpacity(this->m_config.TF2DV2Intensity_0, this->m_config.TF2DV2Radius_0, val, gradientMagnitude),
            linearOpacity(this->m_config.TF2DV2Intensity_1, this->m_config.TF2DV2Radius_1, val, gradientMagnitude));
    }

    return 0.0f;
}

// TODO comment
glm::vec4 Renderer::getTF2DV2Color(float val, float gradientMagnitude) const
{
    bool inTriangle_0 = inTriangle(
        this->m_config.TF2DV2Intensity_0 - this->m_config.TF2DV2Radius_0,
        this->m_config.TF2DV2Intensity_0,
        this->m_config.TF2DV2Intensity_0 + this->m_config.TF2DV2Radius_0,
        val,
        gradientMagnitude);

    bool inTriangle_1 = inTriangle(
        this->m_config.TF2DV2Intensity_1 - this->m_config.TF2DV2Radius_1,
        this->m_config.TF2DV2Intensity_1,
        this->m_config.TF2DV2Intensity_1 + this->m_config.TF2DV2Radius_1,
        val,
        gradientMagnitude);

    if (inTriangle_0) { // inside triangle_0
        // at this moment triangleGradientBoundary is the point above intensity where the triangle gets intersected
        return this->m_config.TF2DV2Color_0;
        // return 1;
    } else if (inTriangle_1) { // inside triangle_1
        // at this moment triangleGradientBoundary is the point above intensity where the triangle gets intersected
        return this->m_config.TF2DV2Color_1;
        // return 1;
    } else if (inTriangle_0 && inTriangle_1) {
        if (linearOpacity(this->m_config.TF2DV2Intensity_0, this->m_config.TF2DV2Radius_0, val, gradientMagnitude) > linearOpacity(this->m_config.TF2DV2Intensity_1, this->m_config.TF2DV2Radius_1, val, gradientMagnitude)) {
            return this->m_config.TF2DV2Color_0;
        } else {
            return this->m_config.TF2DV2Color_1;
        }
    }

    return glm::vec4(0, 0, 0, 0);
}
}