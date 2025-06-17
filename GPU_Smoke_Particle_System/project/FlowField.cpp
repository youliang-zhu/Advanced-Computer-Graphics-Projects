#include "FlowField.h"
#include <cmath>
#include <algorithm>

///////////////////////////////////////////////////////////////////////
// Grid attributes settings
///////////////////////////////////////////////////////////////////////

GridFlowField::GridFlowField(const glm::ivec3& res, const glm::vec3& min_bounds, const glm::vec3& max_bounds)
    : resolution(res), bounds_min(min_bounds), bounds_max(max_bounds)
{
    velocities.resize(resolution.x * resolution.y * resolution.z, glm::vec3(0.0f));
    cell_size = (bounds_max - bounds_min) / glm::vec3(resolution - 1);
}

int GridFlowField::getIndex(int x, int y, int z) const
{
    return z * (resolution.x * resolution.y) + y * resolution.x + x;
}

bool GridFlowField::isValidIndex(int x, int y, int z) const
{
    return x >= 0 && x < resolution.x &&
        y >= 0 && y < resolution.y &&
        z >= 0 && z < resolution.z;
}

void GridFlowField::setVelocity(int x, int y, int z, const glm::vec3& velocity)
{
    if (isValidIndex(x, y, z)) 
    {
        velocities[getIndex(x, y, z)] = velocity;
    }
}

glm::vec3 GridFlowField::getVelocity(int x, int y, int z) const
{
    if (isValidIndex(x, y, z)) 
    {
        return velocities[getIndex(x, y, z)];
    }
    return glm::vec3(0.0f);
}

///////////////////////////////////////////////////////////////////////
// Calculate different flowFields 
///////////////////////////////////////////////////////////////////////
FlowField::FlowField()
    : currentType(FlowFieldType::UPWARD_FLOW), globalStrength(1.0f)
{
}

void FlowField::initialize(const glm::vec3& bounds_min, const glm::vec3& bounds_max, const glm::ivec3& grid_resolution)
{
    boundsMin = bounds_min;
    boundsMax = bounds_max;

    // Creating Grid
    gridField = std::make_unique<GridFlowField>(grid_resolution, bounds_min, bounds_max);

    // Initialize to updraft
    generateGridFromSimpleFlow(FlowFieldType::UPWARD_FLOW, 0.0f);
}

glm::vec3 FlowField::getVelocityAt(const glm::vec3& position, float time) const
{
    if (!isInBounds(position)) 
    {
        return glm::vec3(0.0f);
    }

    glm::vec3 velocity(0.0f);

    switch (currentType) 
    {
    case FlowFieldType::UNIFORM_WIND:
        velocity = calculateUniformWind(position, time);
        break;
    case FlowFieldType::VORTEX:
        velocity = calculateVortexFlow(position, time);
        break;
    case FlowFieldType::UPWARD_FLOW:
        velocity = calculateUpwardFlow(position, time);
        break;
    case FlowFieldType::TURBULENT:
        velocity = calculateTurbulentFlow(position, time);
        break;
    case FlowFieldType::CUSTOM_GRID:
        velocity = getGridVelocity(position);
        break;
    }

    return velocity * globalStrength;
}

glm::vec3 FlowField::calculateUniformWind(const glm::vec3& position, float time) const
{
    glm::vec3 wind = flowParams.wind_direction * flowParams.wind_strength;
    float timeVariation = 1.0f + 0.1f * sin(time * flowParams.time_scale);

    return wind * timeVariation;
}

glm::vec3 FlowField::calculateVortexFlow(const glm::vec3& position, float time) const
{
    glm::vec3 centerPos = flowParams.vortex_center;
    glm::vec3 axis = normalize(flowParams.vortex_axis);

    // Calculate the vector from the vortex center to the particle
    glm::vec3 toParticle = position - centerPos;

    // Calculate the distance from the vortex axis
    float axisDistance = length(toParticle - dot(toParticle, axis) * axis);

    if (axisDistance < 0.01f) 
    {
        return glm::vec3(0.0f); // Avoiding singularities on axes
    }

    // Calculate velocity components
    glm::vec3 radial = normalize(toParticle - dot(toParticle, axis) * axis);
    glm::vec3 tangential = cross(axis, radial);

    // The eddy current strength decays with distance
    float strength = flowParams.vortex_strength / (1.0f + axisDistance * 0.1f);

    // Adding time changes
    float timeVariation = 1.0f + 0.2f * sin(time * flowParams.time_scale * 2.0f);

    return tangential * strength * timeVariation;
}

glm::vec3 FlowField::calculateUpwardFlow(const glm::vec3& position, float time) const
{
    glm::vec3 upward = glm::vec3(0.0f, flowParams.upward_strength, 0.0f);

    // Adding altitude-based intensity variation
    float heightFactor = (position.y - boundsMin.y) / (boundsMax.y - boundsMin.y);
    heightFactor = glm::clamp(heightFactor, 0.0f, 1.0f);
    float heightMultiplier = 1.0f - heightFactor * 0.5f;

    // Add slight lateral disturbance
    float xNoise = noise3D(position * 0.05f + glm::vec3(time * 0.5f, 0, 0));
    float zNoise = noise3D(position * 0.05f + glm::vec3(0, time * 0.5f, 0));

    glm::vec3 disturbance = glm::vec3(xNoise, 0.0f, zNoise) * 0.5f;

    return (upward * heightMultiplier + disturbance) * (1.0f + 0.1f * sin(time * flowParams.time_scale));
}

glm::vec3 FlowField::calculateTurbulentFlow(const glm::vec3& position, float time) const
{
    glm::vec3 turbulence(0.0f);

    float scale = flowParams.turbulence_scale;
    float strength = flowParams.turbulence_strength;

    // Multi-layer noise
    for (int i = 0; i < 3; ++i) 
    {
        float currentScale = scale * pow(2.0f, i);
        float currentStrength = strength / pow(2.0f, i);

        glm::vec3 samplePos = position * currentScale + glm::vec3(time * flowParams.time_scale);

        turbulence.x += noise3D(samplePos) * currentStrength;
        turbulence.y += noise3D(samplePos + glm::vec3(100.0f, 0.0f, 0.0f)) * currentStrength;
        turbulence.z += noise3D(samplePos + glm::vec3(0.0f, 100.0f, 0.0f)) * currentStrength;
    }

    return turbulence;
}


///////////////////////////////////////////////////////////////////////
// Calculate flowFields velocity
///////////////////////////////////////////////////////////////////////

glm::vec3 FlowField::getGridVelocity(const glm::vec3& position) const
{
    if (!gridField) 
    {
        return glm::vec3(0.0f);
    }

    return trilinearInterpolate(position);
}

glm::vec3 FlowField::trilinearInterpolate(const glm::vec3& position) const
{
    // Convert to grid coordinates
    glm::vec3 gridPos = worldToGrid(position);

    // Get the grid index
    glm::ivec3 index0 = glm::ivec3(floor(gridPos));
    glm::ivec3 index1 = index0 + glm::ivec3(1);

    // Bounds Checking
    index0 = glm::max(index0, glm::ivec3(0));
    index1 = glm::min(index1, gridField->resolution - 1);

    // Calculate interpolation weights
    glm::vec3 weight = gridPos - glm::vec3(index0);
    weight = glm::clamp(weight, 0.0f, 1.0f);

    // Get the speed of 8 vertices
    glm::vec3 v000 = gridField->getVelocity(index0.x, index0.y, index0.z);
    glm::vec3 v100 = gridField->getVelocity(index1.x, index0.y, index0.z);
    glm::vec3 v010 = gridField->getVelocity(index0.x, index1.y, index0.z);
    glm::vec3 v110 = gridField->getVelocity(index1.x, index1.y, index0.z);
    glm::vec3 v001 = gridField->getVelocity(index0.x, index0.y, index1.z);
    glm::vec3 v101 = gridField->getVelocity(index1.x, index0.y, index1.z);
    glm::vec3 v011 = gridField->getVelocity(index0.x, index1.y, index1.z);
    glm::vec3 v111 = gridField->getVelocity(index1.x, index1.y, index1.z);

    // Trilinear interpolation
    glm::vec3 v00 = mix(v000, v100, weight.x);
    glm::vec3 v10 = mix(v010, v110, weight.x);
    glm::vec3 v01 = mix(v001, v101, weight.x);
    glm::vec3 v11 = mix(v011, v111, weight.x);

    glm::vec3 v0 = mix(v00, v10, weight.y);
    glm::vec3 v1 = mix(v01, v11, weight.y);

    return mix(v0, v1, weight.z);
}

void FlowField::updateGridFlowField(float time)
{
    if (!gridField) return;

    // 使用当前流场类型更新网格
    for (int z = 0; z < gridField->resolution.z; ++z) 
    {
        for (int y = 0; y < gridField->resolution.y; ++y) 
        {
            for (int x = 0; x < gridField->resolution.x; ++x) 
            {
                // Calculate the world coordinates of the grid points
                glm::vec3 worldPos = gridField->bounds_min +
                    glm::vec3(x, y, z) * gridField->cell_size;

                // Calculate the flow velocity at this point
                FlowFieldType originalType = currentType;
                currentType = FlowFieldType::UPWARD_FLOW;
                glm::vec3 velocity = getVelocityAt(worldPos, time);
                currentType = originalType;

                gridField->setVelocity(x, y, z, velocity);
            }
        }
    }
}

void FlowField::generateGridFromSimpleFlow(FlowFieldType sourceType, float time)
{
    if (!gridField) return;

    FlowFieldType originalType = currentType;
    currentType = sourceType;

    for (int z = 0; z < gridField->resolution.z; ++z) {
        for (int y = 0; y < gridField->resolution.y; ++y) {
            for (int x = 0; x < gridField->resolution.x; ++x) {
                glm::vec3 worldPos = gridField->bounds_min +
                    glm::vec3(x, y, z) * gridField->cell_size;

                glm::vec3 velocity(0.0f);
                switch (sourceType) 
                {
                case FlowFieldType::UNIFORM_WIND:
                    velocity = calculateUniformWind(worldPos, time);
                    break;
                case FlowFieldType::VORTEX:
                    velocity = calculateVortexFlow(worldPos, time);
                    break;
                case FlowFieldType::UPWARD_FLOW:
                    velocity = calculateUpwardFlow(worldPos, time);
                    break;
                case FlowFieldType::TURBULENT:
                    velocity = calculateTurbulentFlow(worldPos, time);
                    break;
                default:
                    velocity = glm::vec3(0.0f);
                    break;
                }

                gridField->setVelocity(x, y, z, velocity);
            }
        }
    }

    currentType = originalType;
}

float FlowField::noise3D(const glm::vec3& p) const
{
    // Simple pseudo-random noise function
    glm::vec3 i = floor(p);
    glm::vec3 f = p - i;

    f = f * f * (3.0f - 2.0f * f);

    // Hash Functions
    auto hash = [](float n) -> float 
        {
        return sin(n) * 43758.5453f - floor(sin(n) * 43758.5453f);
        };

    float n = i.x + i.y * 57.0f + i.z * 113.0f;

    float a = hash(n);
    float b = hash(n + 1.0f);
    float c = hash(n + 57.0f);
    float d = hash(n + 58.0f);
    float e = hash(n + 113.0f);
    float f_val = hash(n + 114.0f);
    float g = hash(n + 170.0f);
    float h = hash(n + 171.0f);

    float k0 = glm::mix(a, b, f.x);
    float k1 = glm::mix(c, d, f.x);
    float k2 = glm::mix(e, f_val, f.x);
    float k3 = glm::mix(g, h, f.x);

    float k4 = glm::mix(k0, k1, f.y);
    float k5 = glm::mix(k2, k3, f.y);

    return glm::mix(k4, k5, f.z) * 2.0f - 1.0f; // Normalized to[-1,1]
}

glm::vec3 FlowField::worldToGrid(const glm::vec3& worldPos) const
{
    if (!gridField) return glm::vec3(0.0f);

    return (worldPos - gridField->bounds_min) / gridField->cell_size;
}

bool FlowField::isInBounds(const glm::vec3& position) const
{
    return position.x >= boundsMin.x && position.x <= boundsMax.x &&
        position.y >= boundsMin.y && position.y <= boundsMax.y &&
        position.z >= boundsMin.z && position.z <= boundsMax.z;
}