struct shader {
    unsigned int id;

    shader(const char *vertex_path, const char *fragment_path) {
        FILE *vertex_file = fopen(vertex_path, "rb");
        assert(vertex_file != NULL);
        FILE *fragment_file = fopen(fragment_path, "rb");
        assert(fragment_file != NULL);

        char *vertex_code, *fragment_code;
        size_t file_size, bytes_read;

        fseek(vertex_file, 0, SEEK_END);
        file_size = ftell(vertex_file);
        rewind(vertex_file);
        vertex_code = (char *)malloc((file_size + 1) * sizeof(char));
        assert(vertex_code != NULL);
        
        bytes_read = fread(vertex_code, 1, file_size, vertex_file);
        vertex_code[bytes_read] = '\0';

        fseek(fragment_file, 0, SEEK_END);
        file_size = ftell(fragment_file);
        rewind(fragment_file);
        fragment_code = (char *)malloc((file_size + 1) * sizeof(char));
        assert(fragment_code != NULL);
        
        bytes_read = fread(fragment_code, 1, file_size, fragment_file);
        fragment_code[bytes_read] = '\0';
        
        unsigned int vertex, fragment;
        int success;
        char info_log[512];

        vertex = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertex, 1, (const char**)&vertex_code, NULL);
        glCompileShader(vertex);
        glGetShaderiv(vertex, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(vertex, 512, NULL, info_log);
            std::cout << "error::vertex::" << info_log << std::endl;
        }

        fragment = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragment, 1, (const char**)&fragment_code, NULL);
        glCompileShader(fragment);
        glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(fragment, 512, NULL, info_log);
            std::cout << "error::fragment::" << info_log << std::endl;
        }

        id = glCreateProgram();
        glAttachShader(id, vertex);
        glAttachShader(id, fragment);
        glLinkProgram(id);
        glGetProgramiv(id, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(id, 512, NULL, info_log);
            std::cout << "error::shader::" << info_log << std::endl;
        }

        glDeleteShader(vertex);
        glDeleteShader(fragment);

        fclose(vertex_file);
        fclose(fragment_file);
    }
};

void shader_bind(shader &s) {
    glUseProgram(s.id);
}

void shader_set_vec3(shader &s, const char *name, const glm::vec3 &value) { 
    glUniform3fv(glGetUniformLocation(s.id, name), 1, glm::value_ptr(value)); 
}

void shader_set_mat4(shader &s, const char *name, const glm::mat4 &value) { 
    glUniformMatrix4fv(glGetUniformLocation(s.id, name), 1, GL_FALSE, glm::value_ptr(value));
}