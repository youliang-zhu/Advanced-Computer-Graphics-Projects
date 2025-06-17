#include "ComputeManager.h"
#include "FlowFieldGPU.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
//#include "../../TDA362_GPU_Smoke_Particle_System/project_others/"

ComputeManager::ComputeManager()
    : computeShaderProgram(0), particleSSBO(0), counterSSBO(0),
    maxParticles(0), initialized(false)
{
    physicsParams = PhysicsParametersGPU(2.5f, 0.5f, 1.0f);
}

ComputeManager::~ComputeManager()
{
    cleanup();
}

bool ComputeManager::initialize()
{
    if (initialized) 
    {
        return true;
    }

    // Check compute shader support
    if (!GLEW_ARB_compute_shader) {
        std::cerr << "Compute shaders not supported!" << std::endl;
        return false;
    }

    // Loading compute shader
    if (!loadComputeShader("../../TDA362_GPU_Smoke_Particle_System/project_others/particle_update.comp"))
    {
        std::cerr << "Failed to load particle update compute shader!" << std::endl;
        return false;
    }

    initialized = true;
    return true;
}

bool ComputeManager::loadComputeShader(const std::string& filepath)
{
    std::string source = readFile(filepath);
    if (source.empty()) {
        std::cerr << "Failed to read compute shader file: " << filepath << std::endl;
        return false;
    }

    computeShaderProgram = compileComputeShader(source);
    return computeShaderProgram != 0;
}

void ComputeManager::setupSSBOs(unsigned int maxParticles)
{
    this->maxParticles = maxParticles;

    // Creating the Particle Data SSBO
    glGenBuffers(1, &particleSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, particleSSBO);

    // Each particle: vec3 pos + float lifetime + vec3 velocity + float life_length = 8 floats
    GLsizeiptr particleBufferSize = maxParticles * 8 * sizeof(float);
    glBufferData(GL_SHADER_STORAGE_BUFFER, particleBufferSize, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, particleSSBO);

    // Creating Counter SSBO
    glGenBuffers(1, &counterSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, counterSSBO);
    GLsizeiptr counterBufferSize = sizeof(ParticleCounters);
    glBufferData(GL_SHADER_STORAGE_BUFFER, counterBufferSize, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, counterSSBO);

    // Initialize counter
    ParticleCounters initialCounters = { 0, 0, 0, 0 };
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(ParticleCounters), &initialCounters);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    checkGLError("setupSSBOs");
}

void ComputeManager::updateParticles(float deltaTime, unsigned int particleCount)
{
    updateParticlesWithPhysics(deltaTime, particleCount, physicsParams);
}

ParticleCounters ComputeManager::getParticleCounters()
{
    ParticleCounters counters = { 0, 0, 0, 0 };

    if (counterSSBO == 0) {
        return counters;
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, counterSSBO);
    void* ptr = glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_ONLY);
    if (ptr) {
        memcpy(&counters, ptr, sizeof(ParticleCounters));
        glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    return counters;
}

void ComputeManager::updateParticleCounters(unsigned int aliveCount, unsigned int totalCount)
{
    if (counterSSBO == 0) {
        return;
    }

    ParticleCounters counters = { aliveCount, 0, totalCount, 0 };

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, counterSSBO);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(ParticleCounters), &counters);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void ComputeManager::cleanup()
{
    if (particleSSBO != 0) {
        glDeleteBuffers(1, &particleSSBO);
        particleSSBO = 0;
    }

    if (counterSSBO != 0) {
        glDeleteBuffers(1, &counterSSBO);
        counterSSBO = 0;
    }

    if (computeShaderProgram != 0) {
        glDeleteProgram(computeShaderProgram);
        computeShaderProgram = 0;
    }

    initialized = false;
}

GLuint ComputeManager::compileComputeShader(const std::string& source)
{
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    const char* sourceCStr = source.c_str();
    glShaderSource(shader, 1, &sourceCStr, nullptr);
    glCompileShader(shader);

    // Checking the compilation status
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLchar infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        std::cerr << "Compute shader compilation failed:\n" << infoLog << std::endl;
        glDeleteShader(shader);
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);

    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        GLchar infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        std::cerr << "Compute shader program linking failed:\n" << infoLog << std::endl;
        glDeleteProgram(program);
        glDeleteShader(shader);
        return 0;
    }

    glDeleteShader(shader);
    return program;
}

std::string ComputeManager::readFile(const std::string& filepath)
{
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filepath << std::endl;
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void ComputeManager::checkGLError(const std::string& operation)
{
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        std::cerr << "OpenGL error in " << operation << ": " << error << std::endl;
    }
}

///////////////////////////////////////////////////////////////////////////////
// physics system
///////////////////////////////////////////////////////////////////////////////

void ComputeManager::updateParticlesWithPhysics(float deltaTime, unsigned int particleCount, const PhysicsParametersGPU& physics)
{
    if (!isReady() || particleCount == 0) {
        return;
    }

    glUseProgram(computeShaderProgram);

    // Setting basic uniform variables
    glUniform1f(glGetUniformLocation(computeShaderProgram, "u_deltaTime"), deltaTime);
    glUniform1ui(glGetUniformLocation(computeShaderProgram, "u_maxParticles"), maxParticles);

    // Set physical parameter uniform variables
    glUniform1f(glGetUniformLocation(computeShaderProgram, "u_gravity"), physics.gravity);
    glUniform1f(glGetUniformLocation(computeShaderProgram, "u_dragCoeff"), physics.dragCoeff);
    glUniform1f(glGetUniformLocation(computeShaderProgram, "u_particleMass"), physics.particleMass);

    glUniform1i(glGetUniformLocation(computeShaderProgram, "u_hasFlowField"), 0);
    glUniform1f(glGetUniformLocation(computeShaderProgram, "u_flowInfluence"), 0.0f);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, particleSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, counterSSBO);

    // Calculating the number of workgroups
    unsigned int numWorkGroups = (particleCount + 63) / 64; // Round up

    // Scheduling compute shaders
    glDispatchCompute(numWorkGroups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    glUseProgram(0);

    checkGLError("updateParticlesWithPhysics");
}

void ComputeManager::setPhysicsParameters(const PhysicsParametersGPU& params)
{
    physicsParams = params;
    std::cout << "Physics parameters updated: gravity=" << params.gravity
        << ", drag=" << params.dragCoeff
        << ", mass=" << params.particleMass << std::endl;
}

///////////////////////////////////////////////////////////////////////////////
// flow field
///////////////////////////////////////////////////////////////////////////////

void ComputeManager::updateParticlesWithPhysicsAndFlow(float deltaTime, unsigned int particleCount,
    const PhysicsParametersGPU& physics,
    FlowFieldGPU* flowField)
{
    if (!isReady() || particleCount == 0) {
        return;
    }

    glUseProgram(computeShaderProgram);

    // Setting uniform variables
    glUniform1f(glGetUniformLocation(computeShaderProgram, "u_deltaTime"), deltaTime);
    glUniform1ui(glGetUniformLocation(computeShaderProgram, "u_maxParticles"), maxParticles);

    glUniform1f(glGetUniformLocation(computeShaderProgram, "u_gravity"), physics.gravity);
    glUniform1f(glGetUniformLocation(computeShaderProgram, "u_dragCoeff"), physics.dragCoeff);
    glUniform1f(glGetUniformLocation(computeShaderProgram, "u_particleMass"), physics.particleMass);

    bool hasFlowField = (flowField != nullptr && flowField->isInitialized() && flowField->isEnabled());
    glUniform1i(glGetUniformLocation(computeShaderProgram, "u_hasFlowField"), hasFlowField ? 1 : 0);
    glUniform1f(glGetUniformLocation(computeShaderProgram, "u_flowInfluence"), flowInfluence);

    if (hasFlowField) 
    {
        // Binding 3D flow field texture
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_3D, flowField->getFlowFieldTexture());
        glUniform1i(glGetUniformLocation(computeShaderProgram, "u_flowFieldTexture"), 1);

        // Set flow field boundary information
        const auto& bounds = flowField->getBounds();
        glUniform3fv(glGetUniformLocation(computeShaderProgram, "u_flowFieldWorldMin"), 1, &bounds.worldMin[0]);
        glUniform3fv(glGetUniformLocation(computeShaderProgram, "u_flowFieldWorldMax"), 1, &bounds.worldMax[0]);
    }

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, particleSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, counterSSBO);

    // Calculating the number of workgroups
    unsigned int numWorkGroups = (particleCount + 63) / 64; // Round up

    // Scheduling compute shaders
    glDispatchCompute(numWorkGroups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    glUseProgram(0);

    checkGLError("updateParticlesWithPhysicsAndFlow");
}