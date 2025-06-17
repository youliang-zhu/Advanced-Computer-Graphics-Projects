#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <string>

struct FlowFieldBounds
{
    glm::vec3 worldMin;
    glm::vec3 worldMax;
    glm::vec3 worldSize;
    glm::ivec3 resolution;

    FlowFieldBounds() = default;
    FlowFieldBounds(const glm::vec3& min_bounds, const glm::vec3& max_bounds, const glm::ivec3& res)
        : worldMin(min_bounds), worldMax(max_bounds), resolution(res)
    {
        worldSize = worldMax - worldMin;
    }
};

struct UniformWindParameters
{
    glm::vec3 windDirection = glm::vec3(0.0f, 1.0f, 0.0f);
    float windStrength = 2.0f;
    float timeScale = 1.0f;
    float heightVariation = 0.5f;  // Height Variation Coefficient
    float padding[2] = { 0.0f, 0.0f }; // ¶ÔÆë

    UniformWindParameters() = default;
    UniformWindParameters(const glm::vec3& dir, float strength, float time_scale = 1.0f)
        : windDirection(dir), windStrength(strength), timeScale(time_scale) {
    }
};

class FlowFieldGPU
{
public:
    FlowFieldGPU();
    ~FlowFieldGPU();

    /// Initialize 3D textures and compute shaders
    bool initialize(const glm::ivec3& resolution = glm::ivec3(64, 64, 64),
        const glm::vec3& worldMin = glm::vec3(-50.0f, 0.0f, -50.0f),
        const glm::vec3& worldMax = glm::vec3(50.0f, 100.0f, 50.0f));

    /// Generate UNIFORM_WIND flow field data
    void generateUniformWind(const UniformWindParameters& params, float currentTime);

    /// Update flow field
    void updateFlowField(float currentTime);

    /// Get 3D texture handle
    GLuint getFlowFieldTexture() const { return flowFieldTexture3D; }

    /// Get flow field boundary information
    const FlowFieldBounds& getBounds() const { return bounds; }

    /// Set wind farm parameters
    void setWindParameters(const UniformWindParameters& params);

    /// Get current wind farm parameters
    const UniformWindParameters& getWindParameters() const { return windParams; }

    /// Check if it is initialized
    bool isInitialized() const { return initialized; }

    /// Enable/disable flow field
    void setEnabled(bool enabled) { flowFieldEnabled = enabled; }
    bool isEnabled() const { return flowFieldEnabled; }

    void cleanup();

private:
    GLuint flowFieldTexture3D;
    GLuint flowFieldComputeShader;

    FlowFieldBounds bounds;
    UniformWindParameters windParams;

    bool initialized;
    bool flowFieldEnabled;
    float lastUpdateTime;

    /// Compile the flow field to generate the compute shader
    bool loadFlowFieldComputeShader(const std::string& filepath);

    GLuint compileComputeShader(const std::string& source);

    std::string readFile(const std::string& filepath);

    void checkGLError(const std::string& operation);
};