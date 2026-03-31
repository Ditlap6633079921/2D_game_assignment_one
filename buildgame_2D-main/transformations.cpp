#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <stb_image.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <learnopengl/filesystem.h>
#include <learnopengl/shader_s.h>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm> // std::clamp

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow *window);

// settings
const unsigned int SCR_WIDTH = 800;
const unsigned int SCR_HEIGHT = 600;

// ---- DVD bounce + grow state (global for simplicity) ----
static glm::vec2 gPos(0.0f, 0.0f);
static glm::vec2 gVel(0.65f, 0.42f);  // NDC units per second
static float gScale = 0.35f;          // initial scale
static float gLastTime = 0.0f;

static void fillProceduralBeachBallRGBA(int w, int h, std::vector<unsigned char> &out)
{
    out.resize(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            float u = (x + 0.5f) / static_cast<float>(w) * 2.0f - 1.0f;
            float v = (y + 0.5f) / static_cast<float>(h) * 2.0f - 1.0f;
            float r = std::sqrt(u * u + v * v);
            size_t i = (static_cast<size_t>(y) * w + x) * 4;
            if (r > 1.0f)
            {
                out[i] = out[i + 1] = out[i + 2] = out[i + 3] = 0;
                continue;
            }
            float a = std::atan2(v, u);
            unsigned char cr = 255, cg = 220, cb = 60, ca = 255;
            if (a < -1.57f)       { cr = 255; cg = 80;  cb = 80; }
            else if (a < 0.f)     { cr = 60;  cg = 120; cb = 255; }
            else if (a < 1.57f)   { cr = 255; cg = 255; cb = 255; }
            float edge = 1.0f - r;
            ca = static_cast<unsigned char>(255.f * std::clamp(edge * 1.2f, 0.f, 1.f));
            out[i] = cr;
            out[i + 1] = cg;
            out[i + 2] = cb;
            out[i + 3] = ca;
        }
    }
}

static unsigned int createTextureFromRGBA(int width, int height, const unsigned char* rgba)
{
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glGenerateMipmap(GL_TEXTURE_2D);
    return tex;
}

static unsigned int loadTexture2D(const std::string& path)
{
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    int w = 0, h = 0, n = 0;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &n, 0);
    if (!data)
    {
        glDeleteTextures(1, &tex);
        return 0;
    }

    GLenum format = GL_RGB;
    if (n == 1) format = GL_RED;
    else if (n == 3) format = GL_RGB;
    else if (n == 4) format = GL_RGBA;

    glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    stbi_image_free(data);
    return tex;
}

int main()
{
    namespace fs = std::filesystem;

    // glfw: initialize and configure
    // ------------------------------
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    // glfw window creation
    // --------------------
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "LearnOpenGL - DVD Bounce + Grow", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    // glad: load all OpenGL function pointers
    // ---------------------------------------
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    // Shaders are still in LearnOpenGL chapter folder.
    std::string vsPath = FileSystem::getPath("src/1.getting_started/5.1.transformations/5.1.transform.vs");
    std::string fsPath = FileSystem::getPath("src/1.getting_started/5.1.transformations/5.1.transform.fs");
    Shader ourShader(vsPath.c_str(), fsPath.c_str());

    // set up vertex data (and buffer(s)) and configure vertex attributes
    // ------------------------------------------------------------------
    float vertices[] = {
        // positions          // texture coords
         0.5f,  0.5f, 0.0f,   1.0f, 1.0f, // top right
         0.5f, -0.5f, 0.0f,   1.0f, 0.0f, // bottom right
        -0.5f, -0.5f, 0.0f,   0.0f, 0.0f, // bottom left
        -0.5f,  0.5f, 0.0f,   0.0f, 1.0f  // top left
    };
    unsigned int indices[] = {
        0, 1, 3,
        1, 2, 3
    };

    unsigned int VBO, VAO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // texture coord attribute
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Textures: load all images from buildgame_2D-main/resources/00angrybird (this folder),
    // switch image when the quad hits a CORNER.
    std::vector<unsigned int> textures;
    {
        fs::path repoRoot = fs::path(FileSystem::getPath("resources")).parent_path(); // .../LearnOpenGL-master
        fs::path workspaceRoot = repoRoot.parent_path(); // .../3d
        fs::path birdDir = workspaceRoot / "buildgame_2D-main" / "resources" / "00angrybird";

        if (fs::exists(birdDir) && fs::is_directory(birdDir))
        {
            for (const auto& entry : fs::directory_iterator(birdDir))
            {
                if (!entry.is_regular_file()) continue;
                auto ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")
                {
                    unsigned int t = loadTexture2D(entry.path().string());
                    if (t != 0) textures.push_back(t);
                }
            }
        }

        if (textures.empty())
        {
            std::cout << "No textures found in " << birdDir.string() << " — using procedural texture" << std::endl;
            const int W = 128, H = 128;
            std::vector<unsigned char> proc;
            fillProceduralBeachBallRGBA(W, H, proc);
            textures.push_back(createTextureFromRGBA(W, H, proc.data()));
        }
        else
        {
            std::cout << "Loaded " << textures.size() << " texture(s) from " << birdDir.string() << std::endl;
        }
    }
    size_t textureIndex = 0;


    // enable alpha blending (so png transparency works)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // tell OpenGL which texture unit each sampler belongs to
    ourShader.use();
    ourShader.setInt("texture1", 0);

    // init time
    gLastTime = (float)glfwGetTime();

    // render loop
    // -----------
    while (!glfwWindowShouldClose(window))
    {
        // input
        processInput(window);

        // time step
        float now = (float)glfwGetTime();
        float dt = now - gLastTime;
        gLastTime = now;

        // update position (classic DVD bounce)
        gPos += gVel * dt;

        // compute half extent in NDC:
        // local quad spans [-0.5,0.5] so half-size = 0.5 * scale
        float halfExtent = 0.5f * gScale;

        bool collidedX = false;
        bool collidedY = false;

        // bounce X
        if (gPos.x + halfExtent > 1.0f) {
            gPos.x = (1.0f - halfExtent);
            gVel.x *= -1.0f;
            collidedX = true;
        }
        if (gPos.x - halfExtent < -1.0f) {
            gPos.x = (-1.0f + halfExtent);
            gVel.x *= -1.0f;
            collidedX = true;
        }

        // bounce Y
        if (gPos.y + halfExtent > 1.0f) {
            gPos.y = (1.0f - halfExtent);
            gVel.y *= -1.0f;
            collidedY = true;
        }
        if (gPos.y - halfExtent < -1.0f) {
            gPos.y = (-1.0f + halfExtent);
            gVel.y *= -1.0f;
            collidedY = true;
        }

        bool collided = collidedX || collidedY;
        if (collided && textures.size() > 1)
        {
            textureIndex = (textureIndex + 1) % textures.size();
        }

        if (collided) {
            float newHalf = 0.5f * gScale;
            gPos.x = std::clamp(gPos.x, -1.0f + newHalf,  1.0f - newHalf);
            gPos.y = std::clamp(gPos.y, -1.0f + newHalf,  1.0f - newHalf);
        }


        // render
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // bind textures
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textures[textureIndex]);

        // build transform matrix
        float selfSpin = now * 4.0f; // spin on itself
        glm::mat4 transform = glm::mat4(1.0f);
        transform = glm::translate(transform, glm::vec3(gPos.x, gPos.y, 0.0f));
        transform = glm::rotate(transform, selfSpin, glm::vec3(0.0f, 0.0f, 1.0f));
        transform = glm::scale(transform, glm::vec3(gScale, gScale, 1.0f));

        // set uniform
        ourShader.use();
        unsigned int transformLoc = glGetUniformLocation(ourShader.ID, "transform");
        glUniformMatrix4fv(transformLoc, 1, GL_FALSE, glm::value_ptr(transform));

        // draw
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // cleanup
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    for (auto t : textures) glDeleteTextures(1, &t);

    glfwTerminate();
    return 0;
}

void processInput(GLFWwindow *window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    // optional controls
    // R: reset
    if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) {
        gPos = glm::vec2(0.0f, 0.0f);
        gVel = glm::vec2(0.65f, 0.42f);
        gScale = 0.35f;
    }
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}
