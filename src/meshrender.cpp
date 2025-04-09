#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <cmath>
#include <iostream>
#include <vector>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "GridElements.h"
#include "STLStringHelper.h"

///	<summary>
///		Zoom information.
///	</summary>
float zoomLevel = 1.0f;
const float ZOOM_STEP = 0.4f;
std::chrono::steady_clock::time_point lastClickTime;
const double DOUBLE_CLICK_THRESHOLD = 0.3; // seconds

///	<summary>
///		Get the vertices and incides of the sphere.
///	</summary>
void createSphere(
	std::vector<float> & vertices,
	std::vector<unsigned int> & indices,
	int stacks,
	int slices
) {
	for (int i = 0; i <= stacks; ++i) {
		float V = (float)i / stacks;
		float phi = V * M_PI;

		for (int j = 0; j <= slices; ++j) {
			float U = (float)j / slices;
			float theta = U * (2.0f * M_PI);

			float x = - cosf(theta) * sinf(phi);
			float y = cosf(phi);
			float z = - sinf(theta) * sinf(phi);

			x *= 0.999;
			y *= 0.999;
			z *= 0.999;

			vertices.push_back(x);
			vertices.push_back(y);
			vertices.push_back(z);
			vertices.push_back(U); // texCoord u
			vertices.push_back(V); // texCoord v
		}
	}

	for (int i = 0; i < stacks; ++i) {
		for (int j = 0; j < slices; ++j) {
			int first = (i * (slices + 1)) + j;
			int second = first + slices + 1;

			indices.push_back(first);
			indices.push_back(second);
			indices.push_back(first + 1);

			indices.push_back(second);
			indices.push_back(second + 1);
			indices.push_back(first + 1);
		}
	}
}

///	<summary>
///		Get vertices and indices from a mesh file
///	</summary>
void getMesh(
	const std::string & filename,
	std::vector<float> & vertices,
	std::vector<unsigned int> & indices
) {
	Mesh mesh(filename);

	vertices.resize(5 * mesh.nodes.size());
	indices.resize(4 * mesh.faces.size());

	for (size_t v = 0; v < mesh.nodes.size(); v++) {
		vertices[5*v+0] = static_cast<float>(mesh.nodes[v].x);
		vertices[5*v+1] = static_cast<float>(mesh.nodes[v].z);
		vertices[5*v+2] = static_cast<float>(mesh.nodes[v].y);
		vertices[5*v+3] = 0.0f;
		vertices[5*v+4] = 0.0f;
	}

	for (size_t f = 0; f < mesh.faces.size(); f++) {
		indices[4*f+0] = static_cast<unsigned int>(mesh.faces[f][0]);
		indices[4*f+1] = static_cast<unsigned int>(mesh.faces[f][1]);
		indices[4*f+2] = static_cast<unsigned int>(mesh.faces[f][2]);
		indices[4*f+3] = static_cast<unsigned int>(mesh.faces[f][3]);
	}
}

///	<summary>
///		Load the texture from a file.
///	</summary>
GLuint loadTexture(const char* filename) {
	int width, height, channels;
	unsigned char* image = stbi_load(filename, &width, &height, &channels, STBI_rgb);

	if (!image) {
		std::cerr << "Failed to load image: " << filename << std::endl;
		return 0;
	}

	GLuint textureID;
	glGenTextures(1, &textureID);
	glBindTexture(GL_TEXTURE_2D, textureID);
	
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, image);
	glGenerateMipmap(GL_TEXTURE_2D);

	// Texture parameters
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);	
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);	
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	stbi_image_free(image);
	return textureID;
}

///	<summary>
///		Vertex shader.
///	</summary>
const char* vertexShaderSrc = R"(
#version 120
attribute vec3 aPos;
attribute vec2 aTexCoord;
varying vec2 TexCoord;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main() {
	gl_Position = projection * view * model * vec4(aPos, 1.0);
	TexCoord = aTexCoord;
}
)";

///	<summary>
///		Fragment shader.
///	</summary>
const char* fragmentShaderSrc = R"(
#version 120
varying vec2 TexCoord;
uniform sampler2D texture1;
uniform bool useTexture;
uniform vec4 lineColor;
void main() {
	if (useTexture)
		gl_FragColor = texture2D(texture1, TexCoord);
	else
		gl_FragColor = lineColor;
}
)";

///	<summary>
///		Compile the shader.
///	</summary>
GLuint compileShader(GLenum type, const char* src) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &src, NULL);
	glCompileShader(shader);
	return shader;
}

///	<summary>
///		Create the shader program.
///	</summary>
GLuint createShaderProgram() {
	GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSrc);
	GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSrc);
	GLuint program = glCreateProgram();
	glAttachShader(program, vertexShader);
	glAttachShader(program, fragmentShader);
	glBindAttribLocation(program, 0, "aPos");
	glBindAttribLocation(program, 1, "aTexCoord");
	glLinkProgram(program);
	return program;
}

///	<summary>
///		Camera rotation handling.
///	</summary>
float angleX = 0.0f, angleY = 0.0f;
bool mousePressed = false;
double lastX, lastY;

void cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
	if (mousePressed) {
		float deltaX = (float)(xpos - lastX);
		float deltaY = (float)(ypos - lastY);
		angleX += deltaY * 0.02f / sqrt(zoomLevel);
		angleY += deltaX * 0.02f / sqrt(zoomLevel);
	}
	lastX = xpos;
	lastY = ypos;
}

///	<summary>
///		Handle mouse scrolling.
///	</summary>
void mouseScrollCallback(GLFWwindow* window, double xpos, double ypos) {
	if (ypos > 0.0) {
		zoomLevel *= (1.0 + atan(ypos) * 2.0 / M_PI);
	} else { 
		zoomLevel /= (1.0 + atan(-ypos) * 2.0 / M_PI);
	}
}

///	<summary>
///		Handle mouse clicks.
///	</summary>
void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
	if (button == GLFW_MOUSE_BUTTON_LEFT) {
		if (action == GLFW_PRESS) {
			mousePressed = true;

			auto now = std::chrono::steady_clock::now();
			std::chrono::duration<double> elapsed = now - lastClickTime;

			if (elapsed.count() < DOUBLE_CLICK_THRESHOLD) {
				zoomLevel *= 1.0 + ZOOM_STEP;
			}

			lastClickTime = now;

		} else if (action == GLFW_RELEASE) {
			mousePressed = false;
		}
	}
	if (button == GLFW_MOUSE_BUTTON_RIGHT) {
		if (action == GLFW_PRESS) {
			mousePressed = true;

			auto now = std::chrono::steady_clock::now();
			std::chrono::duration<double> elapsed = now - lastClickTime;

			if (elapsed.count() < DOUBLE_CLICK_THRESHOLD) {
				zoomLevel /= 1.0 + ZOOM_STEP;
			}

			lastClickTime = now;

		} else if (action == GLFW_RELEASE) {
			mousePressed = false;
		}
	}
}

///	<summary>
///		Entry point to executable.
///	</summary>
int main(int argc, char** argv) {

	std::string strMesh;
	std::string strTexture("BlueMarble_June2004_11km.jpg");
	std::string strLineColor;
	std::string strLineWidth;

	float dLineWidth = 1.0f;
	float dLineColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};

	bool fPrintUsage = false;
	if (argc < 2) {
		fPrintUsage = true;
	} else {
		for (int c = 1; c < argc; c++) {
			if (argv[c][0] == '-') {
				if (c == argc-1) {
					printf("ERROR: Missing parameter for argument %s\n", argv[c]);
					fPrintUsage = true;
					break;
				}
				if (strcmp(argv[c],"-b") == 0) {
					strTexture = argv[c+1];
				} else if (strcmp(argv[c],"-lc") == 0) {
					strLineColor = argv[c+1];
				} else if (strcmp(argv[c],"-lw") == 0) {
					strLineWidth = argv[c+1];
				}
				c++;

			} else {
				if (c != argc-1) {
					fPrintUsage = true;
					break;
				}
				strMesh = argv[c];
			}
		}
	}
	if (strLineWidth.length() == 0) {
	} else if (!STLStringHelper::IsFloat(strLineWidth)) {
		printf("ERROR: -lw must be of type float\n");
		fPrintUsage = true;
	} else {
		dLineWidth = std::stof(strLineWidth);
		if (dLineWidth <= 0.0) {
			printf("ERROR: -lw must be positive");
			fPrintUsage = true;
		}
	}
	if (strLineColor.length() != 0) {
		STLStringHelper::ToLower(strLineColor);
		if (strLineColor == "white") {
			dLineColor[0] = dLineColor[1] = dLineColor[2] = dLineColor[3] = 1.0f;
		} else if (strLineColor == "black") {
			dLineColor[0] = dLineColor[1] = dLineColor[2] = 0.0f;
			dLineColor[3] = 1.0f;
		} else {
			std::vector<std::string> vecLineColorA;
			STLStringHelper::ParseVariableList(strLineColor, vecLineColorA);
			if ((vecLineColorA.size() < 3) || (vecLineColorA.size() > 4)) {
				printf("ERROR: -lc must be a name, RGB or RGBA colorspec\n");
				fPrintUsage = true;
			} else {
				dLineColor[3] = 1.0f;
				for (size_t s = 0; s < vecLineColorA.size(); s++) {
					if (!STLStringHelper::IsFloat(vecLineColorA[s])) {
						printf("ERROR: -lc RGB or RGBA colorspec must have values 0.0-1.0\n");
						fPrintUsage = true;
						break;
					} else {
						dLineColor[s] = std::stof(vecLineColorA[s]);
						if ((dLineColor[s] < 0.0) || (dLineColor[s] > 1.0)) {
							printf("ERROR: -lc RGB or RGBA colorspec must have values 0.0-1.0\n");
							fPrintUsage = true;
							break;
						}
					}
				}
			}
		}
	}

	if (fPrintUsage) {
		printf("meshrender [-b img] [-lc lcol] [-lw lwidth] <mesh file>\n");
		printf("  [-b img]           Globe image file\n");
		printf("  [-lc lcol]         Line color spec (name or R,G,B[,A])\n");
		printf("  [-lw lwidth]       Line width (default 1.0)\n");
		return (-1);
	}

	// Initialize window
	if (!glfwInit()) return -1;
	GLFWwindow* window = glfwCreateWindow(800, 800, "meshrender", NULL, NULL);
	if (!window) {
		glfwTerminate(); return -1;
	}
	glfwMakeContextCurrent(window);

	glewExperimental = GL_TRUE;
	if (glewInit() != GLEW_OK) return -1;

	// Generate vertex arrays and buffers
	GLuint vao[2], vbo[2], ebo[2];
	glGenVertexArrays(2, vao);
	glGenBuffers(2, vbo);
	glGenBuffers(2, ebo);

	// Generate the sphere and corresponding buffers
	std::vector<float> verticesSphere;
	std::vector<unsigned int> indicesSphere;
	createSphere(verticesSphere, indicesSphere, 40, 40);

	glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
	glBufferData(GL_ARRAY_BUFFER, verticesSphere.size() * sizeof(float), verticesSphere.data(), GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo[0]);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, indicesSphere.size() * sizeof(unsigned int), indicesSphere.data(), GL_STATIC_DRAW);

	GLuint texture = loadTexture(strTexture.c_str());

	// Generate the mesh and corresponding buffers
	std::vector<float> verticesMesh;
	std::vector<unsigned int> indicesMesh;
	getMesh(strMesh, verticesMesh, indicesMesh);

	glBindBuffer(GL_ARRAY_BUFFER, vbo[1]);
	glBufferData(GL_ARRAY_BUFFER, verticesMesh.size() * sizeof(float), verticesMesh.data(), GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo[1]);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, indicesMesh.size() * sizeof(unsigned int), indicesMesh.data(), GL_STATIC_DRAW);

	// Initialize the shader and load the texture
	GLuint shaderProgram = createShaderProgram();

	glEnable(GL_DEPTH_TEST);

	// Mesh drawing settings
	glLineWidth(dLineWidth);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_LINE_SMOOTH);
	glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

	glfwSetCursorPosCallback(window, cursorPosCallback);
	glfwSetMouseButtonCallback(window, mouseButtonCallback);
	glfwSetScrollCallback(window, mouseScrollCallback);

	glUseProgram(shaderProgram);

	// Set line color
	GLuint lineColorLoc = glGetUniformLocation(shaderProgram, "lineColor");
	//glUniform4fv(lineColorLoc, 4, dLineColor);
	glUniform4f(lineColorLoc, dLineColor[0], dLineColor[1], dLineColor[2], dLineColor[3]);

	while (!glfwWindowShouldClose(window)) {
		glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// Rotation matrix based on user input
		float identity[16] = {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		};

		float zoommatrix[16] = {
			zoomLevel, 0.0f, 0.0f, 0.0f,
			0.0f, zoomLevel, 0.0f, 0.0f,
			0.0f, 0.0f, 0.5f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		};

		float model[16] = {
			cos(angleY), sin(angleY) * sin(angleX), sin(angleY) * cos(angleX), 0.0f,
			0.0f, cos(angleX), -sin(angleX), 0.0f,
			-sin(angleY), cos(angleY) * sin(angleX), cos(angleY) * cos(angleX), 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		};

		GLint modelLoc = glGetUniformLocation(shaderProgram, "model");
		GLint viewLoc = glGetUniformLocation(shaderProgram, "view");
		GLint projLoc = glGetUniformLocation(shaderProgram, "projection");

		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, model);
		glUniformMatrix4fv(viewLoc, 1, GL_FALSE, zoommatrix); // Identity view (no camera movement)
		glUniformMatrix4fv(projLoc, 1, GL_FALSE, identity); // Identity projection (no perspective)

		// Texture flag
		GLuint useTextureLoc = glGetUniformLocation(shaderProgram, "useTexture");

		// Create the globe
		glUniform1i(useTextureLoc, GL_TRUE);
		glBindVertexArray(vao[0]);
		glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo[0]);
		glBindTexture(GL_TEXTURE_2D, texture);

		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(0); // Position
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
		glEnableVertexAttribArray(1); // TexCoord

		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glDrawElements(GL_TRIANGLES, indicesSphere.size(), GL_UNSIGNED_INT, 0);

		// Draw the mesh
		glUniform1i(useTextureLoc, GL_FALSE);
		glBindVertexArray(vao[1]);
		glBindBuffer(GL_ARRAY_BUFFER, vbo[1]);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo[1]);

		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(0); // Position
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
		glEnableVertexAttribArray(1); // TexCoord

		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glDrawElements(GL_QUADS, indicesMesh.size(), GL_UNSIGNED_INT, 0);

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	glDeleteVertexArrays(2, vao);
	glDeleteBuffers(2, vbo);
	glDeleteBuffers(2, ebo);

	glfwTerminate();
	return 0;
}

