#include "../Externals/Include/Include.h"

#define MENU_TIMER_START 1
#define MENU_TIMER_STOP 2
#define MENU_EXIT 3

/*----------Global----------*/
GLubyte timer_cnt = 0;
bool timer_enabled = true;
unsigned int timer_speed = 16;

GLuint program;
GLint um4p;
GLint um4mv;
GLuint tex_location;

GLuint skybox_prog;
GLuint tex_envmap;
GLuint skybox_vao;

using namespace glm;
using namespace std;

mat4 mv_matrix;
mat4 proj_matrix;
mat4 view_matrix;
mat4 sky_view;

GLfloat leftRight = -90.0f;
GLfloat upDown = 0.0f;
GLfloat lastX = 0.0f;
GLfloat lastY = 0.0f;
GLfloat sky_leftRight = -90.0f;
GLfloat sky_upDown = 0.0f;

vec3 cameraPosition (0.0f, 3.0f, 0.0f);
vec3 cameraDirection (0.0f, 0.0f, -1.0f);
vec3 cameraNormal (0.0f, 1.0f, 0.0f);
vec3 sky_cameraDirection(0.0f, 0.0f, -1.0f);

bool firstMouse = true;

struct
{
	struct
	{
		GLint mv_matrix;
		GLint proj_matrix;
	} render;
	struct
	{
		GLint view_matrix;
	} skybox;
} uniforms;

struct Shape
{
	GLuint vao;
	GLuint vbo_position;
	GLuint vbo_normal;
	GLuint vbo_texcoord;
	GLuint ibo;
	int drawCount;
	int materialID;
};
struct Material
{
	GLuint diffuse_tex;
};

vector<Shape> arr_shape;
vector<Material> arr_material;

char** loadShaderSource(const char* file)
{
    FILE* fp = fopen(file, "rb");
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *src = new char[sz + 1];
    fread(src, sizeof(char), sz, fp);
    src[sz] = '\0';
    char **srcp = new char*[1];
    srcp[0] = src;
    return srcp;
}

void freeShaderSource(char** srcp)
{
    delete[] srcp[0];
    delete[] srcp;
}

void shaderLog(GLuint shader)
{
	GLint isCompiled = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
	if (isCompiled == GL_FALSE)
	{
		GLint maxLength = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);

		// The maxLength includes the NULL character
		GLchar* errorLog = new GLchar[maxLength];
		glGetShaderInfoLog(shader, maxLength, &maxLength, &errorLog[0]);

		printf("%s\n", errorLog);
		delete[] errorLog;
	}
}

// define a simple data structure for storing texture image raw data
typedef struct _TextureData
{
    _TextureData(void) :
        width(0),
        height(0),
        data(0)
    {
    }

    int width;
    int height;
    unsigned char* data;
} TextureData;

// load a png image and return a TextureData structure with raw data
// not limited to png format. works with any image format that is RGBA-32bit
TextureData loadPNG(const char* const pngFilepath)
{
    TextureData texture;
    int components;

    // load the texture with stb image, force RGBA (4 components required)
    stbi_uc *data = stbi_load(pngFilepath, &texture.width, &texture.height, &components, 4);

    // is the image successfully loaded?
    if (data != NULL)
    {
        // copy the raw data
        size_t dataSize = texture.width * texture.height * 4 * sizeof(unsigned char);
        texture.data = new unsigned char[dataSize];
        memcpy(texture.data, data, dataSize);

        // mirror the image vertically to comply with OpenGL convention
        for (size_t i = 0; i < texture.width; ++i)
        {
            for (size_t j = 0; j < texture.height / 2; ++j)
            {
                for (size_t k = 0; k < 4; ++k)
                {
                    size_t coord1 = (j * texture.width + i) * 4 + k;
                    size_t coord2 = ((texture.height - j - 1) * texture.width + i) * 4 + k;
                    std::swap(texture.data[coord1], texture.data[coord2]);
                }
            }
        }

        // release the loaded image
        stbi_image_free(data);
    }

    return texture;
}

void Load_Mesh()
{
	const aiScene *scene = aiImportFile("sponza.obj", aiProcessPreset_TargetRealtime_MaxQuality);

	for (unsigned int i = 0; i < scene->mNumMaterials; ++i)
	{
		aiMaterial *material = scene->mMaterials[i];
		Material materials;
		aiString texturePath;
		TextureData texture;
		if (material->GetTexture(aiTextureType_DIFFUSE, 0, &texturePath) == aiReturn_SUCCESS)
		{
			// load width, height and data from texturePath.C_Str();
			texture = loadPNG(texturePath.C_Str());
			glGenTextures(1, &materials.diffuse_tex);
			glBindTexture(GL_TEXTURE_2D, materials.diffuse_tex);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texture.width, texture.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture.data);
			glGenerateMipmap(GL_TEXTURE_2D);
		}
		else
		{
			// load some default image as default_diffuse_tex
			scene->mMaterials[1]->GetTexture(aiTextureType_DIFFUSE, 0, &texturePath);
			TextureData tex = loadPNG(texturePath.C_Str());

			glGenTextures(1, &materials.diffuse_tex);
			glBindTexture(GL_TEXTURE_2D, materials.diffuse_tex);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tex.width, tex.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex.data);
			glGenerateMipmap(GL_TEXTURE_2D);
		}
		arr_material.push_back(materials);
	}

	for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
	{
		aiMesh *mesh = scene->mMeshes[i];
		Shape shape;
		float* position = new float[mesh->mNumVertices * 3];
		float* normal = new float[mesh->mNumVertices * 3];
		float* texcoord = new float[mesh->mNumVertices * 3];
		unsigned int* index = new unsigned int[mesh->mNumFaces * 3];
		int index_po, index_nor, index_tex, index_ibo;

		glGenVertexArrays(1, &shape.vao);
		glBindVertexArray(shape.vao);

		// create 3 vbos to hold data
		glGenBuffers(1, &shape.vbo_position);
		glGenBuffers(1, &shape.vbo_normal);
		glGenBuffers(1, &shape.vbo_texcoord);
		
		index_po = 0;
		index_nor = 0;
		index_tex = 0;
		for (unsigned int v = 0; v < mesh->mNumVertices; ++v)
		{
			position[index_po++] = mesh->mVertices[v][0];
			position[index_po++] = mesh->mVertices[v][1];
			position[index_po++] = mesh->mVertices[v][2];

			normal[index_nor++] = mesh->mNormals[v][0];
			normal[index_nor++] = mesh->mNormals[v][1];
			normal[index_nor++] = mesh->mNormals[v][2];

			texcoord[index_tex++] = mesh->mTextureCoords[0][v][0];
			texcoord[index_tex++] = mesh->mTextureCoords[0][v][1];
		}
		glBindBuffer(GL_ARRAY_BUFFER, shape.vbo_position);
		glBufferData(GL_ARRAY_BUFFER, mesh->mNumVertices * 3 * sizeof(float), position, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);
		
		glBindBuffer(GL_ARRAY_BUFFER, shape.vbo_normal);
		glBufferData(GL_ARRAY_BUFFER, mesh->mNumVertices * 3 * sizeof(float), normal, GL_STATIC_DRAW);
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 0, NULL);

		glBindBuffer(GL_ARRAY_BUFFER, shape.vbo_texcoord);
		glBufferData(GL_ARRAY_BUFFER, mesh->mNumVertices * 2 * sizeof(float), texcoord, GL_STATIC_DRAW);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, NULL);

		// create 1 ibo to hold data
		glGenBuffers(1, &shape.ibo);
		index_ibo = 0;
		for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
		{
			index[index_ibo++] = mesh->mFaces[f].mIndices[0];
			index[index_ibo++] = mesh->mFaces[f].mIndices[1];
			index[index_ibo++] = mesh->mFaces[f].mIndices[2];
		}
		glBindBuffer(GL_ARRAY_BUFFER, shape.ibo);
		glBufferData(GL_ARRAY_BUFFER, mesh->mNumFaces * 3 * sizeof(float), index, GL_STATIC_DRAW);

		shape.materialID = mesh->mMaterialIndex;
		shape.drawCount = mesh->mNumFaces * 3;
		arr_shape.push_back(shape);
	}

	aiReleaseImport(scene);
}

void My_Init()
{
	glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);

	/*-----------skybox program---------------*/
	skybox_prog = glCreateProgram();
	GLuint skybox_frag = glCreateShader(GL_FRAGMENT_SHADER);
	GLuint skybox_ver = glCreateShader(GL_VERTEX_SHADER);

	char** skyboxVertexSource = loadShaderSource("skybox.vs.glsl");
	char** skyboxFragmentSource = loadShaderSource("skybox.fs.glsl");

	glShaderSource(skybox_ver, 1, skyboxVertexSource, NULL);
	glShaderSource(skybox_frag, 1, skyboxFragmentSource, NULL);

	freeShaderSource(skyboxVertexSource);
	freeShaderSource(skyboxFragmentSource);

	glCompileShader(skybox_ver);
	glCompileShader(skybox_frag);

	glAttachShader(skybox_prog, skybox_ver);
	glAttachShader(skybox_prog, skybox_frag);

	shaderLog(skybox_ver);
	shaderLog(skybox_frag);

	glLinkProgram(skybox_prog);
	glUseProgram(skybox_prog);

	uniforms.skybox.view_matrix = glGetUniformLocation(skybox_prog, "view_matrix");

	glGenVertexArrays(1, &skybox_vao);
	
	/*-----------render program--------------*/
	program = glCreateProgram();

	GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
	GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);

	char** vertexShaderSource = loadShaderSource("vertex.vs.glsl");
	char** fragmentShaderSource = loadShaderSource("fragment.fs.glsl");

	glShaderSource(vertexShader, 1, vertexShaderSource, NULL);
	glShaderSource(fragmentShader, 1, fragmentShaderSource, NULL);

	freeShaderSource(vertexShaderSource);
	freeShaderSource(fragmentShaderSource);

	glCompileShader(vertexShader);
	glCompileShader(fragmentShader);

	shaderLog(vertexShader);
	shaderLog(fragmentShader);

	glAttachShader(program, vertexShader);
	glAttachShader(program, fragmentShader);

	glLinkProgram(program);

	um4p = glGetUniformLocation(program, "um4p");
	um4mv = glGetUniformLocation(program, "um4mv");
	tex_location = glGetUniformLocation(program, "tex");

	glUseProgram(program);

	TextureData envmap_data = loadPNG("mountaincube.png");
	glGenTextures(1, &tex_envmap);
	glBindTexture(GL_TEXTURE_CUBE_MAP, tex_envmap);
	for (int i = 0; i < 6; ++i)
	{
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA, envmap_data.width, envmap_data.height / 6, 0, GL_RGBA, GL_UNSIGNED_BYTE, envmap_data.data + i * (envmap_data.width * (envmap_data.height / 6) * sizeof(unsigned char) * 4));
	}
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	delete[] envmap_data.data;

	glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

	Load_Mesh();
}

void My_Display()
{
	static const GLfloat gray[] = { 0.2f, 0.2f, 0.2f, 1.0f };
	static const GLfloat ones[] = { 1.0f };
	
	//glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	//glUseProgram(program);

	view_matrix = lookAt(cameraPosition, cameraPosition + cameraDirection, cameraNormal);
	sky_view = lookAt(cameraPosition, cameraPosition + sky_cameraDirection, cameraNormal);
	mv_matrix = view_matrix * mat4(1.0f);

	glClearBufferfv(GL_COLOR, 0, gray);
	glClearBufferfv(GL_DEPTH, 0, ones);
	glBindTexture(GL_TEXTURE_CUBE_MAP, tex_envmap);

	glUseProgram(skybox_prog);
	glBindVertexArray(skybox_vao);

	glUniformMatrix4fv(uniforms.skybox.view_matrix, 1, GL_FALSE, value_ptr(transpose(sky_view)));

	glDisable(GL_DEPTH_TEST);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glEnable(GL_DEPTH_TEST);

	glUseProgram(program);
	
	glUniformMatrix4fv(um4mv, 1, GL_FALSE, value_ptr(mv_matrix));
	glUniformMatrix4fv(um4p, 1, GL_FALSE, value_ptr(proj_matrix));
	glActiveTexture(GL_TEXTURE0);
	glUniform1i(tex_location, 0);
	for (int i = 0; i < arr_shape.size(); ++i)
	{
		glBindVertexArray(arr_shape[i].vao);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, arr_shape[i].ibo);
		int materialID = arr_shape[i].materialID;

		glBindTexture(GL_TEXTURE_2D, arr_material[materialID].diffuse_tex);
		glDrawElements(GL_TRIANGLES, arr_shape[i].drawCount, GL_UNSIGNED_INT, 0);
	}

    glutSwapBuffers();
}

void My_Reshape(int width, int height)
{
	glViewport(0, 0, width, height);
	float viewportAspect = (float)width / (float)height;
	proj_matrix = perspective(radians(60.0f), viewportAspect, 0.1f, 1000.0f);
	lastX = width / 2.0;
	lastY = height / 2.0;
}

void My_Timer(int val)
{
	glutPostRedisplay();
	glutTimerFunc(timer_speed, My_Timer, val);
}

void My_Mouse(int button, int state, int x, int y)
{
	if(state == GLUT_DOWN)
	{
		printf("Mouse %d is pressed at (%d, %d)\n", button, x, y);
		firstMouse = true;
	}
	else if(state == GLUT_UP)
	{
		printf("Mouse %d is released at (%d, %d)\n", button, x, y);
	}
}

void My_Drag(int nowX, int nowY)
{

	//printf("Mouse dragging: (%d, %d)\n", nowX, nowY);
	if (firstMouse) {
		lastX = nowX;
		lastY = nowY;
		firstMouse = false;
	}

	GLfloat xOffset = nowX - lastX;
	GLfloat yOffset = lastY - nowY;
	lastX = nowX;
	lastY = nowY;

	GLfloat sense = 0.08;
	xOffset *= sense;
	yOffset *= sense;

	printf("(%d, %d)\n", xOffset, yOffset);

	leftRight -= xOffset;
	upDown -= yOffset;
	
	sky_leftRight += xOffset;
	sky_upDown += yOffset;

	if (upDown > 89.0f)
		upDown = 89.0f;
	if (upDown < -89.0f)
		upDown = -89.0f;

	if (sky_upDown > 89.0f)
		sky_upDown = 89.0f;
	if (sky_upDown < -89.0f)
		sky_upDown = -89.0f;

	vec3 dir;
	dir.x = cos(radians(leftRight)) * cos(radians(upDown));
	dir.y = sin(radians(upDown));
	dir.z = sin(radians(leftRight)) * cos(radians(upDown));
	cameraDirection = normalize(dir);

	dir.x = cos(radians(sky_leftRight)) * cos(radians(sky_upDown));
	dir.y = sin(radians(sky_upDown));
	dir.z = sin(radians(sky_leftRight)) * cos(radians(sky_upDown));
	sky_cameraDirection = normalize(dir);
}

void My_Keyboard(unsigned char key, int x, int y)
{
	printf("Key %c is pressed at (%d, %d)\n", key, x, y);
	
	GLfloat cameraSpeed = 0.1f;
	if (key == 'w') {
		cameraPosition += cameraSpeed * cameraDirection;
	}
	else if (key == 's') {
		cameraPosition -= cameraSpeed * cameraDirection;
	}
	else if (key == 'a') {
		cameraPosition -= normalize(cross(cameraDirection, cameraNormal)) * cameraSpeed;
	}
	else if (key == 'd') {
		cameraPosition += normalize(cross(cameraDirection, cameraNormal)) * cameraSpeed;
	}
	else if (key == 'x') {
		cameraPosition += cameraSpeed * cameraNormal;
	}
	else if (key == 'z') {
		cameraPosition -= cameraSpeed * cameraNormal;
	}
}

void My_SpecialKeys(int key, int x, int y)
{
	switch(key)
	{
	case GLUT_KEY_F1:
		printf("F1 is pressed at (%d, %d)\n", x, y);
		break;
	case GLUT_KEY_PAGE_UP:
		printf("Page up is pressed at (%d, %d)\n", x, y);
		break;
	case GLUT_KEY_LEFT:
		printf("Left arrow is pressed at (%d, %d)\n", x, y);
		break;
	default:
		printf("Other special key is pressed at (%d, %d)\n", x, y);
		break;
	}
}

void My_Menu(int id)
{
	switch(id)
	{
	case MENU_TIMER_START:
		if(!timer_enabled)
		{
			timer_enabled = true;
			glutTimerFunc(timer_speed, My_Timer, 0);
		}
		break;
	case MENU_TIMER_STOP:
		timer_enabled = false;
		break;
	case MENU_EXIT:
		exit(0);
		break;
	default:
		break;
	}
}

int main(int argc, char *argv[])
{
#ifdef __APPLE__
    // Change working directory to source code path
    chdir(__FILEPATH__("/../Assets/"));
#endif
	// Initialize GLUT and GLEW, then create a window.
	////////////////////
	glutInit(&argc, argv);
#ifdef _MSC_VER
	glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH);
#else
    glutInitDisplayMode(GLUT_3_2_CORE_PROFILE | GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH);
#endif
	glutInitWindowPosition(100, 100);
	glutInitWindowSize(1200, 900);
	glutCreateWindow("AS2_Framework"); // You cannot use OpenGL functions before this line; The OpenGL context must be created first by glutCreateWindow()!
#ifdef _MSC_VER
	glewInit();
#endif
    glPrintContextInfo();
	My_Init();

	// Create a menu and bind it to mouse right button.
	int menu_main = glutCreateMenu(My_Menu);
	int menu_timer = glutCreateMenu(My_Menu);

	glutSetMenu(menu_main);
	glutAddSubMenu("Timer", menu_timer);
	glutAddMenuEntry("Exit", MENU_EXIT);

	glutSetMenu(menu_timer);
	glutAddMenuEntry("Start", MENU_TIMER_START);
	glutAddMenuEntry("Stop", MENU_TIMER_STOP);

	glutSetMenu(menu_main);
	glutAttachMenu(GLUT_RIGHT_BUTTON);

	// Register GLUT callback functions.
	glutDisplayFunc(My_Display);
	glutReshapeFunc(My_Reshape);
	glutMouseFunc(My_Mouse);
	glutMotionFunc(My_Drag);
	glutKeyboardFunc(My_Keyboard);
	glutSpecialFunc(My_SpecialKeys);
	glutTimerFunc(timer_speed, My_Timer, 0); 

	// Enter main event loop.
	glutMainLoop();

	return 0;
}
