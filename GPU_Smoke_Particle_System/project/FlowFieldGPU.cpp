#include "FlowFieldGPU.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>

FlowFieldGPU::FlowFieldGPU()
    : flowFieldTexture3D(0), flowFieldComputeShader(0),
    initialized(false), flowFieldEnabled(true), lastUpdateTime(0.0f)
{
}

FlowFieldGPU::~FlowFieldGPU()
{
    cleanup();
}

bool FlowFieldGPU::initialize(const glm::ivec3& resolution,
    const glm::vec3& worldMin,
    const glm::vec3& worldMax)
{
    if (initialized) 
    {
        return true;
    }

    // Check 3D texture support
    if (!GLEW_VERSION_1_2) 
    {
        std::cerr << "3D textures not supported!" << std::endl;
        return false;
    }

    bounds = FlowFieldBounds(worldMin, worldMax, resolution);

    // Creating 3D Textures
    glGenTextures(1, &flowFieldTexture3D);
    glBindTexture(GL_TEXTURE_3D, flowFieldTexture3D);

    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    // Allocating texture memory
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA32F,
        resolution.x, resolution.y, resolution.z,
        0, GL_RGB, GL_FLOAT, nullptr);

    glBindTexture(GL_TEXTURE_3D, 0);

    checkGLError("FlowField 3D texture creation");

    // Loading compute shaders
    if (!loadFlowFieldComputeShader("../../TDA362_GPU_Smoke_Particle_System/project_others/flow_field_generate.comp")) 
    {
        std::cerr << "Failed to load flow field compute shader!" << std::endl;
        cleanup();
        return false;
    }

    // Set default wind farm parameters
    windParams = UniformWindParameters(glm::vec3(0.0f, 1.0f, 0.0f), 3.0f, 1.0f);

    initialized = true;
    std::cout << "FlowFieldGPU initialized successfully with resolution: "
        << resolution.x << "x" << resolution.y << "x" << resolution.z << std::endl;

    return true;
}

bool FlowFieldGPU::loadFlowFieldComputeShader(const std::string& filepath)
{
    std::string source = readFile(filepath);
    if (source.empty()) {
        std::cerr << "Failed to read flow field compute shader file: " << filepath << std::endl;
        return false;
    }

    flowFieldComputeShader = compileComputeShader(source);
    return flowFieldComputeShader != 0;
}

void FlowFieldGPU::generateUniformWind(const UniformWindParameters& params, float currentTime)
{
    if (!initialized || !flowFieldEnabled) {
        return;
    }

    windParams = params;
    lastUpdateTime = currentTime;

    glUseProgram(flowFieldComputeShader);

    // Bind a 3D texture as an image texture
    glBindImageTexture(0, flowFieldTexture3D, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);

    // Setting uniform variables
    glUniform3fv(glGetUniformLocation(flowFieldComputeShader, "u_windDirection"), 1, &windParams.windDirection[0]);
    glUniform1f(glGetUniformLocation(flowFieldComputeShader, "u_windStrength"), windParams.windStrength);
    glUniform1f(glGetUniformLocation(flowFieldComputeShader, "u_time"), currentTime);
    glUniform1f(glGetUniformLocation(flowFieldComputeShader, "u_timeScale"), windParams.timeScale);
    glUniform1f(glGetUniformLocation(flowFieldComputeShader, "u_heightVariation"), windParams.heightVariation);

    glUniform3fv(glGetUniformLocation(flowFieldComputeShader, "u_worldMin"), 1, &bounds.worldMin[0]);
    glUniform3fv(glGetUniformLocation(flowFieldComputeShader, "u_worldMax"), 1, &bounds.worldMax[0]);

    // Calculating the number of workgroups
    glm::ivec3 workGroups = (bounds.resolution + glm::ivec3(3)) / glm::ivec3(4);

    // Scheduling compute shaders
    glDispatchCompute(workGroups.x, workGroups.y, workGroups.z);
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);

    glUseProgram(0);

    checkGLError("generateUniformWind");
}

void FlowFieldGPU::updateFlowField(float currentTime)
{
    // Only regenerate when the time has changed significantly (avoid recalculating every frame)
    if (initialized && flowFieldEnabled &&
        std::abs(currentTime - lastUpdateTime) > 0.016f) 
    { // ~60fps
        generateUniformWind(windParams, currentTime);
    }
}

void FlowFieldGPU::setWindParameters(const UniformWindParameters& params)
{
    windParams = params;
    std::cout << "Wind parameters updated: direction=("
        << params.windDirection.x << "," << params.windDirection.y << "," << params.windDirection.z
        << "), strength=" << params.windStrength
        << ", timeScale=" << params.timeScale << std::endl;
}

void FlowFieldGPU::cleanup()
{
    if (flowFieldTexture3D != 0) {
        glDeleteTextures(1, &flowFieldTexture3D);
        flowFieldTexture3D = 0;
    }

    if (flowFieldComputeShader != 0) {
        glDeleteProgram(flowFieldComputeShader);
        flowFieldComputeShader = 0;
    }

    initialized = false;
}

GLuint FlowFieldGPU::compileComputeShader(const std::string& source)
{
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    const char* sourceCStr = source.c_str();
    glShaderSource(shader, 1, &sourceCStr, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLchar infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        std::cerr << "Flow field compute shader compilation failed:\n" << infoLog << std::endl;
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
        std::cerr << "Flow field compute shader program linking failed:\n" << infoLog << std::endl;
        glDeleteProgram(program);
        glDeleteShader(shader);
        return 0;
    }

    glDeleteShader(shader);
    return program;
}

std::string FlowFieldGPU::readFile(const std::string& filepath)
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

void FlowFieldGPU::checkGLError(const std::string& operation)
{
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        std::cerr << "OpenGL error in " << operation << ": " << error << std::endl;
    }
}