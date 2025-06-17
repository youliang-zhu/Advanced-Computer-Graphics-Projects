#include "SmokePhysics.h"
#include "ParticleSystem.h"
#include "FlowField.h"
#include <glm/glm.hpp>

//SmokePhysics::SmokePhysics()
//    : physicsParams(9.81f, 0.6f, 2.0f)
//{
//}

SmokePhysics::SmokePhysics(const PhysicsParameters& params)
    : physicsParams(params)
{
}

void SmokePhysics::updateParticle(Particle& particle, float deltaTime, float time)
{
    implicitEulerIntegration(particle, deltaTime, time);
}

// For GPU usage
void SmokePhysics::updateParticles(std::vector<Particle>& particles, float deltaTime, float time)
{
    for (auto& particle : particles)
    {
        updateParticle(particle, deltaTime, time);
    }
}

glm::vec3 SmokePhysics::calculateGravityForce(const Particle& particle) const
{
    // F_gravity = m * g * direction
    return glm::vec3(0.0f, -physicsParams.gravity_strength * physicsParams.particle_mass, 0.0f);
}

glm::vec3 SmokePhysics::calculateDragForce(const Particle& particle) const
{
    // F_drag = -drag_coefficient * |v| * v_normalized
    glm::vec3 velocity = particle.velocity;
    float velocityMagnitude = glm::length(velocity);

    if (velocityMagnitude < 1e-6f) // Avoid division by zero, when the speed is very small, the resistance is 0
    {
        return glm::vec3(0.0f);
    }

    glm::vec3 velocityNormalized = velocity / velocityMagnitude;

    return -physicsParams.drag_coefficient * velocityMagnitude * velocityNormalized;
}

glm::vec3 SmokePhysics::calculateTotalForce(const Particle& particle, float time) const
{
    glm::vec3 totalForce = glm::vec3(0.0f);

    totalForce += calculateGravityForce(particle);
    totalForce += calculateDragForce(particle);
    totalForce += calculateFlowForce(particle, time);

    return totalForce;
}

void SmokePhysics::implicitEulerIntegration(Particle& particle, float deltaTime, float time)
{
    float mass = physicsParams.particle_mass;
    float dragCoeff = physicsParams.drag_coefficient;
    glm::vec3 gravity = glm::vec3(0.0f, -physicsParams.gravity_strength, 0.0f);

    // Current velocity v_n
    glm::vec3 currentVelocity = particle.velocity;

    // Calculating flow field contributions
    glm::vec3 flowContribution(0.0f);
    glm::vec3 flowVelocity = flowField->getVelocityAt(particle.pos, time);
    float flowInfluence = physicsParams.flow_influence;
    flowContribution = flowVelocity * flowInfluence * deltaTime;

    // Analytical solution of implicit Euler£º v_{n+1} = (v_n + dt*g) / (1 + dt*k/m)
    float dampingFactor = 1.0f + deltaTime * (dragCoeff / mass);
    glm::vec3 gravityContribution = deltaTime * gravity;

    // update velocity
    glm::vec3 newVelocity = (currentVelocity + gravityContribution + flowContribution) / dampingFactor;

    // update position
    particle.pos += deltaTime * newVelocity;

    particle.velocity = newVelocity;
}


glm::vec3 SmokePhysics::calculateFlowForce(const Particle& particle, float time) const
{
    if (!flowField) 
    {
        return glm::vec3(0.0f);
    }

    // Get the flow velocity at the particle position
    glm::vec3 flowVelocity = flowField->getVelocityAt(particle.pos, time);

    // Calculate the difference between particle velocity and flow velocity
    glm::vec3 velocityDifference = flowVelocity - particle.velocity;

    // Flow force is proportional to the velocity difference
    // F_flow = flow_coefficient * (v_flow - v_particle)
    float flowCoefficient = physicsParams.flow_influence * physicsParams.particle_mass;

    return flowCoefficient * velocityDifference;
}