#version 460 core
out vec4 frag_color;
in flat uint face_dir;

const float DIR_LIGHT[6] = {
    0.60, // right
    0.60, // left
    1.00, // top
    0.30, // bottom
    0.80, // front
    0.80  // back
};

void main() {
    float light = DIR_LIGHT[face_dir];
    vec3 base_color = vec3(0.3, 0.7, 0.3);
    frag_color = vec4(light * base_color, 1.0);
}