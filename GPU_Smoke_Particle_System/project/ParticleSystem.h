#pragma once

#include <GL/glew.h>
#include <vector>
#include <glm/detail/type_vec3.hpp>
#include <glm/mat4x4.hpp>

class ComputeManager;
class FlowFieldGPU;

struct Particle
{
	glm::vec3 pos;
	float lifetime;
	glm::vec3 velocity;
	float life_length;
};

class ParticleSystem
{
public:
	/// Allocates the gpu buffer to hold up to `capacity` particles and the corresponding vao
	explicit ParticleSystem(int capacity);

	/// Clean up the gpu structures created in the constructor
	~ParticleSystem();

	void init_gpu_data();

	/// Creates a new particle if there less than `max_size` elements in the particles array buffer
	void spawn(Particle particle);

	/// Updates all the particles' positions depending on their speed, their lifetimes, and kills any
	/// that are past their life_length
	void process_particles(float dt);

	/// Updates the vertex buffer with the current particle properties, and renders them
	void submit_to_gpu(const glm::mat4& viewMat);

	GLuint getVAO() const { return gl_vao; }
	GLsizei get_particle_count() const { return static_cast<GLsizei>(particles.size()); }

	///////////////////////////////////////////////////////////////////////
	// GPU compute
	///////////////////////////////////////////////////////////////////////

	/// Initialize GPU computing related resources
	void initGPUCompute(ComputeManager* computeManager);

	/// GPU version particle update - replace the original process_particles
	void updateParticlesGPU(float deltaTime, float currentTime);

	/// Synchronize GPU data to CPU (for rendering)
	void syncGPUData();

	/// Get the actual number of surviving particles
	unsigned int getAliveParticleCount() const { return activeParticleCount; }

	/// Compress particle array and remove dead particles (called periodically)
	void compactParticles();

	///////////////////////////////////////////////////////////////////////
	// GPU Physics Control
	///////////////////////////////////////////////////////////////////////

	/// Setting GPU physics parameters
	void setPhysicsParameters(float gravity, float dragCoeff, float mass);

	/// Get parameters from an existing SmokePhysics object and set them to the GPU
	void syncPhysicsFromCPU(const class SmokePhysics* smokePhysics);

	///////////////////////////////////////////////////////////////////////
	// GPU Flow Field Control
	///////////////////////////////////////////////////////////////////////

	/// Setting flow field parameters	
	void setFlowFieldParameters(const glm::vec3& windDir, float strength, float influence);

	/// Enable/disable flow field
	void enableFlowField(bool enable);

	/// Get the flow field object (for external configuration)
	class FlowFieldGPU* getFlowField() { return flowFieldGPU; }

	/// Set the flow field influence strength
	void setFlowInfluence(float influence);

	void initializeFlowFieldWithBounds(const glm::vec3& worldMin, const glm::vec3& worldMax);
	

private:
	/// Deletes a particle at position `id` by swapping it with the last
	/// particle in the array and reducing the size by 1.
	/// (Care with indexes, as this can change the particle an index refers to)
	void kill(int id);

	// Members
	//std::vector<Particle> particles;
	int max_size;

	GLuint gl_vao = 0;
	GLuint gl_buffer = 0;
	std::vector<glm::vec4> gl_data_temp_buffer;

	///////////////////////////////////////////////////////////////////////
	// GPU compute
	///////////////////////////////////////////////////////////////////////

	/// Upload new particle data to the GPU
	void uploadNewParticles();

	/// Download particle data from GPU
	void downloadParticleData();

	// Member variables
	unsigned int activeParticleCount;
	unsigned int totalParticleCount;

	ComputeManager* computeManager = nullptr;
	bool useGPUCompute = false;

	// CPU-side particle buffer (for new particle generation and GPU synchronization)
	std::vector<Particle> newParticles;  // New particles waiting to be uploaded to the GPU
	std::vector<Particle> particles;

	float lastCompactTime = 0.0f;       // Last compression time
	const float compactInterval = 2.0f;  // Compression interval (seconds)

	///////////////////////////////////////////////////////////////////////
	// GPU Flow Field
	///////////////////////////////////////////////////////////////////////

	class FlowFieldGPU* flowFieldGPU = nullptr;
	bool useFlowField = false;
};
