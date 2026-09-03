#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <vector>
#include <queue>
#include <unordered_map>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>

#include <assert.h>

#include "shader.h"
#include "camera.h"
#include "chunk.h"

#define FNL_IMPL
#include "FastNoiseLite.h"

// constants

const int SCR_WIDTH     =   900;
const int SCR_HEIGHT    =   900;

const int CHUNK_SIZE    =     16;
const int CHUNK_HEIGHT  =    256;
const int WORLD_SIZE    =     32;
const int RENDER_DIST   =     32;

const int dx[] = { 1, -1, 0, 0, 0, 0 };
const int dy[] = { 0, 0, 1, -1, 0, 0 };
const int dz[] = { 0, 0, 0, 0, 1, -1 };

// timing

float delta_time = 0.0f;
float last_frame = 0.0f;

// camera

float last_x = SCR_WIDTH / 2.0f, last_y = SCR_HEIGHT / 2.0f;
bool first_mouse = true;
camera player_cam(glm::vec3(0.0f, 150.0f, 0.0f));

// callbacks

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
void process_input(GLFWwindow *window);

// terrain gen

fnl_state terrain_noise;
void terrain_noise_init(int seed) {
    terrain_noise              = fnlCreateState();
    terrain_noise.seed         = seed;
    terrain_noise.noise_type   = FNL_NOISE_PERLIN;
    terrain_noise.frequency    = 0.005f;
    terrain_noise.octaves      = 4;
    terrain_noise.fractal_type = FNL_FRACTAL_FBM;
}

// chunks

std::vector<std::vector<chunk>> world(WORLD_SIZE,
std::vector<chunk>(WORLD_SIZE));

glm::ivec3 block_facing;
glm::vec3 block_facing_n;

safe_queue<chunk_task> chunk_tasks;
safe_queue<chunk_result> chunk_results;
std::atomic<bool> program_running(true);

#define CHUNK_IMPLEMENTATION
#include <chunk.h>

int main() {
    #pragma region initialize opengl
    
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    
    // create window and context
    
    GLFWwindow *window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "boids", NULL, NULL);
    if (window == NULL) {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);
    
    // glad : load all function pointers
    
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        glfwTerminate();
        return -1;
    }
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    
    #pragma endregion
    
    // init shader
    
    shader s("shaders/vertex.vs", "shaders/fragment.fs");
    shader_bind(s);
    glm::mat4 projection = glm::perspective(glm::radians(90.0f), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 1000.0f);
    shader_set_mat4(s, "projection", projection);
    
    // boxy

    terrain_noise_init(42069);
    
    int chunk_x, chunk_z;
    std::vector<glm::ivec2> start_chunk_positions;
    chunk_get_coord(player_cam.pos.x, player_cam.pos.z, chunk_x, chunk_z);
    for (int x = chunk_x - WORLD_SIZE / 2; x < chunk_x + WORLD_SIZE / 2; x++) {
        for (int z = chunk_z - WORLD_SIZE / 2; z < chunk_z + WORLD_SIZE / 2; z++) {
            int xmod, zmod;
            chunk_get_coord_mod(x, z, xmod, zmod);
            chunk &c = world[xmod][zmod];
            c.x = x, c.z = z;
            c.state = IN_QUEUE;
            start_chunk_positions.push_back({x, z});
            // chunk_tasks.push({x, z});
        }
    }

    std::sort(start_chunk_positions.begin(), start_chunk_positions.end(),
    [](const glm::ivec2 &a, const glm::ivec2 &b) {
        return (a.x * a.x + a.y * a.y) < (b.x * b.x + b.y * b.y);
    });
    for (auto pos : start_chunk_positions) {
        chunk_tasks.push({pos.x, pos.y});
    }

    // create worker threads
    
    const int num_workers = 4;
    std::vector<std::thread> workers;
    for (int i = 0; i < num_workers; i++) {
        workers.push_back(std::thread(chunk_worker_loop, i));
    }
    
    // render loop
    
    while (!glfwWindowShouldClose(window)) {
        // timing
        
        float current_frame = (float)glfwGetTime();
        delta_time = current_frame - last_frame;
        last_frame = current_frame;
        
        // input
        
        process_input(window);
        
        // events
        
        chunk_result result;
        int chunks_sent_to_gpu = 0;
        while (chunks_sent_to_gpu < 2 && chunk_results.pop(result)) {
            int xmod, zmod;
            chunk_get_coord_mod(result.x, result.z, xmod, zmod);
            
            chunk &c = world[xmod][zmod];
            c.x = result.x, c.z = result.z;
            c.mesh = std::move(result.mesh);
            c.voxels = std::move(result.voxels);
            c.vertex_count = c.mesh.size();
            chunk_send_to_gpu(c);
            chunks_sent_to_gpu++;
        }

        chunk_get_coord(player_cam.pos.x, player_cam.pos.z, chunk_x, chunk_z);
        for (int x = chunk_x - WORLD_SIZE / 2; x < chunk_x + WORLD_SIZE / 2; x++) {
            for (int z = chunk_z - WORLD_SIZE / 2; z < chunk_z + WORLD_SIZE / 2; z++) {
                int xmod, zmod;
                chunk_get_coord_mod(x, z, xmod, zmod);
                chunk &c = world[xmod][zmod];
                if (c.state == DIRTY || c.state == IN_QUEUE) continue;
                if (c.x != x || c.z != z) {
                    c.x = x, c.z = z;
                    chunk_delete(c);
                }
            }
        }

        for (int x = 0; x < WORLD_SIZE; x++) {
            for (int z = 0; z < WORLD_SIZE; z++) {
                chunk &c = world[x][z];
                if (c.state == DIRTY) {
                    c.state = IN_QUEUE;
                    chunk_tasks.push({c.x, c.z});
                }
            }
        }

        
        if (get_voxel_from_raycast(block_facing, block_facing_n)) {
            std::cout << block_facing.x << ' ' << block_facing.y << ' ' << block_facing.z << '\n';
        }

        // render

        glClearColor(0.8f, 0.8f, 0.85f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); 

        shader_bind(s);
        shader_set_mat4(s, "view", camera_view_mat(player_cam));

        for (int x = 0; x < WORLD_SIZE; x++) {
            for (int z = 0; z < WORLD_SIZE; z++) {
                if (world[x][z].state == READY) {
                    chunk_draw(world[x][z], s);
                }
            }
        }
        
        // buffer swap and poll IO events
        
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    program_running = false;
    for (int i = 0; i < num_workers; i++) {
        if (workers[i].joinable())
            workers[i].join();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    
    return 0;
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    float x = (float)xpos, y = (float)ypos;
    if (first_mouse) {
        last_x = x;
        last_y = y;
        first_mouse = false;
    }
    float xoffset = x - last_x;
    float yoffset = last_y - y;
    last_x = x;
    last_y = y;
    camera_process_mouse(player_cam, xoffset, yoffset);
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT)
        voxel_place(block_facing);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {

}

void process_input(GLFWwindow *window) {
    if(glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, 1);
    
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camera_process_keyboard(player_cam, FORWARD, delta_time);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camera_process_keyboard(player_cam, BACKWARD, delta_time);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera_process_keyboard(player_cam, LEFT, delta_time);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera_process_keyboard(player_cam, RIGHT, delta_time);
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
        camera_process_keyboard(player_cam, UP, delta_time);
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT))
        camera_process_keyboard(player_cam, DOWN, delta_time);
}
