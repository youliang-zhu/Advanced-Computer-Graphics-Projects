#pragma once

#include <glm/glm.hpp>
#include <vector>

struct Particle;
class FlowField;

struct PhysicsParameters
{
    float gravity_strength = 0.0f;
    float drag_coefficient = 0.5f;
    float particle_mass = 1.0f;
    float flow_influence = 1.0f;

    PhysicsParameters() = default;
    PhysicsParameters(float gravity, float drag, float mass, float flow = 1.0f)
        : gravity_strength(gravity), drag_coefficient(drag), particle_mass(mass), flow_influence(flow) 
    {
    }
};

class SmokePhysics
{
public:
    ///////////////////////////////////////////////////////////////////////
    // Particles physics
    ///////////////////////////////////////////////////////////////////////

    explicit SmokePhysics(const PhysicsParameters& params);

    ~SmokePhysics() = default;

    /// Update the physics state of a single particle
    void updateParticle(Particle& particle, float deltaTime, float time);

    /// Batch update the physics state of multiple particles
    void updateParticles(std::vector<Particle>& particles, float deltaTime, float time);

    /// Calculate the gravitational force on a particle
    glm::vec3 calculateGravityForce(const Particle& particle) const;

    /// Calculate the drag acting on a particle
    glm::vec3 calculateDragForce(const Particle& particle) const;

    /// Calculate the total force on the particle
    glm::vec3 calculateTotalForce(const Particle& particle, float time) const;

    /// Get physical parameters
    const PhysicsParameters& getParameters() const { return physicsParams; }

    /// Setting physical parameters
    void setParameters(const PhysicsParameters& params) { physicsParams = params; }

    /// Set gravity strength
    void setGravityStrength(float gravity) { physicsParams.gravity_strength = gravity; }

    /// Setting the drag coefficient
    void setDragCoefficient(float drag) { physicsParams.drag_coefficient = drag; }

    /// Setting particle quality
    void setParticleMass(float mass) { physicsParams.particle_mass = mass; }

    ///////////////////////////////////////////////////////////////////////
    // Flow Field physics
    ///////////////////////////////////////////////////////////////////////

    /// Setting up flow field references
    void setFlowField(FlowField* flowField) { this->flowField = flowField; }

    /// Calculate the force of the flow field on the particles
    glm::vec3 calculateFlowForce(const Particle& particle, float time) const;

    /// Set the flow field influence strength
    void setFlowInfluence(float influence) { physicsParams.flow_influence = influence; }

private:
    PhysicsParameters physicsParams;
    FlowField* flowField = nullptr;

    /// Integration using implicit Euler method
    void implicitEulerIntegration(Particle& particle, float deltaTime, float time);
};