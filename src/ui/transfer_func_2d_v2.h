#pragma once
#include "render/render_config.h"
#include "volume/gradient_volume.h"
#include "volume/volume.h"
#include <GL/glew.h> // Include before glfw3
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace ui {

class TransferFunction2DV2Widget {
public:
    TransferFunction2DV2Widget(const volume::Volume& volume, const volume::GradientVolume& gradient);

    void draw();
    void updateRenderConfig(render::RenderConfig& renderConfig);

private:
    float m_intensity_0, m_intensity_1, m_maxIntensity;
    float m_radius_0, m_radius_1;
    glm::vec4 m_color_0, m_color_1;

    int m_interactingPoint;
    GLuint m_histogramImg;
};
}
