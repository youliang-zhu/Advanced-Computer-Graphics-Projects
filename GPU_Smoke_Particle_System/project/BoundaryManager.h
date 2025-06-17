#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <GL/glew.h>

struct BoundingBox
{
    glm::vec3 center;
    glm::vec3 size;
    glm::vec3 min_bounds;
    glm::vec3 max_bounds;
};

struct Particle;

class BoundaryManager
{
public:
    /// Creates a cube boundary with specified center and size
    BoundaryManager(const glm::vec3& center, const glm::vec3& size);

    ~BoundaryManager();

    /// Initialize GPU data for rendering bounding boxes
    void initRenderData();

    /// Check if the particle collides with the boundary and handles the bounce if it does
    bool checkAndHandleCollision(Particle& particle);

    /// Generate random positions in the circular area at the bottom of the cube
    glm::vec3 generateSpawnPosition(float radius = 5.0f);

    /// Render bounding box wireframe
    void renderBoundary(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix, GLuint shaderProgram);

    /// Get boundary information
    const BoundingBox& getBoundingBox() const { return boundingBox; }

    /// Setting Boundaries
    void setBoundary(const glm::vec3& center, const glm::vec3& size);

private:
    BoundingBox boundingBox;

    GLuint wireframeVAO;
    GLuint wireframeVBO;
    GLuint wireframeEBO;

    /// Update boundary calculation
    void updateBounds();

    /// 创建线框几何数据
    void createWireframeGeometry();
};