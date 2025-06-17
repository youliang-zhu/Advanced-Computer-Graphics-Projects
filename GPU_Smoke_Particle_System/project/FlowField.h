#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <memory>

enum class FlowFieldType
{
    UNIFORM_WIND,
    VORTEX,  
    UPWARD_FLOW,
    TURBULENT, 
    CUSTOM_GRID
};

struct FlowFieldParameters
{
    glm::vec3 wind_direction = glm::vec3(1.0f, 0.0f, 0.0f);
    float wind_strength = 2.0f;

    float vortex_strength = 3.0f; 
    glm::vec3 vortex_center = glm::vec3(0.0f, 20.0f, 0.0f);
    glm::vec3 vortex_axis = glm::vec3(0.0f, 1.0f, 0.0f);

    float upward_strength = 2.5f; 

    float turbulence_scale = 0.1f;
    float turbulence_strength = 1.0f;

    float time_scale = 1.0f;
};

struct GridFlowField
{
    std::vector<glm::vec3> velocities;
    glm::ivec3 resolution;
    glm::vec3 bounds_min;
    glm::vec3 bounds_max; 
    glm::vec3 cell_size;

    GridFlowField() = default;
    GridFlowField(const glm::ivec3& res, const glm::vec3& min_bounds, const glm::vec3& max_bounds);

    /// Get the grid index
    int getIndex(int x, int y, int z) const;

    /// Check if the index is valid
    bool isValidIndex(int x, int y, int z) const;

    /// Set the grid point speed
    void setVelocity(int x, int y, int z, const glm::vec3& velocity);

    /// Get the grid point velocity
    glm::vec3 getVelocity(int x, int y, int z) const;
};

class FlowField
{
public:

    FlowField();
    ~FlowField() = default;

    /// Initialize the flow system
    void initialize(const glm::vec3& bounds_min, const glm::vec3& bounds_max, const glm::ivec3& grid_resolution = glm::ivec3(32, 32, 32));

    /// Get the flow field velocity at the specified position and time
    glm::vec3 getVelocityAt(const glm::vec3& position, float time) const;

    /// Set the current flow field type
    void setFlowFieldType(FlowFieldType type) { currentType = type; }

    /// Get the current flow field type
    FlowFieldType getFlowFieldType() const { return currentType; }

    /// Setting flow field parameters
    void setParameters(const FlowFieldParameters& params) { flowParams = params; }

    /// Get flow field parameters
    const FlowFieldParameters& getParameters() const { return flowParams; }

    /// Update mesh flow field (if using custom flow field)
    void updateGridFlowField(float time);

    /// Generate mesh data based on a simple flow field
    void generateGridFromSimpleFlow(FlowFieldType sourceType, float time);

    /// Set the global scaling of flow field intensity
    void setGlobalStrength(float strength) { globalStrength = strength; }

    /// Get global flow field strength
    float getGlobalStrength() const { return globalStrength; }

private:
    FlowFieldType currentType;
    FlowFieldParameters flowParams;
    float globalStrength;

    // Grid data
    std::unique_ptr<GridFlowField> gridField;

    // Flow field boundary
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;

    /// Calculate uniform wind speed
    glm::vec3 calculateUniformWind(const glm::vec3& position, float time) const;

    /// Calculate eddy current velocity
    glm::vec3 calculateVortexFlow(const glm::vec3& position, float time) const;

    /// Calculating updraft velocity
    glm::vec3 calculateUpwardFlow(const glm::vec3& position, float time) const;

    /// Calculate turbulent velocity
    glm::vec3 calculateTurbulentFlow(const glm::vec3& position, float time) const;

    /// Get interpolated velocity from a grid
    glm::vec3 getGridVelocity(const glm::vec3& position) const;

    /// Trilinear interpolation
    glm::vec3 trilinearInterpolate(const glm::vec3& position) const;

    /// Simple noise function (for turbulence)
    float noise3D(const glm::vec3& p) const;

    /// Convert world coordinates to grid coordinates
    glm::vec3 worldToGrid(const glm::vec3& worldPos) const;

    /// Check if the location is within the bounds
    bool isInBounds(const glm::vec3& position) const;
};