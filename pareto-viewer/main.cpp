// STL
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
//
// glbinding handles the OpenGL
// inclusion and extension loading.
#include <glbinding/gl/gl.h>
#include <glbinding/glbinding.h>
//
// Use GLFW as platform-independent library to create a window.
// Because of glbinding, we do not want GLFW to include OpenGL.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
//
// Include GLM for typical math routines in 2, 3, and 4 dimensions.
#include <glm/glm.hpp>
//
#include <glm/ext.hpp>

// STL is standard. So we use its namespace everywhere.
using namespace std;
// glbinding puts OpenGL functions in namespace "gl".
// To write compatible code, we open it.
using namespace gl;

// This application shall be thought of as singleton.
// We use a namespace as an implementation alternative
// to the standard object-oriented design of a class.
// This allows the use of a global application state
// which is much easier to use with GLFW and lambdas.
namespace application {

// Default Window Parameters
int screen_width = 500;
int screen_height = 500;
const char* window_title = "VIPO: Pareto Frontier Viewer";

// Vertex and fragment shader source code.
// We do not need newline characters at every line ending
// due to the semicolons in the syntax of GLSL.
// Because the shader is statically coded,
// we use tab characters for readability.
const char* vertex_shader_text =
    "#version 330 core\n"
    "uniform mat4 MVP;"
    "in vec3 vPos;"
    "void main(){"
    "  gl_Position = MVP * vec4(vPos, 1.0);"
    "}";
const char* fragment_shader_text =
    "#version 330 core\n"
    "void main(){"
    "  gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);"
    "}";

// Initialize the application.
// Can be called manually.
// Otherwise called by application::run.
void init();

// Run the application.
// If not initialized, automatically calls application::init.
void run();

// Destroy the application.
// Automatically called when exiting the program.
void free();

}  // namespace application

glm::vec3 up{0, 0, 1};
glm::vec3 origin{0, 0, 0};
float fov = 45.0f;
float radius = 5.0f;
float altitude = 0.0f;
float azimuth = 0.0f;
vector<glm::vec3> vertices{};
vector<pair<uint32_t, uint32_t>> edges{};
array<glm::vec3, 8> aabb_vertices{};
array<pair<uint32_t, uint32_t>, 12> aabb_edges{
    pair{0, 2},  // x
    {0, 4},      // y
    {0, 1},      // z
    {1, 3},     {3, 2}, {5, 7}, {7, 6}, {6, 4}, {4, 5}, {1, 5}, {3, 7}, {2, 6}};

glm::mat4 model{1.0f};

int main(int argc, char** argv) {
  if (argc != 2) {
    cout << "usage:\n" << argv[0] << " <pareto frontier file>\n";
    return -1;
  }

  fstream file{argv[1], ios::in};
  if (!file.is_open()) {
    cerr << "Failed to open file '" << argv[1] << "' for reading.\n";
    return -1;
  }

  // Parse Pareto frontier of given file.
  string line;
  while (getline(file, line)) {
    if (line.empty()) continue;
    stringstream stream{line};
    string command;
    stream >> command;
    if (command == "v") {
      glm::vec3 v;
      stream >> v.x;
      stream >> v.y;
      stream >> v.z;
      vertices.push_back(v);
    } else if (command == "l") {
      pair<uint32_t, uint32_t> e;
      stream >> e.first;
      stream >> e.second;
      edges.push_back(e);
    } else {
      cerr << "Failed to parse given file. Command '" << command
           << "' is unknown.\n";
      return -1;
    }
  }

  // Compute AABB of Pareto frontier and
  // initialize default origin and radius.
  glm::vec3 aabb_min{vertices[0]};
  glm::vec3 aabb_max{vertices[0]};
  for (size_t i = 2; i < vertices.size(); i += 2) {
    aabb_min = min(aabb_min, vertices[i]);
    aabb_max = max(aabb_max, vertices[i]);
  }
  aabb_vertices[0] = aabb_min;
  aabb_vertices[1] = {aabb_min.x, aabb_min.y, aabb_max.z};
  aabb_vertices[2] = {aabb_max.x, aabb_min.y, aabb_min.z};
  aabb_vertices[3] = {aabb_max.x, aabb_min.y, aabb_max.z};
  aabb_vertices[4] = {aabb_min.x, aabb_max.y, aabb_min.z};
  aabb_vertices[5] = {aabb_min.x, aabb_max.y, aabb_max.z};
  aabb_vertices[6] = {aabb_max.x, aabb_max.y, aabb_min.z};
  aabb_vertices[7] = aabb_max;
  // origin = 0.5f * (aabb_max + aabb_min);
  // radius = 0.5f * length(aabb_max - aabb_min) *
  //          (1.0f / tan(0.5f * fov * M_PI / 180.0f));
  model = glm::scale(model, 1.0f / (0.5f * (aabb_max - aabb_min)));
  model = glm::translate(model, -0.5f * (aabb_max + aabb_min));

  // Initialize the application.
  // Is automatically called by application::run()
  // but can be called manually.
  // application::init();

  // Run the application loop and show the triangle.
  application::run();

  // Destroy the application.
  // Is automatically called at the end of program execution
  // but can be called manually.
  // application::free();
}

// Implementation of the application functions.
namespace application {

// To model private members of the singleton application,
// we use an embedded namespace "detail".
// With this, we even allow calls to private members
// but make clear that this should only be done
// when the user knows what that means.
namespace detail {

// Window and OpenGL Context
GLFWwindow* window = nullptr;
bool is_initialized = false;
// Vertex Data Handles
GLuint vertex_array;
GLuint vertex_buffer;
GLuint element_buffer;
// AABB Handles
GLuint aabb_vertex_array;
GLuint aabb_vertex_buffer;
GLuint aabb_element_buffer;
// Shader Handles
GLuint program;
GLint mvp_location, vpos_location, vcol_location;
// Transformation Matrices
glm::mat4 view, projection;
// UI
glm::vec2 old_mouse_pos{};
glm::vec2 mouse_pos{};

// RAII Destructor Simulator
// To make sure that the application::free function
// can be viewed as a destructor and adheres to RAII principle,
// we use a global variable of a simple type without a state
// and a destructor calling the application::free function.
struct raii_destructor_t {
  ~raii_destructor_t() { free(); }
} raii_destructor{};

// Helper Function Declarations
// Create window with OpenGL context.
void init_window();
// Compile and link the shader program.
void init_shader();
// Set up vertex buffer, vertex array, and vertex attributes.
void init_vertex_data();
// Function called when window is resized.
void resize();
// Function called to update variables in every application loop.
void update();
// Function called to render to screen in every application loop.
void render();

}  // namespace detail

void init() {
  // "Allow" function to access private members of application.
  using namespace detail;
  // Do not initialize if it has already been done.
  if (is_initialized) return;

  init_window();
  // The shader has to be initialized before
  // the initialization of the vertex data
  // due to identifier location variables
  // that have to be set after creating the shader program.
  init_shader();
  init_vertex_data();

  // To initialize the viewport and matrices,
  // window has to be resized at least once.
  resize();

  // Update private state.
  is_initialized = true;
}

void free() {
  // "Allow" function to access private members of application.
  using namespace detail;
  // An uninitialized application cannot be destroyed.
  if (!is_initialized) return;

  // Delete vertex data.
  glDeleteBuffers(1, &element_buffer);
  glDeleteBuffers(1, &vertex_buffer);
  glDeleteVertexArrays(1, &vertex_array);
  // Delete shader program.
  glDeleteProgram(program);

  if (window) glfwDestroyWindow(window);
  glfwTerminate();

  // Update private state.
  is_initialized = false;
}

void run() {
  // "Allow" function to access private members of application.
  using namespace detail;
  // Make sure application::init has been called.
  if (!is_initialized) init();

  // Start application loop.
  while (!glfwWindowShouldClose(window)) {
    // Handle user and OS events.
    glfwPollEvents();

    update();
    render();

    // Swap buffers to display the
    // new content of the frame buffer.
    glfwSwapBuffers(window);
  }
}

// Private Member Function Implementations
namespace detail {

void init_window() {
  // Create GLFW handler for error messages.
  glfwSetErrorCallback([](int error, const char* description) {
    throw runtime_error("GLFW Error " + to_string(error) + ": " + description);
  });

  // Initialize GLFW.
  glfwInit();

  // Set required OpenGL context version for the window.
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  // Force GLFW to use the core profile of OpenGL.
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  // Set up anti-aliasing.
  glfwWindowHint(GLFW_SAMPLES, 4);

  // Create the window to render in.
  window = glfwCreateWindow(screen_width, screen_height, window_title,  //
                            nullptr, nullptr);

  // Initialize the OpenGL context for the current window by using glbinding.
  glfwMakeContextCurrent(window);
  glbinding::initialize(glfwGetProcAddress);

  // Make window to be closed when pressing Escape
  // by adding key event handler.
  glfwSetKeyCallback(window, [](GLFWwindow* window, int key, int scancode,
                                int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
      glfwSetWindowShouldClose(window, GLFW_TRUE);
  });

  // Add zooming when scrolling.
  glfwSetScrollCallback(window, [](GLFWwindow* window, double x, double y) {
    radius *= exp(-0.1f * float(y));
  });

  // Add resize handler.
  glfwSetFramebufferSizeCallback(
      window, [](GLFWwindow* window, int width, int height) { resize(); });
}

void init_shader() {
  // Compile and create the vertex shader.
  auto vertex_shader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertex_shader, 1, &vertex_shader_text, nullptr);
  glCompileShader(vertex_shader);
  {
    // Check for errors.
    GLint success;
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
      char info_log[512];
      glGetShaderInfoLog(vertex_shader, 512, nullptr, info_log);
      throw runtime_error(
          string("OpenGL Error: Failed to compile vertex shader!: ") +
          info_log);
    }
  }

  // Compile and create the fragment shader.
  auto fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragment_shader, 1, &fragment_shader_text, nullptr);
  glCompileShader(fragment_shader);
  {
    // Check for errors.
    GLint success;
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
      char info_log[512];
      glGetShaderInfoLog(fragment_shader, 512, nullptr, info_log);
      throw runtime_error(
          string("OpenGL Error: Failed to compile fragment shader!: ") +
          info_log);
    }
  }

  // Link vertex shader and fragment shader to shader program.
  program = glCreateProgram();
  glAttachShader(program, vertex_shader);
  glAttachShader(program, fragment_shader);
  glLinkProgram(program);
  {
    // Check for errors.
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
      char info_log[512];
      glGetProgramInfoLog(program, 512, nullptr, info_log);
      throw runtime_error(
          string("OpenGL Error: Failed to link shader program!: ") + info_log);
    }
  }

  // Delete unused shaders.
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);

  // Get identifier locations in the shader program
  // to change their values from the outside.
  mvp_location = glGetUniformLocation(program, "MVP");
  vpos_location = glGetAttribLocation(program, "vPos");

  glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
  glEnable(GL_DEPTH_TEST);
}

void init_vertex_data() {
  // Use a vertex array to be able to reference the vertex buffer and
  // the vertex attribute arrays of the triangle with one single variable.
  glGenVertexArrays(1, &vertex_array);
  glBindVertexArray(vertex_array);

  // Generate and bind the buffer which shall contain the triangle data.
  glGenBuffers(1, &vertex_buffer);
  glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
  // The data is not changing rapidly. Therefore we use GL_STATIC_DRAW.
  glBufferData(GL_ARRAY_BUFFER,
               vertices.size() * sizeof(decltype(vertices)::value_type),
               vertices.data(), GL_STATIC_DRAW);

  // Set the data layout of the position and colors
  // with vertex attribute pointers.
  glEnableVertexAttribArray(vpos_location);
  glVertexAttribPointer(vpos_location, 3, GL_FLOAT, GL_FALSE,
                        sizeof(decltype(vertices)::value_type), (void*)0);

  // Generate buffer for triangle data.
  glGenBuffers(1, &element_buffer);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, element_buffer);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               edges.size() * sizeof(decltype(edges)::value_type), edges.data(),
               GL_STATIC_DRAW);

  // Do the same for the AABB.
  // Use a vertex array to be able to reference the vertex buffer and
  // the vertex attribute arrays of the triangle with one single variable.
  glGenVertexArrays(1, &aabb_vertex_array);
  glBindVertexArray(aabb_vertex_array);

  // Generate and bind the buffer which shall contain the triangle data.
  glGenBuffers(1, &aabb_vertex_buffer);
  glBindBuffer(GL_ARRAY_BUFFER, aabb_vertex_buffer);
  // The data is not changing rapidly. Therefore we use GL_STATIC_DRAW.
  glBufferData(
      GL_ARRAY_BUFFER,
      aabb_vertices.size() * sizeof(decltype(aabb_vertices)::value_type),
      aabb_vertices.data(), GL_STATIC_DRAW);

  // Set the data layout of the position and colors
  // with vertex attribute pointers.
  glEnableVertexAttribArray(vpos_location);
  glVertexAttribPointer(vpos_location, 3, GL_FLOAT, GL_FALSE,
                        sizeof(decltype(aabb_vertices)::value_type), (void*)0);

  // Generate buffer for triangle data.
  glGenBuffers(1, &aabb_element_buffer);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, aabb_element_buffer);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               aabb_edges.size() * sizeof(decltype(aabb_edges)::value_type),
               aabb_edges.data(), GL_STATIC_DRAW);
}

void resize() {
  // Update size parameters and compute aspect ratio.
  glfwGetFramebufferSize(window, &screen_width, &screen_height);
  const auto aspect_ratio = float(screen_width) / screen_height;
  // Make sure rendering takes place in the full screen.
  glViewport(0, 0, screen_width, screen_height);
  // Use a perspective projection with correct aspect ratio.
  projection = glm::perspective(fov, aspect_ratio, 0.1f, 10000.f);
  // Position the camera in space by using a view matrix.
  // view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -2));
}

void update() {
  glm::vec3 camera{cos(altitude) * cos(azimuth), cos(altitude) * sin(azimuth),
                   sin(altitude)};
  camera *= radius;
  view = glm::lookAt(camera + origin, origin, up);
  const auto camera_right = normalize(cross(-camera, up));
  const auto camera_up = normalize(cross(camera_right, -camera));
  const float pixel_size =
      2.0f * tan(0.5f * fov * M_PI / 180.0f) / screen_height;

  old_mouse_pos = mouse_pos;
  double xpos, ypos;
  glfwGetCursorPos(window, &xpos, &ypos);
  mouse_pos = glm::vec2{xpos, ypos};
  const auto mouse_move = mouse_pos - old_mouse_pos;

  int state = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
  if (state == GLFW_PRESS) {
    altitude += mouse_move.y * 0.01;
    azimuth -= mouse_move.x * 0.01;
    constexpr float bound = M_PI_2 - 1e-5f;
    altitude = clamp(altitude, -bound, bound);
  }
  state = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT);
  if (state == GLFW_PRESS) {
    const auto scale = 1.3f * pixel_size * length(camera);
    origin +=
        -scale * mouse_move.x * camera_right + scale * mouse_move.y * camera_up;
  }

  // glm::mat4 projection = glm::perspective(fov, ratio, 0.1f, 10000.f);

  // Compute and set MVP matrix in shader.
  // model = glm::mat4{1.0f};
  // const auto axis = glm::normalize(glm::vec3(1, 1, 1));
  // model = rotate(model, float(glfwGetTime()), axis);
  const auto mvp = projection * view * model;
  glUniformMatrix4fv(mvp_location, 1, GL_FALSE, glm::value_ptr(mvp));
}

void render() {
  // Clear the screen.
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // Bind vertex array of triangle
  // and use the created shader
  // to render the triangle.
  glUseProgram(program);
  glBindVertexArray(vertex_array);
  glLineWidth(1.5f);
  glDrawElements(GL_LINES, 2 * edges.size(), GL_UNSIGNED_INT, 0);
  glBindVertexArray(aabb_vertex_array);
  glLineWidth(3.0f);
  glDrawElements(GL_LINES, 3 * 2, GL_UNSIGNED_INT, 0);
  glLineWidth(1.0f);
  glDrawElements(GL_LINES, 9 * 2, GL_UNSIGNED_INT,
                 (void*)(3 * sizeof(decltype(aabb_edges)::value_type)));
}

}  // namespace detail

}  // namespace application
