#include "ParticleSystem.h"
#include "ComputeManager.h"
#include"SmokePhysics.h"
#include "FlowFieldGPU.h"
#include <algorithm> 
#include <iostream>

ParticleSystem::ParticleSystem(int capacity)
    : max_size(capacity), activeParticleCount(0), totalParticleCount(0), useGPUCompute(false)
{
    gl_data_temp_buffer.resize(max_size);
    particles.reserve(max_size);
    newParticles.reserve(100);

    flowFieldGPU = new FlowFieldGPU();
}

ParticleSystem::~ParticleSystem()
{
    if (gl_vao != 0) {
        glDeleteVertexArrays(1, &gl_vao);
    }
    if (gl_buffer != 0) {
        glDeleteBuffers(1, &gl_buffer);
    }
    if (flowFieldGPU) 
    {
        delete flowFieldGPU;
        flowFieldGPU = nullptr;
    }
}

void ParticleSystem::init_gpu_data()
{
    // Generate and bind VAO
    glGenVertexArrays(1, &gl_vao);
    glBindVertexArray(gl_vao);

    // Generate and bind VBO
    glGenBuffers(1, &gl_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, gl_buffer);

    // Allocate GPU memory space, each particle requires a vec4 (4 floats)
    GLsizeiptr bufferSize = max_size * sizeof(glm::vec4);
    glBufferData(GL_ARRAY_BUFFER, bufferSize, nullptr, GL_DYNAMIC_DRAW);

    // Setting vertex attributes
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(glm::vec4), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void ParticleSystem::process_particles(float dt)
{
    // First loop: Update alive particles
    for (unsigned i = 0; i < particles.size(); ++i) 
    {
        Particle& particle = particles[i];
        particle.pos += particle.velocity * dt;
        particle.lifetime += dt;
    }
    // Second loop: Kill dead particles (iterate backwards to handle index changes)
    for (int i = particles.size() - 1; i >= 0; --i) {
        const Particle& particle = particles[i];
        if (particle.lifetime > particle.life_length) {
            kill(i);
        }
    }
}

void ParticleSystem::submit_to_gpu(const glm::mat4& viewMat)
{
    if (useGPUCompute) {
        syncGPUData();
    }

    unsigned int num_active_particles = particles.size();
    if (num_active_particles == 0)
    {
        return;
    }

    // Extract and convert particle data
    for (unsigned int i = 0; i < num_active_particles; i++)
    {
        const Particle& particle = particles[i];

        // Convert particle positions to view space
        glm::vec4 viewSpacePos = viewMat * glm::vec4(particle.pos, 1.0f);

        // Normalized lifespan: 0 = just created, 1 = about to die
        float normalizedLifetime = particle.lifetime / particle.life_length;
        normalizedLifetime = glm::clamp(normalizedLifetime, 0.0f, 1.0f);

        // Pack the data into a vec4: xyz is the view space position, w is the normalized lifetime
        gl_data_temp_buffer[i] = glm::vec4(viewSpacePos.x, viewSpacePos.y, viewSpacePos.z, normalizedLifetime);
    }

    // Sort particles by depth
    std::sort(gl_data_temp_buffer.begin(),
        std::next(gl_data_temp_buffer.begin(), num_active_particles),
        [](const glm::vec4& lhs, const glm::vec4& rhs) {
            return lhs.z < rhs.z;
        });

    // Upload data to GPU
    glBindBuffer(GL_ARRAY_BUFFER, gl_buffer);
    GLsizeiptr dataSize = num_active_particles * sizeof(glm::vec4);
    glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize, gl_data_temp_buffer.data());
}


void ParticleSystem::kill(int id)
{
    if (id < 0 || id >= particles.size())
    {
        return;
    }
    // Swap with last element and pop
    std::swap(particles[id], particles.back());
    particles.pop_back();
}

///////////////////////////////////////////////////////////////////////
// GPU compute
///////////////////////////////////////////////////////////////////////

void ParticleSystem::initGPUCompute(ComputeManager* computeManager)
{
    this->computeManager = computeManager;

    if (computeManager && computeManager->isReady()) 
    {
        // Setting up the SSBO buffer
        computeManager->setupSSBOs(max_size);
        useGPUCompute = true;
        std::cout << "GPU compute initialized for particle system" << std::endl;
    }
    else {
        std::cerr << "Failed to initialize GPU compute for particle system" << std::endl;
        useGPUCompute = false;
    }
}

void ParticleSystem::spawn(Particle particle)
{
    if (useGPUCompute) {
        // GPU mode: Add to the upload queue first
        if (totalParticleCount < max_size) {
            newParticles.push_back(particle);
        }
    }
    else {
        if (particles.size() < max_size) {
            particles.push_back(particle);
        }
    }
}

void ParticleSystem::updateParticlesGPU(float deltaTime, float currentTime)
{
    if (!useGPUCompute || !computeManager) {
        return;
    }

    uploadNewParticles();

    if (totalParticleCount > 0) 
    {
        PhysicsParametersGPU physicsParams = computeManager->getPhysicsParameters();

        if (useFlowField && flowFieldGPU) 
        {
            flowFieldGPU->updateFlowField(currentTime);
            computeManager->updateParticlesWithPhysicsAndFlow(deltaTime, totalParticleCount, physicsParams, flowFieldGPU);
        }
        else
        {
            computeManager->updateParticlesWithPhysics(deltaTime, totalParticleCount, physicsParams);
        }

        // Get updated count information
        auto counters = computeManager->getParticleCounters();
        activeParticleCount = counters.alive_count;

        // Periodically compress the particle array
        static float accumTime = 0.0f;
        accumTime += deltaTime;
        if (accumTime > compactInterval && counters.dead_count > max_size / 4) {
            compactParticles();
            accumTime = 0.0f;
        }
    }

    if (totalParticleCount > 0) 
    {
        syncGPUData();
    }
}

void ParticleSystem::uploadNewParticles()
{
    if (newParticles.empty() || !computeManager) {
        return;
    }

    GLuint particleSSBO = computeManager->getParticleSSBO();
    if (particleSSBO == 0) {
        return;
    }

    unsigned int canAdd = std::min(static_cast<unsigned int>(newParticles.size()),
        max_size - totalParticleCount);

    if (canAdd == 0) {
        newParticles.clear();
        return;
    }

    // Upload new particle data to GPU
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, particleSSBO);

    GLintptr offset = totalParticleCount * 8 * sizeof(float); // 8 floats per particle
    GLsizeiptr size = canAdd * 8 * sizeof(float);

    std::vector<float> uploadData;
    uploadData.reserve(canAdd * 8);

    for (unsigned int i = 0; i < canAdd; ++i) {
        const Particle& p = newParticles[i];
        // pos.xyz, lifetime, velocity.xyz, life_length
        uploadData.push_back(p.pos.x);
        uploadData.push_back(p.pos.y);
        uploadData.push_back(p.pos.z);
        uploadData.push_back(p.lifetime);
        uploadData.push_back(p.velocity.x);
        uploadData.push_back(p.velocity.y);
        uploadData.push_back(p.velocity.z);
        uploadData.push_back(p.life_length);
    }

    glBufferSubData(GL_SHADER_STORAGE_BUFFER, offset, size, uploadData.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // Update count
    totalParticleCount += canAdd;
    activeParticleCount += canAdd;

    // Update the counter on the GPU side
    computeManager->updateParticleCounters(activeParticleCount, totalParticleCount);

    newParticles.clear();
}

void ParticleSystem::syncGPUData()
{
    if (!useGPUCompute || !computeManager) {
        return;
    }
    downloadParticleData();
}

void ParticleSystem::downloadParticleData()
{
    if (!computeManager || totalParticleCount == 0) {
        return;
    }

    GLuint particleSSBO = computeManager->getParticleSSBO();
    if (particleSSBO == 0) {
        return;
    }

    // Download GPU data for rendering
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, particleSSBO);
    void* ptr = glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_ONLY);

    if (ptr) {
        float* data = static_cast<float*>(ptr);
        particles.clear();
        particles.reserve(totalParticleCount);

        for (unsigned int i = 0; i < totalParticleCount; ++i) {
            float* particleData = &data[i * 8];

            Particle p;
            p.pos = glm::vec3(particleData[0], particleData[1], particleData[2]);
            p.lifetime = particleData[3];
            p.velocity = glm::vec3(particleData[4], particleData[5], particleData[6]);
            p.life_length = particleData[7];

            // Only add live particles
            if (p.lifetime >= 0.0f) {
                particles.push_back(p);
            }
        }
        glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void ParticleSystem::compactParticles()
{
    if (!useGPUCompute || !computeManager) {
        return;
    }

    downloadParticleData();

    // Re-upload the compressed data
    GLuint particleSSBO = computeManager->getParticleSSBO();
    if (particleSSBO == 0 || particles.empty()) {
        return;
    }

    // Preparing compressed data
    std::vector<float> compactData;
    compactData.reserve(particles.size() * 8);

    for (const auto& p : particles) 
    {
        compactData.push_back(p.pos.x);
        compactData.push_back(p.pos.y);
        compactData.push_back(p.pos.z);
        compactData.push_back(p.lifetime);
        compactData.push_back(p.velocity.x);
        compactData.push_back(p.velocity.y);
        compactData.push_back(p.velocity.z);
        compactData.push_back(p.life_length);
    }

    // Upload compressed data
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, particleSSBO);
    GLsizeiptr dataSize = compactData.size() * sizeof(float);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, dataSize, compactData.data());

    // Clear the remaining part
    if (particles.size() < totalParticleCount) {
        GLsizeiptr clearOffset = dataSize;
        GLsizeiptr clearSize = (totalParticleCount - particles.size()) * 8 * sizeof(float);
        std::vector<float> zeroData(clearSize / sizeof(float), 0.0f);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, clearOffset, clearSize, zeroData.data());
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // Update count
    totalParticleCount = particles.size();
    activeParticleCount = particles.size();

    // Update GPU counters
    computeManager->updateParticleCounters(activeParticleCount, totalParticleCount);

    std::cout << "Compacted particles: " << totalParticleCount << " alive" << std::endl;
}


///////////////////////////////////////////////////////////////////////
// GPU Physics Control
///////////////////////////////////////////////////////////////////////

void ParticleSystem::setPhysicsParameters(float gravity, float dragCoeff, float mass)
{
    if (computeManager && useGPUCompute) 
    {
        PhysicsParametersGPU params(gravity, dragCoeff, mass);
        computeManager->setPhysicsParameters(params);
    }
}

void ParticleSystem::syncPhysicsFromCPU(const SmokePhysics* smokePhysics)
{
    if (smokePhysics && computeManager && useGPUCompute) 
    {
        const auto& cpuParams = smokePhysics->getParameters();

        PhysicsParametersGPU gpuParams;
        gpuParams.gravity = cpuParams.gravity_strength;
        gpuParams.dragCoeff = cpuParams.drag_coefficient;
        gpuParams.particleMass = cpuParams.particle_mass;

        computeManager->setPhysicsParameters(gpuParams);

        std::cout << "Synced physics parameters from CPU to GPU" << std::endl;
    }
}

///////////////////////////////////////////////////////////////////////
// Flow Field Methods
///////////////////////////////////////////////////////////////////////

void ParticleSystem::setFlowFieldParameters(const glm::vec3& windDir, float strength, float influence)
{
    if (flowFieldGPU && useGPUCompute) 
    {
        // Set wind farm parameters
        UniformWindParameters windParams(glm::normalize(windDir), strength);
        flowFieldGPU->setWindParameters(windParams);

        // Set the flow field influence strength
        if (computeManager) {
            computeManager->setFlowInfluence(influence);
        }

        // Generate flow field data
        flowFieldGPU->generateUniformWind(windParams, 0.0f);

        std::cout << "Flow field parameters updated: direction=("
            << windDir.x << "," << windDir.y << "," << windDir.z
            << "), strength=" << strength
            << ", influence=" << influence << std::endl;
    }
}

void ParticleSystem::enableFlowField(bool enable)
{
    useFlowField = enable;
    if (flowFieldGPU) {
        flowFieldGPU->setEnabled(enable);
    }

    std::cout << "Flow field " << (enable ? "enabled" : "disabled") << std::endl;
}

void ParticleSystem::setFlowInfluence(float influence)
{
    if (computeManager) {
        computeManager->setFlowInfluence(influence);
    }
}

void ParticleSystem::initializeFlowFieldWithBounds(const glm::vec3& worldMin, const glm::vec3& worldMax)
{
    if (flowFieldGPU && useGPUCompute) 
    {
        flowFieldGPU->initialize(glm::ivec3(64, 64, 64), worldMin, worldMax);
    }
}