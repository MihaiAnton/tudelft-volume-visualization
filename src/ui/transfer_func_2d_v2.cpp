#include "transfer_func_2d_v2.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <glm/common.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>
#include <imgui.h>
#include <iostream>
#include <vector>

static ImVec2 glmToIm(const glm::vec2& v);
static glm::vec2 ImToGlm(const ImVec2& v);
static std::vector<glm::vec4> createHistogramImage(
    const volume::Volume& volume, const volume::GradientVolume& gradient, const glm::ivec2& res);

namespace ui {

// Radius of the three points in the histogram image.
static constexpr float pointRadius = 8.0f;
static constexpr glm::ivec2 widgetSize { 475, 300 };

TransferFunction2DV2Widget::TransferFunction2DV2Widget(const volume::Volume& volume, const volume::GradientVolume& gradient)
    : m_intensity_0(68.0f)
    , m_intensity_1(100.0f)
    , m_maxIntensity(volume.maximum())
    , m_radius_0(38.0f)
    , m_radius_1(20.0f)
    , m_color_0(0.0f, 0.8f, 0.6f, 0.3f)
    , m_color_1(0.0f, 0.8f, 0.6f, 0.3f)
    , m_interactingPoint(-1)
    , m_histogramImg(0)
{
    const glm::ivec2 res = glm::ivec2(volume.maximum(), gradient.maxMagnitude() + 1);
    const auto imgData = createHistogramImage(volume, gradient, res);

    glGenTextures(1, &m_histogramImg);
    glBindTexture(GL_TEXTURE_2D, m_histogramImg);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, m_histogramImg);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, res.x, res.y, 0, GL_RGBA, GL_FLOAT, imgData.data());
}

// Draw the widget and handle interactions
void TransferFunction2DV2Widget::draw()
{
    const ImGuiIO& io = ImGui::GetIO();

    ImGui::Text("2D Transfer Function V2");
    ImGui::Text("Click and drag points to alter the m_radius or m_intensity");

    // Histogram image is positioned to the right of the content region.
    const glm::vec2 canvasSize { widgetSize.x, widgetSize.y - 20 };
    glm::vec2 canvasPos = ImToGlm(ImGui::GetCursorScreenPos()); // this is the imgui draw cursor, not mouse cursor
    const float xOffset = (ImToGlm(ImGui::GetContentRegionAvail()).x - canvasSize.x);
    canvasPos.x += xOffset; // center widget

    // Draw side text (imgui cannot center-align text so we have to do it ourselves).
    ImVec2 cursorPos = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(cursorPos.x + 25, cursorPos.y + canvasSize.y / 2 - 20 - ImGui::GetFontSize()));
    //ImGui::Text("");
    ImGui::Separator();
    ImGui::SetCursorPos(ImVec2(cursorPos.x + 15, cursorPos.y + canvasSize.y / 2 - 20));
    ImGui::Text("Gradient");
    ImGui::SetCursorPos(ImVec2(cursorPos.x + 5, cursorPos.y + canvasSize.y / 2 - 20 + ImGui::GetFontSize()));
    ImGui::Text("Magnitude");
    ImGui::SetCursorPos(cursorPos);

    // Draw box and histogram image.
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(glmToIm(canvasPos), glmToIm(canvasPos + canvasSize));
    drawList->AddRect(glmToIm(canvasPos), glmToIm(canvasPos + canvasSize), ImColor(180, 180, 180, 255));

    cursorPos = ImVec2(ImGui::GetCursorPosX() + xOffset, ImGui::GetCursorPosY());

    // Draw histogram image that we uploaded to the GPU using OpenGL.
    ImGui::SetCursorPos(cursorPos);

    // NOTE(Mathijs): reinterpret casting the pointer is undefined behavior according to the standard.
    // Use memcpy for now and move to std::bit_cast (C++20) when more compilers support it.
    //
    // From the standard:
    // When it is needed to interpret the bytes of an object as a value of a different type, std::memcpy or std::bit_cast (since C++20)can be used:
    // https://en.cppreference.com/w/cpp/language/reinterpret_cast
    ImTextureID imguiTexture;
    std::memcpy(&imguiTexture, &m_histogramImg, sizeof(m_histogramImg));
    ImGui::Image(imguiTexture, glmToIm(canvasSize - glm::vec2(1)));

    // Detect and handle mouse interaction.
    if (!io.MouseDown[0] && !io.MouseDown[1]) {
        m_interactingPoint = -1;
    }

    // Place an invisible button on top of the histogram. IsItemHovering returns whether the cursor is
    // hovering over the last added item which in this case is the invisble button. This way we can
    // easily detect whether the cursor is inside the histogram image.
    ImGui::SetCursorPos(cursorPos);
    ImGui::InvisibleButton("tfn_canvas", glmToIm(canvasSize));

    // Mouse position within the histogram image.
    const ImVec2 bbMin = ImGui::GetItemRectMin();
    const ImVec2 bbMax = ImGui::GetItemRectMax();
    const glm::vec2 clippedMousePos {
        std::min(std::max(io.MousePos.x, bbMin.x), bbMax.x),
        std::min(std::max(io.MousePos.y, bbMin.y), bbMax.y)
    };

    const float normalizedIntensity_0 = m_intensity_0 / m_maxIntensity;
    const float normalizedRadius_0 = m_radius_0 / m_maxIntensity;
    const std::array points_0 = {
        glm::vec2(normalizedIntensity_0, 0.f),
        glm::vec2(normalizedIntensity_0 - normalizedRadius_0, 1.f),
        glm::vec2(normalizedIntensity_0 + normalizedRadius_0, 1.f)
    };

    const float normalizedIntensity_1 = m_intensity_1 / m_maxIntensity;
    const float normalizedRadius_1 = m_radius_1 / m_maxIntensity;
    const std::array points_1 = {
        glm::vec2(normalizedIntensity_1, 0.f),
        glm::vec2(normalizedIntensity_1 - normalizedRadius_1, 1.f),
        glm::vec2(normalizedIntensity_1 + normalizedRadius_1, 1.f)
    };

    const glm::vec2 viewScale { canvasSize.x, -canvasSize.y };
    const glm::vec2 viewOffset { canvasPos.x, canvasPos.y + canvasSize.y };
    if (ImGui::IsItemHovered() && (io.MouseDown[0] || io.MouseDown[1])) {
        const glm::vec2 mousePos = glm::clamp((clippedMousePos - viewOffset) / viewScale, 0.0f, 1.0f);

        if (io.MouseDown[0]) {
            // Left mouse button is down.
            if (m_interactingPoint == -1) {
                // No point is currently selected; check if the user clicked any of the points.
                for (size_t i = 0; i < points_0.size(); i++) {
                    const glm::vec2 pointPosition = points_0[i] * viewScale + viewOffset;
                    const glm::vec2 diff = pointPosition - clippedMousePos;
                    float distanceSquared = glm::dot(diff, diff);
                    if (distanceSquared < pointRadius * pointRadius) {
                        m_interactingPoint = int(i);
                        break;
                    }
                }
                for (size_t i = 0; i < points_1.size(); i++) {
                    const glm::vec2 pointPosition = points_1[i] * viewScale + viewOffset;
                    const glm::vec2 diff = pointPosition - clippedMousePos;
                    float distanceSquared = glm::dot(diff, diff);
                    if (distanceSquared < pointRadius * pointRadius) {
                        m_interactingPoint = int(i + points_0.size());
                        break;
                    }
                }
            }

            if (m_interactingPoint != -1) {
                // A point was already selected.
                switch (m_interactingPoint) {
                case 0: {
                    // m_intensity_0 point
                    m_intensity_0 = mousePos.x * m_maxIntensity;
                } break;
                case 1: {
                    // left m_radius point
                    m_radius_0 = std::max(m_intensity_0 - mousePos.x * m_maxIntensity, 1.0f);
                } break;
                case 2: {
                    // right m_radius point
                    m_radius_0 = std::max(mousePos.x * m_maxIntensity - m_intensity_0, 1.0f);
                } break;
                case 3: {
                    // m_intensity_0 point
                    m_intensity_1 = mousePos.x * m_maxIntensity;
                } break;
                case 4: {
                    // left m_radius point
                    m_radius_1 = std::max(m_intensity_1 - mousePos.x * m_maxIntensity, 1.0f);
                } break;
                case 5: {
                    // right m_radius point
                    m_radius_1 = std::max(mousePos.x * m_maxIntensity - m_intensity_1, 1.0f);
                } break;
                };
            }
        }
    }

    // Draw m_intensity_0 and m_radius points.
    const ImVec2 leftRadiusPointPos_0 = glmToIm(glm::vec2(normalizedIntensity_0 - normalizedRadius_0, 1.f) * viewScale + viewOffset);
    const ImVec2 rightRadiusPointPos_0 = glmToIm(glm::vec2(normalizedIntensity_0 + normalizedRadius_0, 1.f) * viewScale + viewOffset);
    const ImVec2 intensityPointPos_0 = glmToIm(glm::vec2(normalizedIntensity_0, 0.f) * viewScale + viewOffset);

    const ImVec2 leftRadiusPointPos_1 = glmToIm(glm::vec2(normalizedIntensity_1 - normalizedRadius_1, 1.f) * viewScale + viewOffset);
    const ImVec2 rightRadiusPointPos_1 = glmToIm(glm::vec2(normalizedIntensity_1 + normalizedRadius_1, 1.f) * viewScale + viewOffset);
    const ImVec2 intensityPointPos_1 = glmToIm(glm::vec2(normalizedIntensity_1, 0.f) * viewScale + viewOffset);

    drawList->AddLine(leftRadiusPointPos_0, intensityPointPos_0, 0xFFFFFFFF);
    drawList->AddLine(intensityPointPos_0, rightRadiusPointPos_0, 0xFFFFFFFF);

    drawList->AddLine(leftRadiusPointPos_1, intensityPointPos_1, 0xFFFFFFFF);
    drawList->AddLine(intensityPointPos_1, rightRadiusPointPos_1, 0xFFFFFFFF);

    drawList->AddCircleFilled(leftRadiusPointPos_0, pointRadius, 0xFFFFFFFF);
    drawList->AddCircleFilled(intensityPointPos_0, pointRadius, 0xFFFFFFFF);
    drawList->AddCircleFilled(rightRadiusPointPos_0, pointRadius, 0xFFFFFFFF);

    drawList->AddCircleFilled(leftRadiusPointPos_1, pointRadius, 0xFFFFFFFF);
    drawList->AddCircleFilled(intensityPointPos_1, pointRadius, 0xFFFFFFFF);
    drawList->AddCircleFilled(rightRadiusPointPos_1, pointRadius, 0xFFFFFFFF);

    drawList->PopClipRect();

    // Bottom text.
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + xOffset + canvasSize.x / 2 - 40);
    ImGui::Text("Voxel Value");

    // Draw text boxes and color picker.
    ImGui::NewLine();
    int inputFill = 0;
    ImGui::PushItemWidth(0.1f); // These 3 coming lines show nothing but vertically align the text of the intensity and radius boxes
    ImGui::InputScalar("", ImGuiDataType_U32, &inputFill);
    ImGui::SameLine();
    ImGui::Text("Intensity: ");
    ImGui::SameLine();
    ImGui::PushItemWidth(50.f);
    ImGui::InputScalar("", ImGuiDataType_Float, &m_intensity_0, NULL, NULL, "%.2f", ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    ImGui::Text("Radius: ");
    ImGui::SameLine();
    ImGui::PushItemWidth(50.f);
    ImGui::InputScalar("", ImGuiDataType_Float, &m_radius_0, NULL, NULL, "%.2f", ImGuiInputTextFlags_ReadOnly);

    ImGui::NewLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + xOffset / 2);
    ImGui::PushItemWidth(ImGui::GetContentRegionAvailWidth() * 0.4f);
    ImGui::ColorPicker4("Color1", glm::value_ptr(m_color_0));

    ImGui::NewLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + xOffset / 2);
    ImGui::PushItemWidth(ImGui::GetContentRegionAvailWidth() * 0.4f);
    ImGui::ColorPicker4("Color2", glm::value_ptr(m_color_1));
}

void TransferFunction2DV2Widget::updateRenderConfig(render::RenderConfig& renderConfig)
{
    renderConfig.TF2DColor = m_color_0;

    renderConfig.TF2DV2Intensity_0 = m_intensity_0;
    renderConfig.TF2DV2Radius_0 = m_radius_0;
    renderConfig.TF2DV2Color_0 = m_color_0;

    renderConfig.TF2DV2Intensity_1 = m_intensity_1;
    renderConfig.TF2DV2Radius_1 = m_radius_1;
    renderConfig.TF2DV2Color_1 = m_color_1;
}
}

static ImVec2 glmToIm(const glm::vec2& v)
{
    return ImVec2(v.x, v.y);
}

static glm::vec2 ImToGlm(const ImVec2& v)
{
    return glm::vec2(v.x, v.y);
}

// Compute a histogram texture from the volume and gradient data
static std::vector<glm::vec4> createHistogramImage(
    const volume::Volume& volume, const volume::GradientVolume& gradient, const glm::ivec2& res)
{
    const size_t numPixels = static_cast<size_t>(res.x * res.y);
    std::vector<int> bins(numPixels, 0);
    for (int z = 0; z < volume.dims().z; z++) {
        for (int y = 0; y < volume.dims().y; y++) {
            for (int x = 0; x < volume.dims().x; x++) {
                const size_t imgX = static_cast<size_t>(volume.getVoxel(x, y, z));
                const size_t imgY = static_cast<size_t>(res.y - 1) - static_cast<size_t>(gradient.getGradientVoxel(x, y, z).magnitude);
                const size_t index = imgX + imgY * static_cast<size_t>(res.x);
                bins[index]++;
            }
        }
    }

    const int maxCount = *std::max_element(std::begin(bins), std::end(bins));
    const float factor = 1.0f / std::log(float(maxCount));

    std::vector<glm::vec4> imageData(numPixels, glm::vec4(0.0f));
    std::transform(std::begin(bins), std::end(bins), std::begin(imageData),
        [&](int count) {
            return glm::vec4(1.0f, 1.0f, 1.0f, std::log(float(count)) * factor);
        });
    return imageData;
}
