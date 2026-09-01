#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <vector>
#include <queue>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <atomic>

#include <assert.h>

#include "shader.h"
#include "camera.h"

#define FNL_IMPL
#include "FastNoiseLite.h"

// constants

const int SCR_WIDTH     =   1920;
const int SCR_HEIGHT    =   1080;

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
camera player_cam(glm::vec3(0.0f, 128.0f, 0.0f));

// callbacks

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
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

enum block_type {
    AIR,
    GRASS,
    STONE,
    DIRT,
    IDK
};

enum chunk_state {
    READY,
    IN_QUEUE,
    DIRTY
};

struct vertex {
    uint32_t data;
    vertex() {}
    vertex(int x, int y, int z, int dir, int idx) {
        data = (x | (y << 4) | (z << 12) | (dir << 16) | (idx << 19));
    }
};

struct chunk {
    std::vector<uint8_t> voxels;
    std::vector<vertex> mesh;
    int x, z;
    unsigned int vao = 0;
    unsigned int vbo = 0;
    int vertex_count = 0;
    chunk_state state = DIRTY;
};

int get_voxel_index(int x, int y, int z) {
    return x * CHUNK_SIZE * CHUNK_HEIGHT + y * CHUNK_SIZE + z;
}

void chunk_init_voxels(std::vector<uint8_t> &voxels, int cx, int cz) {
    voxels.resize(CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE);
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            int noise_val = (int)(fnlGetNoise2D(&terrain_noise, x + cx * CHUNK_SIZE, z + cz * CHUNK_SIZE) * 64);
            int height = CHUNK_HEIGHT / 4 + noise_val;
            for (int y = 0; y < CHUNK_HEIGHT; y++) {
                if (y < height) voxels[get_voxel_index(x, y, z)] = STONE;
                else voxels[get_voxel_index(x, y, z)] = AIR;
            }
        }
    }
}

void chunk_quad_add(std::vector<vertex> &mesh, int x, int y, int z, int dir) {
    vertex v[4];
    v[0] = vertex(x, y, z, dir, 0);
    v[1] = vertex(x, y, z, dir, 1);
    v[2] = vertex(x, y, z, dir, 2);
    v[3] = vertex(x, y, z, dir, 3);

    mesh.push_back(v[0]);
    mesh.push_back(v[1]);
    mesh.push_back(v[2]);

    mesh.push_back(v[2]);
    mesh.push_back(v[3]);
    mesh.push_back(v[0]);
}

void chunk_create_mesh(std::vector<vertex> &mesh, std::vector<uint8_t> &voxels) {
    // create mesh from voxel values
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int y = 0; y < CHUNK_HEIGHT; y++) {
            for (int z = 0; z < CHUNK_SIZE; z++) {
                if (voxels[get_voxel_index(x, y, z)] == AIR) continue;

                for (int dir = 0; dir < 6; dir++) {
                    int x1 = x + dx[dir];
                    int y1 = y + dy[dir];
                    int z1 = z + dz[dir];

                    if (x1 < 0 || x1 >= CHUNK_SIZE || y1 < 0 || y1 >= CHUNK_HEIGHT || z1 < 0 || z1 >= CHUNK_SIZE) {
                        chunk_quad_add(mesh, x, y, z, dir);
                    } else if (voxels[get_voxel_index(x1, y1, z1)] == AIR) {
                        chunk_quad_add(mesh, x, y, z, dir);
                    }
                }
            }
        }
    }
}

void chunk_send_to_gpu(chunk &c) {
    glGenVertexArrays(1, &c.vao);
    glGenBuffers(1, &c.vbo);
    
    glBindVertexArray(c.vao);
    glBindBuffer(GL_ARRAY_BUFFER, c.vbo);
    glBufferData(GL_ARRAY_BUFFER, c.vertex_count * sizeof(vertex), &c.mesh[0], GL_STATIC_DRAW);
    
    glEnableVertexAttribArray(0);
    // glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vertex), (void *)0);
    glVertexAttribIPointer(0, 1, GL_INT, sizeof(vertex), (void *)0);
    c.state = READY;
}

void chunk_delete(chunk &c) {
    c.mesh.clear();
    if (c.vao) {
        glDeleteVertexArrays(1, &c.vao);
        c.vao = 0;
    }
    if (c.vbo) {
        glDeleteBuffers(1, &c.vbo);
        c.vbo = 0;
    }
    c.vertex_count = 0;
    c.state = DIRTY;
}

void chunk_draw(chunk &c, shader &s) {
    shader_bind(s);
    glm::mat4 model = glm::mat4(1.0);
    model = glm::translate(model, glm::vec3((float)(c.x * CHUNK_SIZE), 0, (float)(c.z * CHUNK_SIZE)));
    shader_set_mat4(s, "model", model);
    glBindVertexArray(c.vao);
    glDrawArrays(GL_TRIANGLES, 0, c.vertex_count);
}

void get_chunk_coord(int &x, int &z) {
    x = player_cam.pos.x / CHUNK_SIZE;
    z = player_cam.pos.z / CHUNK_SIZE;
    if (player_cam.pos.x < 0) x--;
    if (player_cam.pos.z < 0) z--;
}

void get_mod_chunk_coord(int x, int z, int &xmod, int &zmod) {
    xmod = (x % WORLD_SIZE + WORLD_SIZE) % WORLD_SIZE;
    zmod = (z % WORLD_SIZE + WORLD_SIZE) % WORLD_SIZE;
    assert(xmod >= 0 && xmod < WORLD_SIZE);
    assert(zmod >= 0 && zmod < WORLD_SIZE);
}

struct chunk_task {
    int x, z;
};

struct chunk_result {
    std::vector<uint8_t> voxels;
    std::vector<vertex> mesh;
    int x, z;
};

template <typename T> struct safe_queue {
    std::queue<T> q;
    std::mutex mtx;
    void push(const T &val) {
        std::lock_guard<std::mutex> lock(mtx);
        q.push(val);
    }
    bool pop(T &val) {
        std::lock_guard<std::mutex> lock(mtx);
        if (q.empty()) return false;
        val = q.front();
        q.pop();
        return true;
    }
};

safe_queue<chunk_task> chunk_tasks;
safe_queue<chunk_result> chunk_results;
std::atomic<bool> program_running(true);

void chunk_worker_loop(int thread_id) {
    while (program_running) {
        chunk_task task;

        if (!chunk_tasks.pop(task)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        chunk_result result;
        result.x = task.x, result.z = task.z;
        chunk_init_voxels(result.voxels, task.x, task.z);
        chunk_create_mesh(result.mesh, result.voxels);

        chunk_results.push(std::move(result));
    }
}

std::vector<std::vector<chunk>> world(WORLD_SIZE,
std::vector<chunk>(WORLD_SIZE));

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
    glfwSetScrollCallback(window, scroll_callback);
    
    // glad : load all function pointers
    
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        glfwTerminate();
        return -1;
    }
    glEnable(GL_DEPTH_TEST);
    
    #pragma endregion
    
    // init shader
    
    shader s("shaders/vertex.vs", "shaders/fragment.fs");
    shader_bind(s);
    glm::mat4 projection = glm::perspective(glm::radians(90.0f), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 1000.0f);
    shader_set_mat4(s, "projection", projection);
    
    // boxy

    terrain_noise_init(42069);
    
    int chunk_x, chunk_z;
    get_chunk_coord(chunk_x, chunk_z);
    for (int x = chunk_x - WORLD_SIZE / 2; x < chunk_x + WORLD_SIZE / 2; x++) {
        for (int z = chunk_z - WORLD_SIZE / 2; z < chunk_z + WORLD_SIZE / 2; z++) {
            int xmod, zmod;
            get_mod_chunk_coord(x, z, xmod, zmod);
            chunk &c = world[xmod][zmod];
            c.x = x, c.z = z;
            c.state = IN_QUEUE;
            chunk_tasks.push({x, z});
        }
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
        while (chunks_sent_to_gpu < 1 && chunk_results.pop(result)) {
            int xmod, zmod;
            get_mod_chunk_coord(result.x, result.z, xmod, zmod);
            
            chunk &c = world[xmod][zmod];
            c.x = result.x, c.z = result.z;
            c.mesh = std::move(result.mesh);
            c.voxels = std::move(result.voxels);
            c.vertex_count = c.mesh.size();
            chunk_send_to_gpu(c);
            chunks_sent_to_gpu++;
        }
    

        get_chunk_coord(chunk_x, chunk_z);
        for (int x = chunk_x - WORLD_SIZE / 2; x < chunk_x + WORLD_SIZE / 2; x++) {
            for (int z = chunk_z - WORLD_SIZE / 2; z < chunk_z + WORLD_SIZE / 2; z++) {
                int xmod, zmod;
                get_mod_chunk_coord(x, z, xmod, zmod);
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
                    {
                        std::lock_guard<std::mutex> lock(chunk_tasks.mtx);
                        c.state = IN_QUEUE;
                        chunk_tasks.q.push({c.x, c.z});
                    }
                }
            }
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
