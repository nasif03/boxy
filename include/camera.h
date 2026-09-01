const float YAW         = -90.0f;
const float PITCH       =  0.0f;
const float SPEED       =  15.0f;
const float SENSITIVITY =  0.1f;

enum camera_movement {
    FORWARD,
    BACKWARD,
    LEFT,
    RIGHT,
    UP,
    DOWN
};

struct camera {
    glm::vec3 pos;
    glm::vec3 front;
    glm::vec3 up;
    glm::vec3 world_up;
    glm::vec3 right;
    float yaw, pitch;
    float movement_speed;
    float mouse_sensitivity;

    camera(glm::vec3 _pos = glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3 _up = glm::vec3(0.0f, 1.0f, 0.0f), float _yaw = YAW, float _pitch = PITCH) : front(glm::vec3(0.0f, 0.0f, -1.0f)), movement_speed(SPEED), mouse_sensitivity(SENSITIVITY) {
        pos      = _pos;
        world_up = _up;
        yaw      = _yaw;
        pitch    = _pitch;
        
        glm::vec3 f;
        f.x   = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        f.y   = sin(glm::radians(pitch));
        f.z   = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        front = glm::normalize(f);
        right = glm::normalize(glm::cross(front, world_up));
        up    = glm::normalize(glm::cross(right, front));
    }
};

void camera_update_vectors(camera &c) {
    glm::vec3 f;
    f.x = cos(glm::radians(c.yaw)) * cos(glm::radians(c.pitch));
    f.y = sin(glm::radians(c.pitch));
    f.z = sin(glm::radians(c.yaw)) * cos(glm::radians(c.pitch));
    c.front = glm::normalize(f);
    c.right = glm::normalize(glm::cross(c.front, c.world_up));
    c.up    = glm::normalize(glm::cross(c.right, c.front));
}

void camera_process_keyboard(camera &c, camera_movement dir, float delta_time) {
    float velocity = c.movement_speed * delta_time;
    if (dir == FORWARD)
        c.pos += velocity * c.front;
    if (dir == BACKWARD)
        c.pos -= velocity * c.front;
    if (dir == LEFT)
        c.pos -= velocity * c.right;
    if (dir == RIGHT)
        c.pos += velocity * c.right;
    if (dir == UP)
        c.pos += velocity * c.world_up;
    if (dir == DOWN)
        c.pos -= velocity * c.world_up;
}

void camera_process_mouse(camera &c, float xoffset, float yoffset, bool constrain_pitch = true) {
    xoffset *= c.mouse_sensitivity;
    yoffset *= c.mouse_sensitivity;

    c.yaw += xoffset;
    c.pitch += yoffset;

    if (constrain_pitch) {
        if (c.pitch > 89.0f)
            c.pitch = 89.0f;
        if (c.pitch < -89.0f)
            c.pitch = -89.0f;
    }
    camera_update_vectors(c);
}

glm::mat4 camera_view_mat(camera &c) {
    return glm::lookAt(c.pos, c.pos + c.front, c.up);
}