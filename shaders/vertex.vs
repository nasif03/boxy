#version 460 core
layout (location = 0) in uint a_data;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

const vec3 CUBE_OFFSETS[6 * 4] = vec3[24](
    vec3(1, 0, 1), vec3(1, 0, 0), vec3(1, 1, 0), vec3(1, 1, 1), // +X (Right)
    vec3(0, 0, 0), vec3(0, 0, 1), vec3(0, 1, 1), vec3(0, 1, 0), // -X (Left)
    vec3(0, 1, 1), vec3(1, 1, 1), vec3(1, 1, 0), vec3(0, 1, 0), // +Y (Top)
    vec3(0, 0, 0), vec3(1, 0, 0), vec3(1, 0, 1), vec3(0, 0, 1), // -Y (Bottom)
    vec3(0, 0, 1), vec3(1, 0, 1), vec3(1, 1, 1), vec3(0, 1, 1), // +Z (Front)
    vec3(1, 0, 0), vec3(0, 0, 0), vec3(0, 1, 0), vec3(1, 1, 0)  // -Z (Back)
);

out uint face_dir;

void main() {
	uint x   = (a_data) & 0x0F;
	uint y   = (a_data >> 4) & 0xFF;
	uint z   = (a_data >> 12) & 0x0F;
	uint dir = (a_data >> 16) & 0x07;
	uint idx = (a_data >> 19) & 0x03;

	vec3 block_pos = vec3(float(x), float(y), float(z));
	vec3 pos = block_pos + CUBE_OFFSETS[dir * 4 + idx];

	gl_Position = projection * view * model * vec4(pos, 1.0);
	face_dir = dir;
}