#ifndef CHUNK_H
#define CHUNK_H

enum voxel_type : uint8_t {
    AIR,
    GRASS,
    STONE,
    DIRT,
    IDK
};

enum chunk_state : uint8_t {
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
    std::vector<voxel_type> voxels;
    std::vector<vertex> mesh;
    int x, z;
    unsigned int vao = 0;
    unsigned int vbo = 0;
    int vertex_count = 0;
    chunk_state state = DIRTY;
};

struct chunk_task {
    int x, z;
};

struct chunk_result {
    std::vector<voxel_type> voxels;
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

void chunk_get_coord(int x, int z, int &cx, int &cz);
void chunk_get_coord_mod(int x, int z, int &xmod, int &zmod);
void chunk_init_voxels(std::vector<voxel_type> &voxels, int cx, int cz);
void chunk_quad_add(std::vector<vertex> &mesh, int x, int y, int z, int dir);
void chunk_create_mesh(std::vector<vertex> &mesh, std::vector<voxel_type> &voxels, int cx, int cz);
void chunk_send_to_gpu(chunk &c);
void chunk_delete(chunk &c);
void chunk_draw(chunk &c, shader &s);
void chunk_worker_loop(int thread_id);

int voxel_get_index(int x, int y, int z);
int voxel_get_index(glm::ivec3 pos);
void voxel_place(int x, int y, int z);
void voxel_place(glm::ivec3 pos);
voxel_type voxel_get_type(int x, int y, int z);
voxel_type voxel_get_type(glm::ivec3 pos);
bool get_voxel_from_raycast(glm::ivec3 &res, glm::vec3 &normal, float max_dist);

#endif /* CHUNK_H */

#ifdef CHUNK_IMPLEMENTATION

void voxel_place(glm::ivec3 pos) {
    int cx, cz, cxmod, czmod;
    chunk_get_coord(pos.x, pos.z, cx, cz);
    chunk_get_coord_mod(cx, cz, cxmod, czmod);

    pos.x = (pos.x % CHUNK_SIZE + CHUNK_SIZE) % CHUNK_SIZE;
    pos.z = (pos.z % CHUNK_SIZE + CHUNK_SIZE) % CHUNK_SIZE;
    chunk &c = world[cxmod][czmod];
    c.voxels[voxel_get_index(pos)] = STONE;
    c.state = DIRTY;
}

voxel_type voxel_get_type(int x, int y, int z) {
    if (y < 0 || y >= CHUNK_HEIGHT) return AIR;

    int cx, cz, cxmod, czmod;
    chunk_get_coord(x, z, cx, cz);
    chunk_get_coord_mod(cx, cz, cxmod, czmod);

    x = (x % CHUNK_SIZE + CHUNK_SIZE) % CHUNK_SIZE;
    z = (z % CHUNK_SIZE + CHUNK_SIZE) % CHUNK_SIZE;
    if (world[cxmod][czmod].voxels.empty()) return AIR;
    return world[cxmod][czmod].voxels[voxel_get_index(x, y, z)];
}

voxel_type voxel_get_type(glm::ivec3 pos) {
    return voxel_get_type(pos.x, pos.y, pos.z);
}

bool get_voxel_from_raycast(glm::ivec3 &res, glm::vec3 &normal, float max_dist = 10.0f) {
    glm::vec3 dir = player_cam.front;
    glm::vec3 start = player_cam.pos;

    glm::ivec3 step = glm::sign(dir);
    glm::ivec3 pos = glm::ivec3(glm::floor(start));
    
    const double eps = 1e-9;
    if (abs(dir.x) < eps) dir.x = eps;
    if (abs(dir.y) < eps) dir.y = eps;
    if (abs(dir.z) < eps) dir.z = eps;
    
    glm::vec3 delta = glm::abs(glm::vec3(1.0f / dir.x, 1.0f / dir.y, 1.0f / dir.z));
    glm::vec3 fract = start - glm::floor(start);

    glm::vec3 tmax = glm::vec3(
        (dir.x > 0.0f ? 1.0f - fract.x : fract.x) * delta.x,
        (dir.y > 0.0f ? 1.0f - fract.y : fract.y) * delta.y,
        (dir.z > 0.0f ? 1.0f - fract.z : fract.z) * delta.z
    );

    float dist = 0.0f;
    glm::vec3 last_move = glm::vec3(0.0f);

    while (dist < max_dist) {
        if (voxel_get_type(pos) != AIR) {
            res = pos;
            normal = -glm::normalize(last_move);
            return true;
        }

        if (tmax.x < tmax.y && tmax.x < tmax.z) {
            pos.x += step.x;
            dist = tmax.x;
            tmax.x += delta.x;
            last_move = glm::vec3(step.x, 0, 0);
        } else if (tmax.y < tmax.z) {
            pos.y += step.y;
            dist = tmax.y;
            tmax.y += delta.y;
            last_move = glm::vec3(0, step.y, 0);
        } else {
            pos.z += step.z;
            dist = tmax.z;
            tmax.z += delta.z;
            last_move = glm::vec3(0, step.z, 0);
        }
    }
    return false;
}

int voxel_get_index(int x, int y, int z) {
    return x * CHUNK_SIZE * CHUNK_HEIGHT + y * CHUNK_SIZE + z;
}

int voxel_get_index(glm::ivec3 pos) {
    return voxel_get_index(pos.x, pos.y, pos.z);
}

void chunk_init_voxels(std::vector<voxel_type> &voxels, int cx, int cz) {
    voxels.resize(CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE);
    
    const int min_height = 64;
    const int max_terrain_height = 128;
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            int terrain_height = (int)((fnlGetNoise2D(&terrain_noise, x + cx * CHUNK_SIZE, z + cz * CHUNK_SIZE) + 1.0f) / 2.0f * max_terrain_height);
            int height = min_height + terrain_height;
            for (int y = 0; y < CHUNK_HEIGHT; y++) {
                if (y < height) voxels[voxel_get_index(x, y, z)] = STONE;
                else voxels[voxel_get_index(x, y, z)] = AIR;
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

void chunk_create_mesh(std::vector<vertex> &mesh, std::vector<voxel_type> &voxels, int cx, int cz) {
    // create mesh from voxel values
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int y = 0; y < CHUNK_HEIGHT; y++) {
            for (int z = 0; z < CHUNK_SIZE; z++) {
                if (voxels[voxel_get_index(x, y, z)] == AIR) continue;

                for (int dir = 0; dir < 6; dir++) {
                    int x1 = x + dx[dir];
                    int y1 = y + dy[dir];
                    int z1 = z + dz[dir];

                    if (x1 < 0 || x1 >= CHUNK_SIZE || y1 < 0 || y1 >= CHUNK_HEIGHT || z1 < 0 || z1 >= CHUNK_SIZE) {
                        chunk_quad_add(mesh, x, y, z, dir);
                    } else if (voxels[voxel_get_index(x1, y1, z1)] == AIR) {
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
    glVertexAttribIPointer(0, 1, GL_UNSIGNED_INT, sizeof(vertex), (void *)0);
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

void chunk_get_coord(int x, int z, int &cx, int &cz) {
    cx = x / CHUNK_SIZE;
    cz = z / CHUNK_SIZE;
    if (x < 0) cx--;
    if (z < 0) cz--;
}

void chunk_get_coord_mod(int x, int z, int &xmod, int &zmod) {
    xmod = (x % WORLD_SIZE + WORLD_SIZE) % WORLD_SIZE;
    zmod = (z % WORLD_SIZE + WORLD_SIZE) % WORLD_SIZE;
}

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
        chunk_create_mesh(result.mesh, result.voxels, result.x, result.z);

        chunk_results.push(std::move(result));
    }
}

#endif /* CHUNK_IMPLEMENTATION */