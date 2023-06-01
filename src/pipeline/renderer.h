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

struct sProbe {
	vec3 pos; //where is located
	vec3 local; //its ijk pos in the matrix
	int index; //its index in the linear array
	SphericalHarmonics sh; //coeffs
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

	struct sIrradianceCahceInfo {
		int num_probes;
		vec3 dims;
		vec3 start;
		vec3 end;
	};

	struct sReflectionProbe {
		vec3 pos;
		GFX::Texture* texture = nullptr;
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
		bool show_tonemapper;
		bool show_global_position;
		bool show_ssao;
		bool show_probes;
		bool show_irradiance;
		bool show_ref_probes;

		eRenderMode render_mode;
		eShaderMode shader_mode;

		float tonemapper_scale;
		float average_lum;
		float lum_white2;
		float gamma;

		GFX::Texture* skybox_cubemap;
		GFX::Texture* probes_texture;

		SCN::Scene* scene;

		//vector of all the lights of the scene
		std::vector<LightEntity*> lights;
		std::vector<LightEntity*> visible_lights;
		std::vector<RenderCall> render_calls;
		std::vector<RenderCall> render_calls_alpha;
		
		std::vector<vec3> ssao_points;
		float ssao_radius;

		sIrradianceCahceInfo irradiance_cache_info;
		std::vector<sProbe> probes;
		float irr_mulitplier;

		sReflectionProbe ref_probes;

		GFX::FBO* gbuffer_fbo;
		GFX::FBO* illumination_fbo;
		GFX::FBO* ssao_fbo;
		GFX::FBO* irr_fbo;
		GFX::FBO* ref_fbo;
		GFX::FBO* plane_ref_fbo;

		//updated every frame
		Renderer(const char* shaders_atlas_filename);

		//just to be sure we have everything ready for the rendering
		void setupScene(Camera* camera);

		//add here your functions
		//...
		void orderRender(SCN::Node* node, Camera* camera);
		void renderObjects(Camera* camera);

		//renders several elements of the scene
		void renderScene(SCN::Scene* scene, Camera* camera);
		void renderForward(SCN::Scene* scene, Camera* camera);
		void renderDeferred(SCN::Scene* scene, Camera* camera);
		void renderFrame(SCN::Scene* scene, Camera* camera);

		void generateShadowMaps();

		void captureProbe(sProbe& probe);
		void renderProbe(sProbe& probe);

		void captureIrradiance();
		void loadIrradianceCache();
		void uploadIrradianceCache();
		void applyIrradiance();

		void captureReflection(SCN::Scene* scene, sReflectionProbe& probe);
		void rendereReflectionProbe(sReflectionProbe& probe);
		void renderPlanarReflection(SCN::Scene* scene, Camera* camera);
		//render the skybox
		void renderSkybox(GFX::Texture* cubemap, float intensity);

		//to render one node from the prefab and its children
		void renderNode(Matrix44 model, GFX::Mesh* mesh, SCN::Material* material, Camera* camera);
		
		//to render one mesh given its material and transformation matrix
		void renderMeshWithMaterial(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material);
		void renderMeshWithMaterialFlat(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material);
		void renderMeshWithMaterialLight(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material);
		void renderMeshWithMaterialGBuffers(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material);

		void showUI();

		void cameraToShader(Camera* camera, GFX::Shader* shader); //sends camera uniforms to shader
		void lightToShader(LightEntity* light, GFX::Shader* shader); //sends light uniforms to shader

		void debugShadowMaps();
	};

};

std::vector<vec3> generateSpherePoints(int num, float radius, bool hemi);


