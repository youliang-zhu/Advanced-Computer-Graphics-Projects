#include "ParticleSystem.h"
#include <algorithm> 
ParticleSystem::ParticleSystem(int capacity) : max_size(capacity)
{
	gl_data_temp_buffer.resize(max_size);
}

ParticleSystem::~ParticleSystem()
{
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

void ParticleSystem::spawn(Particle particle)
{
    if (particles.size() < max_size)
    {
        particles.push_back(particle);
    }
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