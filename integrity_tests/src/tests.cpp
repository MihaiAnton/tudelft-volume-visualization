// Can access the header files from the viewer...
#include "test_classes.h"
#include "ui/window.h"
#include <algorithm>
#include <catch2/catch.hpp>
#include <glm/gtc/type_ptr.hpp>

/*
GradientVolume:
    - linearInterpolate
    - getGradientVoxelLinearInterpolate

Volume:
    - weight
    - cubicInterpolate
    - bicubicInterpolate
    - getVoxelTriCubicInterpolate

Renderer:
    - compositeRender : m_pVolume, m_pGradientVolume, m_pCamera, m_config.volumeShading
    - TF2DRender : m_pVolume, m_pGradientVolume, m_pCamera, m_config.volumeShading, m_config.TF2DColor
    - isoRender : m_pVolume, m_pGradientVolume, m_pCamera, m_config.isoValue
    - bisectionAccuracy : m_pVolume
    - computePhongShading
    - getTF2DOpacity : m_pVolume, m_pGradientVolume, m_config.TF2DRadius, m_config.TF2DIntensity 
*/

TEST_CASE("Volume Tests")
{
    REQUIRE_NOTHROW(TestVolume::test_weight(0.f));
    REQUIRE_NOTHROW(TestVolume::test_cubicInterpolate(0.f, 0.f, 0.f, 0.f, 0.f));

    const TestVolume volume { std::vector<uint16_t>(125, 0), glm::ivec3(5) };
    REQUIRE_NOTHROW(volume.test_linearInterpolate(0.0f, 1.0f, 0.5f));
    REQUIRE_NOTHROW(volume.test_getVoxelLinearInterpolate(glm::vec3(2.5f)));
    REQUIRE_NOTHROW(volume.test_bicubicInterpolateXY(glm::vec3(2.5f), 2));
    REQUIRE_NOTHROW(volume.test_getVoxelTriCubicInterpolate(glm::vec3(2.5f)));
}

TEST_CASE("Gradient Volume Tests")
{
    volume::GradientVoxel gv = { glm::vec3(1.f, 0.f, 0.f), 1.f };
    REQUIRE_NOTHROW(TestGradientVolume::test_linearInterpolate(gv, gv, 0.0f));

    const volume::Volume volume = volume::Volume(std::vector<uint16_t> { 1 }, glm::ivec3(1));
    const TestGradientVolume gradient { volume };
    REQUIRE_NOTHROW(gradient.test_getGradientVoxelLinearInterpolate(glm::vec3(100.f)));
}
