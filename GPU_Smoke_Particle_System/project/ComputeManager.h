#pragma once

#include <GL/glew.h>
#include <string>

class FlowFieldGPU;

struct ParticleCounters
{
    unsigned int alive_count;
    unsigned int dead_count;
    unsigned int total_count;
    unsigned int padding; // 4字节对齐
};

struct PhysicsParametersGPU
{
    float gravity = 2.5f;
    float dragCoeff = 0.5f;
    float particleMass = 1.0f;
    float padding = 0.0f;  // 4字节对齐

    PhysicsParametersGPU() = default;
    PhysicsParametersGPU(float g, float drag, float mass)
        : gravity(g), dragCoeff(drag), particleMass(mass) {
    }
};


class ComputeManager
{
public:
    ComputeManager();
    ~ComputeManager();

    /// Initialize compute shader and related resources
    bool initialize();

    /// Compile and load compute shader
    bool loadComputeShader(const std::string& filepath);

    /// Creating and binding SSBO buffers
    void setupSSBOs(unsigned int maxParticles);

    /// Perform particle update calculations
    void updateParticles(float deltaTime, unsigned int particleCount);

    /// Get the particle count information on the GPU
    ParticleCounters getParticleCounters();

    /// Update particle count information
    void updateParticleCounters(unsigned int aliveCount, unsigned int totalCount);

    /// Get the particle SSBO handle (for use by ParticleSystem)
    GLuint getParticleSSBO() const { return particleSSBO; }

    /// Cleaning up resources
    void cleanup();

    /// Check if compute shader is available
    bool isReady() const { return computeShaderProgram != 0; }

    ///////////////////////////////////////////////////////////////////////////////
    // physics system
    ///////////////////////////////////////////////////////////////////////////////

    void updateParticlesWithPhysics(float deltaTime, unsigned int particleCount,
        const PhysicsParametersGPU& physics);

    void setPhysicsParameters(const PhysicsParametersGPU& params);

    const PhysicsParametersGPU& getPhysicsParameters() const { return physicsParams; }


    ///////////////////////////////////////////////////////////////////////////////
    // flow field
    ///////////////////////////////////////////////////////////////////////////////

    void updateParticlesWithPhysicsAndFlow(float deltaTime, unsigned int particleCount,
        const PhysicsParametersGPU& physics,
        FlowFieldGPU* flowField);

    void setFlowInfluence(float influence) { flowInfluence = influence; }

    float getFlowInfluence() const { return flowInfluence; }

private:

    GLuint computeShaderProgram;

    GLuint particleSSBO;    // Particle Data Buffer
    GLuint counterSSBO;     // Counter buffer

    unsigned int maxParticles;
    bool initialized;

    float flowInfluence;
    PhysicsParametersGPU physicsParams;

    GLuint compileComputeShader(const std::string& source);

    std::string readFile(const std::string& filepath);

    void checkGLError(const std::string& operation);
};