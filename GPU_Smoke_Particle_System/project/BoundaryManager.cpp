#include "BoundaryManager.h"
#include "ParticleSystem.h"
#include <labhelper.h>
#include <cmath>
#include <random>

BoundaryManager::BoundaryManager(const glm::vec3& center, const glm::vec3& size)
    : wireframeVAO(0), wireframeVBO(0), wireframeEBO(0)
{
    setBoundary(center, size);
}

BoundaryManager::~BoundaryManager()
{
    if (wireframeVAO != 0) {
        glDeleteVertexArrays(1, &wireframeVAO);
    }
    if (wireframeVBO != 0) {
        glDeleteBuffers(1, &wireframeVBO);
    }
    if (wireframeEBO != 0) {
        glDeleteBuffers(1, &wireframeEBO);
    }
}

void BoundaryManager::setBoundary(const glm::vec3& center, const glm::vec3& size)
{
    boundingBox.center = center;
    boundingBox.size = size;
    updateBounds();
}

void BoundaryManager::updateBounds()
{
    glm::vec3 halfSize = boundingBox.size * 0.5f;
    boundingBox.min_bounds = boundingBox.center - halfSize;
    boundingBox.max_bounds = boundingBox.center + halfSize;
}

void BoundaryManager::initRenderData()
{
    createWireframeGeometry();
}

void BoundaryManager::createWireframeGeometry()
{
    float vertices[] = {
        // Bottom (y = min)
        boundingBox.min_bounds.x, boundingBox.min_bounds.y, boundingBox.min_bounds.z,
        boundingBox.max_bounds.x, boundingBox.min_bounds.y, boundingBox.min_bounds.z,
        boundingBox.max_bounds.x, boundingBox.min_bounds.y, boundingBox.max_bounds.z,
        boundingBox.min_bounds.x, boundingBox.min_bounds.y, boundingBox.max_bounds.z, 

        // Top (y = max)
        boundingBox.min_bounds.x, boundingBox.max_bounds.y, boundingBox.min_bounds.z,
        boundingBox.max_bounds.x, boundingBox.max_bounds.y, boundingBox.min_bounds.z,
        boundingBox.max_bounds.x, boundingBox.max_bounds.y, boundingBox.max_bounds.z, 
        boundingBox.min_bounds.x, boundingBox.max_bounds.y, boundingBox.max_bounds.z 
    };

    // Wireframe Index (12 edges)
    unsigned int indices[] = {
        // Bottom Edge
        0, 1,  1, 2,  2, 3,  3, 0,
        // Top Edge
        4, 5,  5, 6,  6, 7,  7, 4,
        // Vertical Edge
        0, 4,  1, 5,  2, 6,  3, 7
    };

    glGenVertexArrays(1, &wireframeVAO);
    glBindVertexArray(wireframeVAO);

    glGenBuffers(1, &wireframeVBO);
    glBindBuffer(GL_ARRAY_BUFFER, wireframeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glGenBuffers(1, &wireframeEBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, wireframeEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // Setting vertex attributes
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
}

bool BoundaryManager::checkAndHandleCollision(Particle& particle)
{
    bool collided = false;
    glm::vec3& pos = particle.pos;
    glm::vec3& vel = particle.velocity;

    // Check the X-axis boundaries
    if (pos.x <= boundingBox.min_bounds.x) {
        pos.x = boundingBox.min_bounds.x;
        vel.x = abs(vel.x);
        collided = true;
    }
    else if (pos.x >= boundingBox.max_bounds.x) {
        pos.x = boundingBox.max_bounds.x;
        vel.x = -abs(vel.x);
        collided = true;
    }

    // Check Y-axis bounds
    if (pos.y <= boundingBox.min_bounds.y) {
        pos.y = boundingBox.min_bounds.y;
        vel.y = abs(vel.y);
        collided = true;
    }
    else if (pos.y >= boundingBox.max_bounds.y) {
        pos.y = boundingBox.max_bounds.y;
        vel.y = -abs(vel.y);
        collided = true;
    }

    // Check Z-axis boundaries
    if (pos.z <= boundingBox.min_bounds.z) {
        pos.z = boundingBox.min_bounds.z;
        vel.z = abs(vel.z);
        collided = true;
    }
    else if (pos.z >= boundingBox.max_bounds.z) {
        pos.z = boundingBox.max_bounds.z;
        vel.z = -abs(vel.z);
        collided = true;
    }

    return collided;
}

glm::vec3 BoundaryManager::generateSpawnPosition(float radius)
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<float> dis(0.0f, 1.0f);

    // Randomly generate positions within a circular area
    float angle = dis(gen) * 2.0f * M_PI;
    float r = sqrt(dis(gen)) * radius;

    // Generate position on the bottom of the cube
    glm::vec3 spawnPos;
    spawnPos.x = boundingBox.center.x + r * cos(angle);
    spawnPos.y = boundingBox.min_bounds.y + 0.1f; // Slightly above the bottom
    spawnPos.z = boundingBox.center.z + r * sin(angle);

    // Make sure to stay within the boundaries
    spawnPos.x = glm::clamp(spawnPos.x, boundingBox.min_bounds.x + 0.1f, boundingBox.max_bounds.x - 0.1f);
    spawnPos.z = glm::clamp(spawnPos.z, boundingBox.min_bounds.z + 0.1f, boundingBox.max_bounds.z - 0.1f);

    return spawnPos;
}

void BoundaryManager::renderBoundary(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix, GLuint shaderProgram)
{
    if (wireframeVAO == 0) 
    {
        return; 
    }

    createWireframeGeometry();

    glUseProgram(shaderProgram);
    glLineWidth(2.5f);

    glm::mat4 modelMatrix = glm::mat4(1.0f);
    glm::mat4 mvpMatrix = projectionMatrix * viewMatrix * modelMatrix;

    labhelper::setUniformSlow(shaderProgram, "modelViewProjectionMatrix", mvpMatrix);
    labhelper::setUniformSlow(shaderProgram, "material_color", glm::vec3(1.0f, 0.0f, 0.0f));

    // Rendering Wireframe
    glBindVertexArray(wireframeVAO);
    glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}