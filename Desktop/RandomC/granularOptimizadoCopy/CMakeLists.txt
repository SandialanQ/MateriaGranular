cmake_minimum_required(VERSION 4.2.1)
project(VentanaNegra)

# Usar C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Buscar las librerías
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)
find_package(GLEW REQUIRED) # <-- Buscamos GLEW
find_package(GLUT REQUIRED) # <-- Buscamos GLUT
# Crear el ejecutable
add_executable(app main.cpp)

# Enlazamos todo: glfw, glm, glew y opengl32
target_link_libraries(app PRIVATE glfw glm::glm GLEW::GLEW ${GLUT_LIBRARIES} opengl32)
