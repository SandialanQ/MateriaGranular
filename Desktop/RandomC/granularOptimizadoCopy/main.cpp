#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <vector>
#include <cmath>
#include <iostream>
#include <random>

// prametros
const int WINDOW_WIDTH = 1000; // escala
const int WINDOW_HEIGHT = 600;
const float SIM_WIDTH = 250.0f;
const float SIM_HEIGHT = 150.0f;

const float G_ACC = 98.1f;
const float VIB_AMPLITUDE = 500.0f;
const float VIB_FREQUENCY = 3.0f;
const int SUB_STEPS = 7;
const float DT = 0.05f / SUB_STEPS;
const float E_MODULUS = 5000.0f;//modulo de young

// clase de la particula
struct Particle {
    glm::vec2 pos;
    glm::vec2 vel;
    glm::vec2 force; // Usamos fuerza acumulada en lugar de modificar aceleración directamente
    float radius;
    float mass;
    float E, poisson, xi;
    glm::vec3 color;
};

// celdas
const float CELL_SIZE = 12.0f; // Ligeramente mayor al diámetro máximo de la particula
const int GRID_W = (int)(SIM_WIDTH / CELL_SIZE) + 1;
const int GRID_H = (int)(SIM_HEIGHT / CELL_SIZE) + 1;
std::vector<int> grid[GRID_W][GRID_H];

// Shaders por default
const char* vertexShaderSource = R"glsl(
    #version 330 core
    layout (location = 0) in vec2 aPos;
    layout (location = 1) in float aRadius;
    layout (location = 2) in vec3 aColor;

    uniform mat4 projection;
    
    out vec3 ParticleColor;
    out float Radius;

    void main() {
        gl_Position = projection * vec4(aPos, 0.0, 1.0);
        // Escalar el tamaño del punto según la resolución
        gl_PointSize = aRadius * 2.0 * (1000.0 / 250.0); 
        ParticleColor = aColor;
        Radius = aRadius;
    }
)glsl";

const char* fragmentShaderSource = R"glsl(
    #version 330 core
    in vec3 ParticleColor;
    out vec4 FragColor;

    void main() {
        // Coordenadas relativas dentro del GL_POINT (-1 a 1)
        vec2 circCoord = 2.0 * gl_PointCoord - 1.0;
        if (dot(circCoord, circCoord) > 1.0) {
            discard; // Descarta los píxeles fuera del círculo
        }
        FragColor = vec4(ParticleColor, 1.0);
    }
)glsl";

//  DEM (Discrete Element Method) para interacción entre partículas
void interact_dem(Particle& p1, Particle& p2) {
    glm::vec2 d = p1.pos - p2.pos;
    float distSq = glm::dot(d, d);
    float minDist = p1.radius + p2.radius;

    if (distSq == 0.0f || distSq >= minDist * minDist) return;

    float dist = std::sqrt(distSq);
    float overlap = minDist - dist;
    float R_star = (p1.radius * p2.radius) / (p1.radius + p2.radius);
    float E_star = 1.0f / (((1.0f - p1.poisson * p1.poisson) / p1.E) + ((1.0f - p2.poisson * p2.poisson) / p2.E));
    float m_star = (p1.mass * p2.mass) / (p1.mass + p2.mass);
    float xi_star = (p1.E * p1.xi + p2.E * p2.xi) / (p1.E + p2.E);

    glm::vec2 n = d / dist;

    // Fuerza Normal
    float F_n_mag = 0.75f * std::sqrt(R_star) * E_star * std::pow(overlap, 1.5f);

    // Fuerza de Amortiguación (Damping)
    glm::vec2 dv = p1.vel - p2.vel;
    float vn = glm::dot(dv, n);
    float damping_term = 2.0f * xi_star * std::sqrt(2.0f * E_star * m_star) * std::pow(R_star * overlap, 0.25f);
    float F_damp_mag = damping_term * (-vn);
    float F_total_mag = F_n_mag + F_damp_mag;
    if (F_total_mag < 0.0f) F_total_mag = 0.0f;

    glm::vec2 F = F_total_mag * n;
    p1.force += F;
    p2.force -= F;
}

int main() {
    // Inicializar GLFW y GLEW
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Granular Matter Simulation", NULL, NULL);
    glfwMakeContextCurrent(window);
    glewInit();

    // Habilitar modificación de tamaño de puntos y blending en OpenGL
    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Compilar Shaders
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    // Proyección Ortográfica usando GLM
    glm::mat4 projection = glm::ortho(0.0f, SIM_WIDTH, SIM_HEIGHT, 0.0f);
    glUseProgram(shaderProgram);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

    // Generar Partículas
    std::vector<Particle> particles;
    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> distRad(-1.0f, 1.0f);
    std::uniform_real_distribution<float> distPos(20.0f, SIM_WIDTH - 20.0f);

    // Partícula gigante (Índice 0)
    particles.push_back({glm::vec2(SIM_WIDTH/2, SIM_HEIGHT - 30.0f), glm::vec2(0.0f), glm::vec2(0.0f), 
                         25.0f, 3.1415f * 25.0f * 25.0f * 1.0f, E_MODULUS, 0.35f, 0.5f, glm::vec3(1.0f, 0.04f, 0.2f)});
    particles.push_back({glm::vec2(SIM_WIDTH/2, 35), glm::vec2(0.0f), glm::vec2(0.0f), 
                         25.0f, 3.1415f * 25.0f * 25.0f * 1.0f, E_MODULUS, 0.35f, 0.5f, glm::vec3(1.0f, 0.04f, 0.2f)});

    // particulas pequenas
    int NUM_SAND = 400; // mas o menos
    for (int i = 0; i < NUM_SAND; i++) {
        float r = 5.0f + distRad(rng);
        particles.push_back({glm::vec2(distPos(rng), distPos(rng) * 0.5f), glm::vec2(0.0f), glm::vec2(0.0f), 
                             r, 3.1415f * r * r * 1.0f, E_MODULUS, 0.35f, 0.5f, glm::vec3(0.98f, 0.78f, 0.2f)});
    }

    // Configuración VBO/VAO
    // Formato de VBO interleaved: x, y, radius, r, g, b
    GLuint VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, particles.size() * sizeof(Particle), particles.data(), GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)offsetof(Particle, pos));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)offsetof(Particle, radius));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)offsetof(Particle, color));
    glEnableVertexAttribArray(2);

    float sim_time = 0.0f;
    float frequency = 10.0f;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        bool mouseState = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        //if (frequency > 5.0f) frequency = 0.0f;

        // Bucle Físico (Sub-stepping)
        for (int step = 0; step < SUB_STEPS; step++) {
            
            // 1. Limpiar fuerzas y aplicar gravedad/vibración
            float vibration_acc = VIB_AMPLITUDE * std::sin(2.0f * 3.1415f * frequency * sim_time);
            glm::vec2 gravity = mouseState ? glm::vec2(vibration_acc, G_ACC + vibration_acc) : glm::vec2(0.0f, G_ACC);
            //glm::vec2 gravity = mouseState ? glm::vec2(vibration_acc + G_ACC,  0) : glm::vec2(G_ACC, 0.0f);
            for (auto& p : particles) {
                p.force = gravity * p.mass;
            }

            // 2. Limpiar e insertar en el Spatial Grid (Omitimos la partícula gigante [0])
            for (int x = 0; x < GRID_W; x++)
                for (int y = 0; y < GRID_H; y++)
                    grid[x][y].clear();

            for (size_t i = 1; i < particles.size(); i++) {
                int cx = glm::clamp((int)(particles[i].pos.x / CELL_SIZE), 0, GRID_W - 1);
                int cy = glm::clamp((int)(particles[i].pos.y / CELL_SIZE), 0, GRID_H - 1);
                grid[cx][cy].push_back(i);
            }

            // 3. Resolución de Colisiones DEM
            // Colisiones Arena vs Arena usando el Grid
            for (int x = 0; x < GRID_W; x++) {
                for (int y = 0; y < GRID_H; y++) {
                    for (size_t i = 0; i < grid[x][y].size(); i++) {
                        int p1_idx = grid[x][y][i];
                        
                        // Chequear misma celda
                        for (size_t j = i + 1; j < grid[x][y].size(); j++) {
                            interact_dem(particles[p1_idx], particles[grid[x][y][j]]);
                        }
                        
                        // Chequear celdas vecinas (Mitad superior/derecha para evitar doble cálculo)
                        int neighbors[4][2] = {{1, 0}, {0, 1}, {1, 1}, {-1, 1}};
                        for (auto& offset : neighbors) {
                            int nx = x + offset[0];
                            int ny = y + offset[1];
                            
                            if (nx >= 0 && nx < GRID_W && ny >= 0 && ny < GRID_H) {
                                for (int p2_idx : grid[nx][ny]) {
                                    interact_dem(particles[p1_idx], particles[p2_idx]);
                                }
                            }
                        }
                    }
                }
            }

            // Colisiones Arena vs Partícula Gigante
            for (size_t i = 1; i < particles.size(); i++) {
                interact_dem(particles[0], particles[i]);
            }

            // 4. Integración de Euler semi-implícita y bordes
            for (auto& p : particles) {
                glm::vec2 acc = p.force / p.mass;
                p.vel += acc * DT;
                p.pos += p.vel * DT;

                // Confinamiento
                if (p.pos.y + p.radius > SIM_HEIGHT) {
                    p.pos.y = SIM_HEIGHT - p.radius;
                    p.vel.y *= -0.3f;
                } else if (p.pos.y - p.radius < 0.0f) {
                    p.pos.y = p.radius;
                    p.vel.y *= -0.3f;
                }

                if (p.pos.x + p.radius > SIM_WIDTH) {
                    p.pos.x = SIM_WIDTH - p.radius;
                    p.vel.x *= -0.3f;
                } else if (p.pos.x - p.radius < 0.0f) {
                    p.pos.x = p.radius;
                    p.vel.x *= -0.3f;
                }
            }
            sim_time += DT;
        }
        
        if (mouseState) frequency += 0.1f;

        // Renderizado
        glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Actualizar VBO
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        // Usamos glBufferSubData que es más rápido para actualizar buffers dinámicos
        glBufferSubData(GL_ARRAY_BUFFER, 0, particles.size() * sizeof(Particle), particles.data());

        glUseProgram(shaderProgram);
        glBindVertexArray(VAO);
        
        // Dibujar todo en un solo Draw Call
        glDrawArrays(GL_POINTS, 0, particles.size());

        glfwSwapBuffers(window);
    }

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);
    glfwTerminate();
    return 0;
}