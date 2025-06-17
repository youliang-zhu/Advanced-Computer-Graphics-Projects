

#ifdef _WIN32
extern "C" _declspec(dllexport) unsigned int NvOptimusEnablement = 0x00000001;
#endif

#include <GL/glew.h>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <chrono>

#include <labhelper.h>
#include <imgui.h>
#include <imgui_impl_sdl_gl3.h>

#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
using namespace glm;

#include <Model.h>
#include "hdr.h"
#include "fbo.h"
#include "heightfield.h"

#include "ParticleSystem.h"
#include "BoundaryManager.h"
#include "SmokePhysics.h"
#include "FlowField.h"
#include "FlowFieldGPU.h"


#include "ComputeManager.h"

#include "stb_image.h"

#include <iostream>




using std::min;
using std::max;

///////////////////////////////////////////////////////////////////////////////
// Function declarations
///////////////////////////////////////////////////////////////////////////////
void drawSmokeParticles(const mat4& viewMatrix, const mat4& projectionMatrix);
void generateSmokeParticles();

///////////////////////////////////////////////////////////////////////////////
// Class globals
///////////////////////////////////////////////////////////////////////////////

ParticleSystem particleSystem(50000);
GLuint particleShaderProgram = 0;

BoundaryManager* boundaryManager = nullptr;

ComputeManager* computeManager = nullptr;

///////////////////////////////////////////////////////////////////////////////
// Various globals
///////////////////////////////////////////////////////////////////////////////

extern float currentTime;

SDL_Window* g_window = nullptr;
float currentTime = 0.0f;
float previousTime = 0.0f;
float deltaTime = 0.0f;
bool showUI = false;
int windowWidth, windowHeight;

// Mouse input
ivec2 g_prevMouseCoords = { -1, -1 };
bool g_isMouseDragging = false;

// Aircraft engine exhaust port location
glm::vec3 particleSpawnOffset = glm::vec3(0.0f, 50.0f, 0.0f);

// particles generating control
bool isGenerate = false;

// Explosion Texture
GLuint explosionTexture = 0;

// Particle Control Parameters
float particleBaseSize = 20.0f;      
float particleSpeedMultiplier = 15.0f;
float particleLifespan = 5.0f;
int particlesPerFrame = 64;

// SmokePhysics
SmokePhysics* smokePhysics = nullptr;

// flowField
glm::vec3 flowFieldMin, flowFieldMax;
bool showFlowFieldBounds = true;


///////////////////////////////////////////////////////////////////////////////
// Shader programs
///////////////////////////////////////////////////////////////////////////////
GLuint shaderProgram;       // Shader for rendering the final image
GLuint simpleShaderProgram; // Shader used to draw the shadow map
GLuint backgroundProgram;
GLuint heightFieldProgram;

///////////////////////////////////////////////////////////////////////////////
// Environment
///////////////////////////////////////////////////////////////////////////////
float environment_multiplier = 1.5f;
GLuint environmentMap, irradianceMap, reflectionMap;
const std::string envmap_base_name = "001";

///////////////////////////////////////////////////////////////////////////////
// Light source
///////////////////////////////////////////////////////////////////////////////
vec3 lightPosition;
vec3 point_light_color = vec3(1.f, 1.f, 1.f);

float point_light_intensity_multiplier = 10000.0f;

///////////////////////////////////////////////////////////////////////////////
// Camera parameters.
///////////////////////////////////////////////////////////////////////////////
vec3 cameraPosition(-70.0f, 50.0f, 70.0f);
vec3 cameraDirection = normalize(vec3(0.0f) - cameraPosition);
float cameraSpeed = 10.f;

vec3 worldUp(0.0f, 1.0f, 0.0f);

///////////////////////////////////////////////////////////////////////////////
// Models
///////////////////////////////////////////////////////////////////////////////
labhelper::Model* landingpadModel = nullptr;

mat4 roomModelMatrix;
mat4 landingPadModelMatrix;

///////////////////////////////////////////////////////////////////////////////
// Texture PNG
///////////////////////////////////////////////////////////////////////////////
GLuint loadPngTextureSTB(const std::string& filename)
{
	GLuint texId;
	glGenTextures(1, &texId);
	glBindTexture(GL_TEXTURE_2D, texId);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

	int width, height, channels;
	unsigned char* data = stbi_load(filename.c_str(), &width, &height, &channels, 0);

	if (data) {
		GLenum format = (channels == 4) ? GL_RGBA : GL_RGB;
		GLenum internalFormat = (channels == 4) ? GL_RGBA8 : GL_RGB8;

		glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0,
			format, GL_UNSIGNED_BYTE, data);
		glGenerateMipmap(GL_TEXTURE_2D);

		stbi_image_free(data);
		printf("PNG texture loaded: %s (%dx%d, %d channels)\n",
			filename.c_str(), width, height, channels);
	}
	else {
		printf("Failed to load PNG texture: %s\n", filename.c_str());
		printf("STB Error: %s\n", stbi_failure_reason());
		glDeleteTextures(1, &texId);
		return 0;
	}

	return texId;
}

///////////////////////////////////////////////////////////////////////////////
// Height field
///////////////////////////////////////////////////////////////////////////////
HeightField terrain;
mat4 terrainModelMatrix;
int terrainResolution = 1500; // Based on half of height map resolution.
float terrainScale = 0.1;
// And so the good book said:
// "Water shall have fresnel F0 of 0.02 and stone that of 0.035–0.056".
float terrainShininess = 50.0;
float terrainFresnel = 0.03;

bool g_showWireframe = false;
bool g_showNormals = false;


void loadShaders(bool is_reload)
{
	GLuint shader = labhelper::loadShaderProgram("../project/simple.vert", "../project/simple.frag", is_reload);
	if (shader != 0)
	{
		simpleShaderProgram = shader;
	}

	shader = labhelper::loadShaderProgram("../project/fullscreenQuad.vert", "../project/background.frag", is_reload);
	if (shader != 0)
	{
		backgroundProgram = shader;
	}

	shader = labhelper::loadShaderProgram("../project/shading.vert", "../project/shading.frag", is_reload);
	if (shader != 0)
	{
		shaderProgram = shader;
	}

	shader = labhelper::loadShaderProgram("../project/heightfield.vert", "../project/shading.frag", is_reload);
	if (shader != 0)
	{
		heightFieldProgram = shader;
	}

	// Loading explotion shader
	shader = labhelper::loadShaderProgram("../project/particle.vert", "../project/particle.frag", is_reload);
	if (shader != 0)
	{
		particleShaderProgram = shader;
	}

	////Loading simple green particle shader
	//shader = labhelper::loadShaderProgram("../project/particle_simple.vert", "../project/particle_simple.frag", is_reload);
	//if (shader != 0)
	//{
	//	particleShaderProgram = shader;
	//}

}



///////////////////////////////////////////////////////////////////////////////
/// This function is called once at the start of the program and never again
///////////////////////////////////////////////////////////////////////////////
void initialize()
{
	ENSURE_INITIALIZE_ONLY_ONCE();

	///////////////////////////////////////////////////////////////////////
	//		Load Shaders
	///////////////////////////////////////////////////////////////////////
	loadShaders(false);

	///////////////////////////////////////////////////////////////////////
	// Load models and set up model matrices
	///////////////////////////////////////////////////////////////////////
	//fighterModel = labhelper::loadModelFromOBJ("../scenes/space-ship.obj");
	landingpadModel = labhelper::loadModelFromOBJ("../scenes/landingpad.obj");

	roomModelMatrix = mat4(1.0f);
	//fighterModelMatrix = glm::translate(fighterPosition);
	landingPadModelMatrix = mat4(1.0f);
	//terrainModelMatrix = scale(vec3(1000.0f, 125.0f, 1000.0f));
	terrainModelMatrix = translate(-10.0f * worldUp) * scale(vec3(5000));

	///////////////////////////////////////////////////////////////////////
	// Load environment map
	///////////////////////////////////////////////////////////////////////
	const int roughnesses = 8;
	std::vector<std::string> filenames;
	for (int i = 0; i < roughnesses; i++)
		filenames.push_back("../scenes/envmaps/" + envmap_base_name + "_dl_" + std::to_string(i) + ".hdr");

	environmentMap = labhelper::loadHdrTexture("../scenes/envmaps/" + envmap_base_name + ".hdr");
	irradianceMap = labhelper::loadHdrTexture("../scenes/envmaps/" + envmap_base_name + "_irradiance.hdr");
	reflectionMap = labhelper::loadHdrMipmapTexture(filenames);

	///////////////////////////////////////////////////////////////////////
	// Load particle texture
	///////////////////////////////////////////////////////////////////////
	explosionTexture = loadPngTextureSTB("../scenes/textures/explosion.png");

	///////////////////////////////////////////////////////////////////////
	// Setup Framebuffer for shadow map rendering
	///////////////////////////////////////////////////////////////////////

	//shadowMapFB.resize(shadowMapResolution, shadowMapResolution);

	//glBindTexture(GL_TEXTURE_2D, shadowMapFB.depthBuffer);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);


	terrain.loadHeightField("../scenes/nlsFinland/L3123F.png");
	terrain.loadDiffuseTexture("../scenes/nlsFinland/L3123F_downscaled.jpg");
	terrain.loadShininess("../scenes/nlsFinland/L3123F_downscaled.jpg");
	terrain.generateMesh(terrainResolution);


	glEnable(GL_DEPTH_TEST);	// enable Z-buffering 
	glEnable(GL_CULL_FACE);		// enables backface culling

	///////////////////////////////////////////////////////////////////////
	// Initialize Particle System
	///////////////////////////////////////////////////////////////////////
	particleSystem.init_gpu_data();

	///////////////////////////////////////////////////////////////////////
	// Initialize GPU Compute Manager
	///////////////////////////////////////////////////////////////////////
	computeManager = new ComputeManager();
	if (computeManager->initialize()) 
	{
		std::cout << "GPU Compute Manager initialized successfully" << std::endl;

		// Setting up GPU computing for particle systems
		particleSystem.initGPUCompute(computeManager);
	}
	else {
		std::cerr << "Failed to initialize GPU Compute Manager" << std::endl;
		delete computeManager;
		computeManager = nullptr;
	}

	///////////////////////////////////////////////////////////////////////
	// Initialize Boundary Manager
	///////////////////////////////////////////////////////////////////////
	boundaryManager = new BoundaryManager(glm::vec3(0.0f, 50.0f, 0.0f), glm::vec3(10.0f, 20.0f, 10.0f));
	boundaryManager->initRenderData();

	///////////////////////////////////////////////////////////////////////
	// Initialize Smoke Physics
	///////////////////////////////////////////////////////////////////////
	PhysicsParameters physicsParams(2.5f, 0.8f, 1.0f, 1.5f); // gravity, drag, mass, flow_influence
	smokePhysics = new SmokePhysics(physicsParams);
	particleSystem.syncPhysicsFromCPU(smokePhysics);

	///////////////////////////////////////////////////////////////////////
	// Initialize Flow Field with Boundary Manager dimensions
	///////////////////////////////////////////////////////////////////////
	if (computeManager && computeManager->isReady()) 
	{
		const auto& boundingBox = boundaryManager->getBoundingBox();
		glm::vec3 flowFieldMin = boundingBox.min_bounds - glm::vec3(0.2f);
		glm::vec3 flowFieldMax = boundingBox.max_bounds + glm::vec3(0.2f);

		particleSystem.initializeFlowFieldWithBounds(flowFieldMin, flowFieldMax);
	}
}

void generateSmokeParticles()
{
	if (!isGenerate) 
	{
		return;
	}

	for (int i = 0; i < particlesPerFrame; i++)
	{
		Particle particle;

		// Position
		particle.pos = boundaryManager->generateSpawnPosition(2.0f);

		// Velocity
		constexpr const float coneHalfAngle = glm::radians(22.5f); // Half angle of a 45 degree cone
		const float baseSpeed = 10.0f;
		const float speedVariation = 0.3f; // Speed ​​variation range (±30%)

		// Generate random directions within a cone
		float phi = labhelper::uniform_randf(0.0f, 2.0f * M_PI);
		float cosTheta = labhelper::uniform_randf(cos(coneHalfAngle), 1.0f);
		float sinTheta = sqrt(1.0f - cosTheta * cosTheta);

		glm::vec3 direction = glm::vec3(sinTheta * cos(phi), cosTheta, sinTheta * sin(phi));
		float speed = baseSpeed * labhelper::uniform_randf(1.0f - speedVariation, 1.0f + speedVariation);

		particle.velocity = direction * speed;

		// life
		particle.lifetime = 0.0f;
		particle.life_length = particleLifespan;

		particleSystem.spawn(particle);
	}
}

void debugDrawLight(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix, const glm::vec3& worldSpaceLightPos)
{
	mat4 modelMatrix = glm::translate(worldSpaceLightPos);
	glUseProgram(simpleShaderProgram);
	labhelper::setUniformSlow(simpleShaderProgram, "modelViewProjectionMatrix", projectionMatrix * viewMatrix * modelMatrix);
	labhelper::setUniformSlow(simpleShaderProgram, "material_color", vec3(1, 1, 1));
	labhelper::debugDrawSphere();
}


void drawBackground(const mat4& viewMatrix, const mat4& projectionMatrix)
{
	glUseProgram(backgroundProgram);
	labhelper::setUniformSlow(backgroundProgram, "environment_multiplier", environment_multiplier);
	labhelper::setUniformSlow(backgroundProgram, "inv_PV", inverse(projectionMatrix * viewMatrix));
	labhelper::setUniformSlow(backgroundProgram, "camera_pos", cameraPosition);
	labhelper::setUniformSlow(backgroundProgram, "environmentMap", 6);
	labhelper::drawFullScreenQuad();
}

///////////////////////////////////////////////////////////////////////////////
/// This function is used to draw the main objects on the scene
///////////////////////////////////////////////////////////////////////////////
void drawScene(GLuint currentShaderProgram,
	const mat4& viewMatrix,
	const mat4& projectionMatrix,
	const mat4& lightViewMatrix,
	const mat4& lightProjectionMatrix)
{
	glUseProgram(currentShaderProgram);
	labhelper::setUniformSlow(currentShaderProgram, "showNormals", g_showNormals);
	// Light source
	vec4 viewSpaceLightPosition = viewMatrix * vec4(lightPosition, 1.0f);
	labhelper::setUniformSlow(currentShaderProgram, "point_light_color", point_light_color);
	labhelper::setUniformSlow(currentShaderProgram, "point_light_intensity_multiplier", point_light_intensity_multiplier);
	labhelper::setUniformSlow(currentShaderProgram, "viewSpaceLightPosition", vec3(viewSpaceLightPosition));
	labhelper::setUniformSlow(currentShaderProgram, "viewSpaceLightDir", normalize(vec3(viewMatrix * vec4(-lightPosition, 0.0f))));


	// Environment
	labhelper::setUniformSlow(currentShaderProgram, "environment_multiplier", environment_multiplier);

	// camera
	labhelper::setUniformSlow(currentShaderProgram, "viewInverse", inverse(viewMatrix));

	// landing pad
	labhelper::setUniformSlow(currentShaderProgram, "modelViewProjectionMatrix",
		projectionMatrix * viewMatrix * landingPadModelMatrix);
	labhelper::setUniformSlow(currentShaderProgram, "modelViewMatrix", viewMatrix * landingPadModelMatrix);
	labhelper::setUniformSlow(currentShaderProgram, "normalMatrix",
		inverse(transpose(viewMatrix * landingPadModelMatrix)));

	labhelper::render(landingpadModel);

	// Fighter
	//labhelper::setUniformSlow(currentShaderProgram, "modelViewProjectionMatrix",
	//	projectionMatrix * viewMatrix * fighterModelMatrix);
	//labhelper::setUniformSlow(currentShaderProgram, "modelViewMatrix", viewMatrix * fighterModelMatrix);
	//labhelper::setUniformSlow(currentShaderProgram, "normalMatrix",
	//	inverse(transpose(viewMatrix * fighterModelMatrix)));

	//labhelper::render(fighterModel);
}




void drawTerrain(const mat4& viewMatrix, const mat4& projectionMatrix, const mat4& lightViewMatrix, const mat4& lightProjectionMatrix) {

	if (g_showWireframe)
	{
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	}

	glUseProgram(heightFieldProgram);

	// Configuration
	labhelper::setUniformSlow(heightFieldProgram, "tesselation", terrainResolution);
	labhelper::setUniformSlow(heightFieldProgram, "scale", terrainScale);
	labhelper::setUniformSlow(heightFieldProgram, "showNormals", g_showNormals);

	// Material parameters.
	// Both water and land are dielectrics.
	labhelper::setUniformSlow(heightFieldProgram, "material_metalness", 0.0f);
	labhelper::setUniformSlow(heightFieldProgram, "material_fresnel", terrainFresnel);
	labhelper::setUniformSlow(heightFieldProgram, "material_shininess", terrainShininess);

	// Fragment shader parameters.
	vec4 viewSpaceLightPosition = viewMatrix * vec4(lightPosition, 1.0f);
	labhelper::setUniformSlow(heightFieldProgram, "point_light_color", point_light_color);
	labhelper::setUniformSlow(heightFieldProgram, "point_light_intensity_multiplier", point_light_intensity_multiplier);
	labhelper::setUniformSlow(heightFieldProgram, "viewSpaceLightPosition", vec3(viewSpaceLightPosition));
	labhelper::setUniformSlow(heightFieldProgram, "viewSpaceLightDir",
		normalize(vec3(viewMatrix * vec4(-lightPosition, 0.0f))));
	labhelper::setUniformSlow(heightFieldProgram, "environment_multiplier", environment_multiplier);
	labhelper::setUniformSlow(heightFieldProgram, "viewInverse", inverse(viewMatrix));

	// Configure textures.
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, terrain.m_texid_hf);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, terrain.m_texid_diffuse);
	glActiveTexture(GL_TEXTURE3);
	glBindTexture(GL_TEXTURE_2D, terrain.m_texid_shininess);
	glActiveTexture(GL_TEXTURE6);
	glBindTexture(GL_TEXTURE_2D, environmentMap);
	glActiveTexture(GL_TEXTURE7);
	glBindTexture(GL_TEXTURE_2D, irradianceMap);
	glActiveTexture(GL_TEXTURE8);
	glBindTexture(GL_TEXTURE_2D, reflectionMap);
	glActiveTexture(GL_TEXTURE0);

	labhelper::setUniformSlow(heightFieldProgram, "heightField", 1);
	labhelper::setUniformSlow(heightFieldProgram, "color_texture", 2); //colorMap //color_texture
	labhelper::setUniformSlow(heightFieldProgram, "shininess_texture", 3); //shininessMap //shininess_texture
	labhelper::setUniformSlow(heightFieldProgram, "environmentMap", 6);
	labhelper::setUniformSlow(heightFieldProgram, "irradianceMap", 7);
	labhelper::setUniformSlow(heightFieldProgram, "reflectionMap", 8);

	labhelper::setUniformSlow(heightFieldProgram, "has_color_texture", 1);
	labhelper::setUniformSlow(heightFieldProgram, "has_shininess_texture", 1);

	// Set matrices.
	labhelper::setUniformSlow(heightFieldProgram, "modelViewProjectionMatrix",
		projectionMatrix * viewMatrix * terrainModelMatrix);
	labhelper::setUniformSlow(heightFieldProgram, "modelViewMatrix", viewMatrix * terrainModelMatrix);
	labhelper::setUniformSlow(heightFieldProgram, "normalMatrix", inverse(transpose(viewMatrix * terrainModelMatrix)));
	terrain.submitTriangles();

	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glUseProgram(0);


}




///////////////////////////////////////////////////////////////////////////////
/// This function will be called once per frame, so the code to set up
/// the scene for rendering should go here
///////////////////////////////////////////////////////////////////////////////
void display(void)
{
	///////////////////////////////////////////////////////////////////////////
	// Check if window size has changed and resize buffers as needed
	///////////////////////////////////////////////////////////////////////////
	{
		int w, h;
		SDL_GetWindowSize(g_window, &w, &h);
		if (w != windowWidth || h != windowHeight)
		{
			windowWidth = w;
			windowHeight = h;
		}
	}
	///////////////////////////////////////////////////////////////////////////
	// Re-tesselate the terrain if we changed the resolution.
	///////////////////////////////////////////////////////////////////////////
	if (terrain.m_meshResolution != terrainResolution)
	{
		terrain.generateMesh(terrainResolution);
	}

	///////////////////////////////////////////////////////////////////////////
	// setup matrices
	///////////////////////////////////////////////////////////////////////////
	mat4 projMatrix = perspective(radians(45.0f), float(windowWidth) / float(windowHeight), 5.0f, 2000.0f);
	mat4 viewMatrix = lookAt(cameraPosition, cameraPosition + cameraDirection, worldUp);

	vec4 lightStartPosition = vec4(40.0f, 40.0f, 0.0f, 1.0f);
	lightPosition = vec3(rotate(currentTime, worldUp) * lightStartPosition);
	mat4 lightViewMatrix = lookAt(lightPosition, vec3(0.0f), worldUp);
	mat4 lightProjMatrix = perspective(radians(45.0f), 1.0f, 25.0f, 100.0f);

	///////////////////////////////////////////////////////////////////////////
	// Bind the environment map(s) to unused texture units
	///////////////////////////////////////////////////////////////////////////

	glUseProgram(shaderProgram);

	glActiveTexture(GL_TEXTURE6);
	glBindTexture(GL_TEXTURE_2D, environmentMap);
	glActiveTexture(GL_TEXTURE7);
	glBindTexture(GL_TEXTURE_2D, irradianceMap);
	glActiveTexture(GL_TEXTURE8);
	glBindTexture(GL_TEXTURE_2D, reflectionMap);


	labhelper::setUniformSlow(shaderProgram, "reflectionMap", 8); //
	labhelper::setUniformSlow(shaderProgram, "environmentMap", 6);
	labhelper::setUniformSlow(shaderProgram, "irradianceMap", 7);



	///////////////////////////////////////////////////////////////////////////
	// Draw from camera
	///////////////////////////////////////////////////////////////////////////
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, windowWidth, windowHeight);
	glClearColor(0.2f, 0.2f, 0.8f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	drawBackground(viewMatrix, projMatrix);
	drawScene(shaderProgram, viewMatrix, projMatrix, lightViewMatrix, lightProjMatrix);
	drawTerrain(viewMatrix, projMatrix, lightViewMatrix, lightProjMatrix);

	debugDrawLight(viewMatrix, projMatrix, vec3(lightPosition));

	///////////////////////////////////////////////////////////////////////////
	// GPU Particle Update and Rendering
	///////////////////////////////////////////////////////////////////////////
	particleSystem.updateParticlesGPU(deltaTime, currentTime);
	drawSmokeParticles(viewMatrix, projMatrix);
	debugDrawLight(viewMatrix, projMatrix, vec3(lightPosition));

	///////////////////////////////////////////////////////////////////////////
	// Render Boundary Box
	///////////////////////////////////////////////////////////////////////////
	boundaryManager->renderBoundary(viewMatrix, projMatrix, simpleShaderProgram);
}

void drawSmokeParticles(const mat4& viewMatrix, const mat4& projectionMatrix)
{
	generateSmokeParticles();

	// no particles, no rendering
	if (particleSystem.get_particle_count() == 0) 
	{
		return;
	}

	glEnable(GL_PROGRAM_POINT_SIZE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);

	// particle shader program
	glUseProgram(particleShaderProgram);
	labhelper::setUniformSlow(particleShaderProgram, "P", projectionMatrix);
	labhelper::setUniformSlow(particleShaderProgram, "screen_x", float(windowWidth));
	labhelper::setUniformSlow(particleShaderProgram, "screen_y", float(windowHeight));

	// Bind the explosion texture to texture unit 0
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, explosionTexture);
	labhelper::setUniformSlow(particleShaderProgram, "colortexture", 0);

	///////// RENDER /////////
	particleSystem.submit_to_gpu(viewMatrix);
	glBindVertexArray(particleSystem.getVAO());
	glDrawArrays(GL_POINTS, 0, particleSystem.get_particle_count());
	glBindVertexArray(0);

	// Restoring OpenGL state
	glDepthMask(GL_TRUE);             
	glDisable(GL_BLEND);               
	glDisable(GL_PROGRAM_POINT_SIZE); 
	glUseProgram(0);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
}


///////////////////////////////////////////////////////////////////////////////
/// This function is used to update the scene according to user input
///////////////////////////////////////////////////////////////////////////////

bool handleEvents(void)
{
	// Allow ImGui to capture events.
	ImGuiIO& io = ImGui::GetIO();

	// check events (keyboard among other)
	SDL_Event event;
	bool quitEvent = false;
	while (SDL_PollEvent(&event))
	{
		ImGui_ImplSdlGL3_ProcessEvent(&event);

		if (event.type == SDL_QUIT || (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_ESCAPE))
		{
			quitEvent = true;
		}
		else if (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_g)
		{
			showUI = !showUI;
		}
		else if (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_PRINTSCREEN)
		{
			labhelper::saveScreenshot();
		}
		if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT
			&& (!showUI || !io.WantCaptureMouse))
		{
			g_isMouseDragging = true;
			int x;
			int y;
			SDL_GetMouseState(&x, &y);
			g_prevMouseCoords.x = x;
			g_prevMouseCoords.y = y;
		}

		if (!(SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT)))
		{
			g_isMouseDragging = false;
		}

		if (event.type == SDL_MOUSEMOTION && g_isMouseDragging && !io.WantCaptureMouse)
		{
			// More info at https://wiki.libsdl.org/SDL_MouseMotionEvent
			int delta_x = event.motion.x - g_prevMouseCoords.x;
			int delta_y = event.motion.y - g_prevMouseCoords.y;
			float rotationSpeed = 0.4f;
			mat4 yaw = rotate(rotationSpeed * deltaTime * -delta_x, worldUp);
			mat4 pitch = rotate(rotationSpeed * deltaTime * -delta_y,
				normalize(cross(cameraDirection, worldUp)));
			cameraDirection = vec3(pitch * yaw * vec4(cameraDirection, 0.0f));
			g_prevMouseCoords.x = event.motion.x;
			g_prevMouseCoords.y = event.motion.y;
		}
	}

	// check keyboard state (which keys are still pressed)
	const uint8_t* state = SDL_GetKeyboardState(nullptr);

	static bool was_shift_pressed = state[SDL_SCANCODE_LSHIFT];
	if (was_shift_pressed && !state[SDL_SCANCODE_LSHIFT])
	{
		cameraSpeed /= 5;
	}
	if (!was_shift_pressed && state[SDL_SCANCODE_LSHIFT])
	{
		cameraSpeed *= 5;
	}
	was_shift_pressed = state[SDL_SCANCODE_LSHIFT];


	vec3 cameraRight = cross(cameraDirection, worldUp);

	if (state[SDL_SCANCODE_W])
	{
		cameraPosition += cameraSpeed * deltaTime * cameraDirection;
	}
	if (state[SDL_SCANCODE_S])
	{
		cameraPosition -= cameraSpeed * deltaTime * cameraDirection;
	}
	if (state[SDL_SCANCODE_A])
	{
		cameraPosition -= cameraSpeed * deltaTime * cameraRight;
	}
	if (state[SDL_SCANCODE_D])
	{
		cameraPosition += cameraSpeed * deltaTime * cameraRight;
	}
	if (state[SDL_SCANCODE_Q])
	{
		cameraPosition -= cameraSpeed * deltaTime * worldUp;
	}
	if (state[SDL_SCANCODE_E])
	{
		cameraPosition += cameraSpeed * deltaTime * worldUp;
	}

	isGenerate = false;

	// Spacecraft forward control(Space bar)
	if (state[SDL_SCANCODE_SPACE])
	{
		isGenerate = true;
	}

	return quitEvent;
}


///////////////////////////////////////////////////////////////////////////////
/// This function is to hold the general GUI logic
///////////////////////////////////////////////////////////////////////////////
void gui()
{
	// ----------------- GPU Compute Info ----------------
	ImGui::Separator();
	ImGui::Text("GPU Compute Status");
	if (computeManager && computeManager->isReady()) {
		ImGui::Text("GPU Compute: ENABLED");
		// Display GPU counter information
		if (ImGui::Button("Compact Particles")) {
			particleSystem.compactParticles();
		}
	}
	else {
		ImGui::Text("GPU Compute: DISABLED");
	}

	// ----------------- GPU Particle System Control ----------------
	ImGui::Separator();
	ImGui::Text("GPU Particle System");
	ImGui::Text("Active particles: %d", particleSystem.get_particle_count());
	ImGui::Text("Alive particles: %d", particleSystem.getAliveParticleCount());
	ImGui::SliderFloat("Particle Lifespan", &particleLifespan, 1.0f, 10.0f);
	ImGui::SliderInt("Particles Per Frame", &particlesPerFrame, 1, 200);

	// ----------------- Boundary Control ----------------
	ImGui::Separator();
	ImGui::Text("Smoke Boundary");
	glm::vec3 center = boundaryManager->getBoundingBox().center;
	glm::vec3 size = boundaryManager->getBoundingBox().size;

	if (ImGui::DragFloat3("Boundary Center", &center.x, 0.5f, -50.0f, 50.0f)) {
		boundaryManager->setBoundary(center, size);
	}
	if (ImGui::DragFloat3("Boundary Size", &size.x, 0.5f, 5.0f, 100.0f)) {
		boundaryManager->setBoundary(center, size);
	}

	// ----------------- GPU Physics Control ----------------
	ImGui::Separator();
	ImGui::Text("GPU Physics Parameters");

	if (computeManager && computeManager->isReady() && smokePhysics) {
		auto currentParams = smokePhysics->getParameters();
		static float gravity = currentParams.gravity_strength;
		static float dragCoeff = currentParams.drag_coefficient;
		static float particleMass = currentParams.particle_mass;

		bool physicsChanged = false;

		if (ImGui::SliderFloat("Gravity", &gravity, 0.0f, 20.0f, "%.2f")) {
			physicsChanged = true;
		}
		if (ImGui::SliderFloat("Drag Coefficient", &dragCoeff, 0.0f, 2.0f, "%.3f")) {
			physicsChanged = true;
		}
		if (ImGui::SliderFloat("Particle Mass", &particleMass, 0.1f, 5.0f, "%.2f")) {
			physicsChanged = true;
		}

		if (physicsChanged) {
			PhysicsParameters newParams(gravity, dragCoeff, particleMass, currentParams.flow_influence);
			smokePhysics->setParameters(newParams);
			particleSystem.syncPhysicsFromCPU(smokePhysics);

			PhysicsParametersGPU gpuParams(gravity, dragCoeff, particleMass);
			computeManager->setPhysicsParameters(gpuParams);
		}

		const auto& displayParams = smokePhysics->getParameters();
		ImGui::Text("Current: G=%.2f, D=%.3f, M=%.2f",
			displayParams.gravity_strength, displayParams.drag_coefficient, displayParams.particle_mass);
	}

	// ----------------- GPU Flow Field Control ----------------
	ImGui::Separator();
	ImGui::Text("GPU Flow Field");

	if (computeManager && computeManager->isReady()) {
		static bool flowFieldEnabled = false;
		static float windDirection[3] = { 1.0f, 0.2f, 0.0f };
		static float windStrength = 3.0f;
		static float flowInfluence = 1.5f;

		bool flowFieldChanged = false;

		if (ImGui::Checkbox("Enable Flow Field", &flowFieldEnabled)) 
		{
			particleSystem.enableFlowField(flowFieldEnabled);
		}

		if (flowFieldEnabled) 
		{
			if (ImGui::SliderFloat3("Wind Direction", windDirection, -1.0f, 1.0f, "%.2f")) {
				flowFieldChanged = true;
			}

			if (ImGui::SliderFloat("Wind Strength", &windStrength, 0.0f, 10.0f, "%.2f")) {
				flowFieldChanged = true;
			}

			if (ImGui::SliderFloat("Flow Influence", &flowInfluence, 0.0f, 5.0f, "%.2f")) {
				flowFieldChanged = true;
			}

			if (flowFieldChanged) 
			{
				glm::vec3 windDir(windDirection[0], windDirection[1], windDirection[2]);
				particleSystem.setFlowFieldParameters(windDir, windStrength, flowInfluence);
			}

			ImGui::Text("Current Wind: (%.2f, %.2f, %.2f)", windDirection[0], windDirection[1], windDirection[2]);
			ImGui::Text("Strength: %.2f, Influence: %.2f", windStrength, flowInfluence);
		}
	}

}

int main(int argc, char* argv[])
{
	g_window = labhelper::init_window_SDL("OpenGL Project");

	initialize();

	bool stopRendering = false;
	auto startTime = std::chrono::system_clock::now();

	while (!stopRendering)
	{
		//update currentTime
		std::chrono::duration<float> timeSinceStart = std::chrono::system_clock::now() - startTime;
		previousTime = currentTime;
		currentTime = timeSinceStart.count();
		deltaTime = currentTime - previousTime;

		// Inform imgui of new frame
		ImGui_ImplSdlGL3_NewFrame(g_window);

		// check events (keyboard among other)
		stopRendering = handleEvents();

		// render to window
		display();

		// Render overlay GUI.
		if (showUI)
		{
			gui();
		}

		// Render the GUI.
		ImGui::Render();

		// Swap front and back buffer. This frame will now been displayed.
		SDL_GL_SwapWindow(g_window);
	}

	// Free particleSystem
	//delete particleSystem;

	// Free boundary manager
	delete boundaryManager;
	delete computeManager;
	delete smokePhysics;

	// Free Models
	//labhelper::freeModel(fighterModel);
	labhelper::freeModel(landingpadModel);

	// Shut down everything. This includes the window and all other subsystems.
	labhelper::shutDown(g_window);
	return 0;
}
