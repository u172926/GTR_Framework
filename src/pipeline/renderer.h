#pragma once
#include "scene.h"
#include "prefab.h"

#include "light.h"
#include "../gfx/sphericalharmonics.h"


//forward declarations
class Camera;
class Skeleton;

namespace GFX {
	class Shader;
	class Mesh;
	class FBO;
}

struct sProbe{
	vec3 pos;
	vec3 local;
	int index;
	SphericalHarmonics sh;
};

namespace SCN {

	class Prefab;
	class Material;

	enum eRenderMode {
		TEXTURED,
		LIGHTS,
		DEFERRED
	};

	enum eShaderMode {
		FLAT,
		TEXTURE,
		MULTIPASS,
		PBR
	};

	struct RenderCall {
	public:
		GFX::Mesh* mesh;
		Material* material;
		Matrix44 model;

		float camera_distance;
	};

	// This class is in charge of rendering anything in our system.
	// Separating the render from anything else makes the code cleaner
	class Renderer
	{
	public:
		bool render_wireframe;
		bool render_boundaries;
		bool show_shadowmaps;
		bool show_gbuffers;
		bool shadowmap_on;
		bool dithering;
		bool global_position;
		bool show_ssao;

		eRenderMode render_mode;
		eShaderMode shader_mode;

		float tonemapper_scale;
		float average_lum;
		float lum_white2;
		float gamma;

		GFX::Texture* skybox_cubemap;

		SCN::Scene* scene;

		//vector of all the lights of the scene
		std::vector<LightEntity*> lights;
		std::vector<LightEntity*> visible_lights;
		std::vector<RenderCall> render_calls;
		std::vector<RenderCall> render_calls_alpha;
		
		std::vector<vec3> ssao_points;
		float ssao_radius;

		GFX::FBO* gbuffer_fbo;
		GFX::FBO* illumination_fbo;
		GFX::FBO* ssao_fbo;

		//updated every frame
		Renderer(const char* shaders_atlas_filename);

		//just to be sure we have everything ready for the rendering
		void setupScene(Camera* camera);

		//add here your functions
		//...
		void orderRender(SCN::Node* node, Camera* camera);

		//renders several elements of the scene
		void renderScene(SCN::Scene* scene, Camera* camera);
		void renderForward(SCN::Scene* scene, Camera* camera);
		void renderDeferred(SCN::Scene* scene, Camera* camera);
		void renderFrame(SCN::Scene* scene, Camera* camera);

		void generateShadowMaps();

		//render the skybox
		void renderSkybox(GFX::Texture* cubemap);

		//to render one node from the prefab and its children
		void renderNode(Matrix44 model, GFX::Mesh* mesh, SCN::Material* material, Camera* camera);
		
		//to render one mesh given its material and transformation matrix
		void renderMeshWithMaterial(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material);
		void renderMeshWithMaterialFlat(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material);
		void renderMeshWithMaterialLight(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material);
		void renderMeshWithMaterialGBuffers(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material);

		void captureProbe(sProbe& probe);
		void renderProbe(sProbe& probe);

		void showUI();

		void cameraToShader(Camera* camera, GFX::Shader* shader); //sends camera uniforms to shader
		void lightToShader(LightEntity* light, GFX::Shader* shader); //sends light uniforms to shader

		void debugShadowMaps();
	};

};

std::vector<vec3> generateSpherePoints(int num, float radius, bool hemi);


