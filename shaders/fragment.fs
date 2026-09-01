#version 460 core
out vec4 frag_color;

in float height;

void main() {
    float g = min(1.0, height / 255 + 0.2);
    frag_color = vec4(0.3, g, 0.3, 1.0);
}