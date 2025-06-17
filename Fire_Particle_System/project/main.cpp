

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

#include "stb_image.h"




using std::min;
using std::max;

///////////////////////////////////////////////////////////////////////////////
// Function declarations
///////////////////////////////////////////////////////////////////////////////
void generateTestParticles();
void drawParticles(const mat4& viewMatrix, const mat4& projectionMatrix); 
void drawExplodeParticles(const mat4& viewMatrix, const mat4& projectionMatrix);
void generateParticlesPerFrame();
void updateFighterTransform();
void generateEngineParticles();

///////////////////////////////////////////////////////////////////////////////
// Various globals
///////////////////////////////////////////////////////////////////////////////
SDL_Window* g_window = nullptr;
float currentTime = 0.0f;
float previousTime = 0.0f;
float deltaTime = 0.0f;
bool showUI = false;
int windowWidth, windowHeight;

// Mouse input
ivec2 g_prevMouseCoords = { -1, -1 };
bool g_isMouseDragging = false;

// Particle System
//ParticleSystem* particleSystem = nullptr;
ParticleSystem particleSystem(10000);
GLuint particleShaderProgram = 0;

// Aircraft engine exhaust port location
glm::vec3 particleSpawnOffset = glm::vec3(8.0f, 4.0f, 0.0f);

// Spacecraft control
glm::vec3 fighterPosition = glm::vec3(0.0f, 15.0f, 0.0f);  // position
float fighterYaw = 0.0f;
float fighterPitch = 0.0f;
float fighterRoll = 0.0f;
bool isMovingForward = false;

// Explosion Texture
GLuint explosionTexture = 0;

// Particle Control Parameters
float particleBaseSize = 20.0f;      
float particleSpeedMultiplier = 15.0f;
float particleLifespan = 3.0f;
int particlesPerFrame = 64;


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
labhelper::Model* fighterModel = nullptr;
labhelper::Model* landingpadModel = nullptr;

mat4 roomModelMatrix;
mat4 landingPadModelMatrix;
mat4 fighterModelMatrix;


float shipSpeed = 50;


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

	//Loading simple green particle shader
	//shader = labhelper::loadShaderProgram("../project/particle_simple.vert", "../project/particle_simple.frag", is_reload);
	//if (shader != 0)
	//{
	//	particleShaderProgram = shader;
	//}

	// Loading explotion shader
	shader = labhelper::loadShaderProgram("../project/particle.vert", "../project/particle.frag", is_reload);
	if (shader != 0)
	{
		particleShaderProgram = shader;
	}

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
	fighterModel = labhelper::loadModelFromOBJ("../scenes/space-ship.obj");
	landingpadModel = labhelper::loadModelFromOBJ("../scenes/landingpad.obj");

	roomModelMatrix = mat4(1.0f);
	fighterModelMatrix = glm::translate(fighterPosition);
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

	//generateTestParticles();
}

void generateTestParticles()
{
	const int NUM_TEST_PARTICLES = 10000;

	for (int i = 0; i < NUM_TEST_PARTICLES; i++)
	{
		// Spherical sampling formula
		const float theta = labhelper::uniform_randf(0.f, 2.f * M_PI);
		const float u = labhelper::uniform_randf(-1.f, 1.f);
		glm::vec3 pos = glm::vec3(sqrt(1.f - u * u) * cosf(theta),u,sqrt(1.f - u * u) * sinf(theta));

		pos *= 10.0f;
		pos += glm::vec3(fighterModelMatrix[3]);

		Particle particle;
		particle.pos = pos;
		particle.velocity = glm::vec3(0.0f);
		particle.lifetime = 0.0f;
		particle.life_length = 10000.0f; 

		// Add to Particle System
		//int before_size = particleSystem->particles.size();
		particleSystem.spawn(particle);
		//int after_size = particleSystem->particles.size();  
	}
}

void generateParticlesPerFrame()
{
	const int PARTICLES_PER_FRAME = 64;

	for (int i = 0; i < PARTICLES_PER_FRAME; i++)
	{
		// Spherical sampling formula
		const float theta = labhelper::uniform_randf(0.f, 2.f * M_PI);
		const float u = labhelper::uniform_randf(0.95f, 1.f);
		glm::vec3 direction = glm::vec3(
			u,
			sqrt(1.f - u * u) * cosf(theta),
			sqrt(1.f - u * u) * sinf(theta)
		);

		Particle particle;

		glm::vec3 localPos = particleSpawnOffset; 
		glm::vec4 worldPos = fighterModelMatrix * glm::vec4(localPos, 1.0f);
		particle.pos = glm::vec3(worldPos);

		glm::mat3 rotationMatrix = glm::mat3(fighterModelMatrix);
		particle.velocity = rotationMatrix * direction * 10.0f;

		particle.lifetime = 0.0f;
		particle.life_length = 5.0f;

		particleSystem.spawn(particle);
	}
}

void generateEngineParticles()
{
	if (!isMovingForward) 
	{
		return;
	}

	for (int i = 0; i < particlesPerFrame; i++)
	{
		const float theta = labhelper::uniform_randf(0.f, 2.f * M_PI);
		const float u = labhelper::uniform_randf(0.95f, 1.f);
		glm::vec3 direction = glm::vec3(
			u,
			sqrt(1.f - u * u) * cosf(theta) * 0.3f,
			sqrt(1.f - u * u) * sinf(theta) * 0.3f
		);

		Particle particle;

		// Position
		glm::vec3 localPos = particleSpawnOffset;
		glm::vec4 worldPos = fighterModelMatrix * glm::vec4(localPos, 1.0f);
		particle.pos = glm::vec3(worldPos);
		particle.pos += glm::vec3(
			labhelper::uniform_randf(-0.2f, 0.2f),
			labhelper::uniform_randf(-0.2f, 0.2f),
			labhelper::uniform_randf(-0.2f, 0.2f)
		);

		// Velocity
		glm::mat3 rotationMatrix = glm::mat3(fighterModelMatrix);
		particle.velocity = rotationMatrix * direction * 15.0f;

		particle.lifetime = 0.0f;
		particle.life_length = 3.0f;

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
	labhelper::setUniformSlow(currentShaderProgram, "modelViewProjectionMatrix",
		projectionMatrix * viewMatrix * fighterModelMatrix);
	labhelper::setUniformSlow(currentShaderProgram, "modelViewMatrix", viewMatrix * fighterModelMatrix);
	labhelper::setUniformSlow(currentShaderProgram, "normalMatrix",
		inverse(transpose(viewMatrix * fighterModelMatrix)));

	labhelper::render(fighterModel);
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
	// Rendering Particle Systems
	///////////////////////////////////////////////////////////////////////////
	//drawParticles(viewMatrix, projMatrix);
	drawExplodeParticles(viewMatrix, projMatrix);
	debugDrawLight(viewMatrix, projMatrix, vec3(lightPosition));
}

void drawParticles(const mat4& viewMatrix, const mat4& projectionMatrix)
{
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);

	glUseProgram(particleShaderProgram);
	labhelper::setUniformSlow(particleShaderProgram, "projectionMatrix", projectionMatrix);

	// Generate new particles every frame
	//generateParticlesPerFrame();
	generateEngineParticles();

	particleSystem.process_particles(deltaTime);
	particleSystem.submit_to_gpu(viewMatrix);

	// render
	glBindVertexArray(particleSystem.getVAO());
	glDrawArrays(GL_POINTS, 0, particleSystem.get_particle_count());
	glBindVertexArray(0);

	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glUseProgram(0);
}

void drawExplodeParticles(const mat4& viewMatrix, const mat4& projectionMatrix)
{
	generateEngineParticles();
	particleSystem.process_particles(deltaTime);

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

	// 设置屏幕尺寸（用于点大小缩放）
	labhelper::setUniformSlow(particleShaderProgram, "screen_x", float(windowWidth));
	labhelper::setUniformSlow(particleShaderProgram, "screen_y", float(windowHeight));

	// Bind the explosion texture to texture unit 0
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, explosionTexture);
	labhelper::setUniformSlow(particleShaderProgram, "colortexture", 0);

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
void updateFighterTransform()
{
	glm::mat4 yawMatrix = glm::rotate(glm::radians(fighterYaw), glm::vec3(0, 1, 0));
	glm::mat4 pitchMatrix = glm::rotate(glm::radians(fighterPitch), glm::vec3(0, 0, 1));
	glm::mat4 rollMatrix = glm::rotate(glm::radians(fighterRoll), glm::vec3(1, 0, 0));

	fighterModelMatrix = glm::translate(fighterPosition) * yawMatrix * pitchMatrix * rollMatrix;
}

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

	// Spaceship controls (using arrow keys and other keys)
	float shipRotationSpeed = 60.0f;
	float shipMoveSpeed = 20.0f;
	isMovingForward = false;

	// Spaceship Yaw control
	if (state[SDL_SCANCODE_LEFT])
	{
		fighterYaw += shipRotationSpeed * deltaTime;
	}
	if (state[SDL_SCANCODE_RIGHT])
	{
		fighterYaw -= shipRotationSpeed * deltaTime;
	}
	// Spaceship Pitch control
	if (state[SDL_SCANCODE_UP])
	{
		fighterPitch += shipRotationSpeed * deltaTime;
	}
	if (state[SDL_SCANCODE_DOWN])
	{
		fighterPitch -= shipRotationSpeed * deltaTime;
	}
	// Spacecraft Roll control
	if (state[SDL_SCANCODE_Z])
	{
		fighterRoll += shipRotationSpeed * deltaTime;
	}
	if (state[SDL_SCANCODE_X])
	{
		fighterRoll -= shipRotationSpeed * deltaTime;
	}

	// Spacecraft forward control(Space bar)
	if (state[SDL_SCANCODE_SPACE])
	{
		isMovingForward = true;
		// Forward direction (-X direction)
		glm::vec3 forwardDir = glm::vec3(-fighterModelMatrix[0]);
		fighterPosition += forwardDir * shipMoveSpeed * deltaTime;
	}
	updateFighterTransform();

	return quitEvent;
}


///////////////////////////////////////////////////////////////////////////////
/// This function is to hold the general GUI logic
///////////////////////////////////////////////////////////////////////////////
void gui()
{
	// ----------------- Set variables --------------------------
	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate,
		ImGui::GetIO().Framerate);
	ImGui::SliderInt("Tesselation", &terrainResolution, 1, 1500);
	ImGui::SliderFloat("Terrain scale", &terrainScale, 0.0f, 1.0f);
	ImGui::SliderFloat("Terrain shininess", &terrainShininess, 0.0f, 100.0f);
	ImGui::SliderFloat("Terrain fresnel", &terrainFresnel, 0.0f, 1.0f);
	ImGui::Checkbox("Show wireframe", &g_showWireframe);
	ImGui::Checkbox("Show normals", &g_showNormals);

	// ----------------- Particle System Control ----------------
	ImGui::Separator();
	ImGui::Text("Particle System");
	ImGui::DragFloat3("Particle spawn offset", &particleSpawnOffset.x, 0.1f, -20.0f, 20.0f);
	ImGui::Text("Active particles: %d", particleSystem.get_particle_count());

	// ----------------- Spacecraft control information ---------
	ImGui::Separator();
	ImGui::Text("Ship Controls");
	ImGui::Text("Position: (%.1f, %.1f, %.1f)", fighterPosition.x, fighterPosition.y, fighterPosition.z);
	ImGui::Text("Rotation: Y=%.1f°, P=%.1f°, R=%.1f°", fighterYaw, fighterPitch, fighterRoll);
	ImGui::Text("Controls:");
	ImGui::Text("  Arrow Keys: Rotate");
	ImGui::Text("  Z/X: Roll");
	ImGui::Text("  Space: Forward + Engine");
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

	// Free Models
	labhelper::freeModel(fighterModel);
	labhelper::freeModel(landingpadModel);

	// Shut down everything. This includes the window and all other subsystems.
	labhelper::shutDown(g_window);
	return 0;
}
