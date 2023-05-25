#include "renderer.h"

#include <algorithm> //sort

#include "camera.h"
#include "../gfx/gfx.h"
#include "../gfx/shader.h"
#include "../gfx/mesh.h"
#include "../gfx/texture.h"
#include "../gfx/fbo.h"
#include "../pipeline/prefab.h"
#include "../pipeline/material.h"
#include "../pipeline/animation.h"
#include "../utils/utils.h"
#include "../extra/hdre.h"
#include "../core/ui.h"

#include "scene.h"


using namespace SCN;

//some globals
GFX::Mesh sphere;

Renderer::Renderer(const char* shader_atlas_filename)
{
	render_wireframe = false;
	render_boundaries = false;
	render_mode = eRenderMode::TEXTURED; //default
	shader_mode = eShaderMode::TEXTURE;

	scene = nullptr;
	skybox_cubemap = nullptr;
	show_shadowmaps = false;
	show_gbuffers = false;
	shadowmap_on = false;
	dithering = false;
	show_ssao = false;
	global_position = false;

	ssao_points = generateSpherePoints(64, 1, false);
	ssao_radius = 5.0;

	gbuffer_fbo = nullptr;
	illumination_fbo = nullptr;
	ssao_fbo = nullptr;

	tonemapper_scale = 1.0;
	average_lum = 1.0;
	lum_white2 = 1.0;
	gamma = 1.0;

	tonemapper_scale = 1.0;
	average_lum = 1.0;
	lum_white2 = 1.0;
	gamma = 1.0;

	if (!GFX::Shader::LoadAtlas(shader_atlas_filename))	exit(1);

	GFX::checkGLErrors();

	sphere.createSphere(1.0f);
	sphere.uploadToVRAM();
}

void Renderer::setupScene(Camera* camera)
{
	if (scene->skybox_filename.size())
		skybox_cubemap = GFX::Texture::Get(std::string(scene->base_folder + "/" + scene->skybox_filename).c_str());
	else
		skybox_cubemap = nullptr;

	//to avoid adding lights infinetly
	lights.clear();
	visible_lights.clear();
	render_calls.clear();
	render_calls_alpha.clear();

	//process entities
	for (int i = 0; i < scene->entities.size(); ++i)
	{
		BaseEntity* ent = scene->entities[i];
		if (!ent->visible)
			continue;

		//prefab entity
		if (ent->getType() == eEntityType::PREFAB)
		{
			PrefabEntity* pent = (SCN::PrefabEntity*)ent;
			if (pent->prefab) orderRender(&pent->root, camera);
		}
		//light entity
		else if (ent->getType() == eEntityType::LIGHT)
		{
			LightEntity* lent = (SCN::LightEntity*)ent;
			lights.push_back(lent);
		}
	}
	std::sort(render_calls.begin(), render_calls.end(), [](const RenderCall a, const RenderCall b)
		{ return a.camera_distance > b.camera_distance; });

	generateShadowMaps();

}

void Renderer::renderScene(SCN::Scene* scene, Camera* camera)
{
	this->scene = scene;

	setupScene(camera);

	renderFrame(scene, camera);

	if (show_shadowmaps) debugShadowMaps();

}

void Renderer::renderFrame(SCN::Scene* scene, Camera* camera)
{
	if (render_mode == eRenderMode::DEFERRED)
		renderDeferred(scene, camera);
	else
		renderForward(scene, camera);
}

void Renderer::renderDeferred(SCN::Scene* scene, Camera* camera)
{
	vec2 size = CORE::getWindowSize();
	GFX::Mesh* quad = GFX::Mesh::getQuad();
	GFX::Shader* shader = nullptr;

	//generate gbudder
	if (!gbuffer_fbo)
	{
		gbuffer_fbo = new GFX::FBO();
		gbuffer_fbo->create(size.x, size.y, 3, GL_RGBA, GL_UNSIGNED_BYTE, true);

		illumination_fbo = new GFX::FBO();
		illumination_fbo->create(size.x, size.y, 1, GL_RGB, GL_HALF_FLOAT, true); //half_float for SDR
	}
	if(!illumination_fbo)
	{
		illumination_fbo = new GFX::FBO();
		illumination_fbo->create(size.x, size.y, 1, GL_RGB, GL_HALF_FLOAT, false); //half_float for SDR	
	}
	if (!ssao_fbo)
	{
		ssao_fbo = new GFX::FBO();
		ssao_fbo->create(size.x, size.y, 3, GL_RGB, GL_UNSIGNED_BYTE, false);
	}

	camera->enable();

	//render inside the fbo all that is in the bind 
	gbuffer_fbo->bind();

		//gbuffer_fbo->enableBuffers(true, false, false, false);
		glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		//gbuffer_fbo->enableAllBuffers();

		//render entities (first ones with alpha)
		for (int i = 0; i < render_calls_alpha.size(); ++i)
		{
			RenderCall rc = render_calls_alpha[i];
			renderNode(rc.model, rc.mesh, rc.material, camera);
		}
		//render entities
		for (int i = 0; i < render_calls.size(); ++i)
		{
			RenderCall rc = render_calls[i];
			renderNode(rc.model, rc.mesh, rc.material, camera);
		}

	gbuffer_fbo->unbind();

	if (show_gbuffers)
	{
		//albedo
		glViewport(0, size.y / 2, size.x / 2, size.y / 2);
		gbuffer_fbo->color_textures[0]->toViewport();
		//normal
		glViewport(size.x / 2, size.y / 2, size.x / 2, size.y / 2);
		gbuffer_fbo->color_textures[1]->toViewport();
		glViewport(0, 0, size.x / 2, size.y / 2);
		//emissive
		gbuffer_fbo->color_textures[2]->toViewport();
		glViewport(size.x / 2, 0, size.x / 2, size.y / 2);
		//depth
		shader = GFX::Shader::getDefaultShader("linear_depth");
		shader->enable();
		shader->setUniform("u_camera_nearfar", vec2(camera->near_plane, camera->far_plane));
		gbuffer_fbo->depth_texture->toViewport(shader);
		glViewport(0, 0, size.x, size.y);
	}
	else
	{
		illumination_fbo->bind();

			//to clear the scene
			glDisable(GL_DEPTH_TEST);
			glDisable(GL_BLEND);
			glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			if (skybox_cubemap) renderSkybox(skybox_cubemap);

			glDisable(GL_BLEND);
			glDisable(GL_DEPTH_TEST);

			shader = GFX::Shader::Get("deferred_global");
			
			shader->enable();
			shader->setTexture("u_albedo_texture", gbuffer_fbo->color_textures[0], 0);
			shader->setTexture("u_normal_texture", gbuffer_fbo->color_textures[1], 1);
			shader->setTexture("u_emissive_texture", gbuffer_fbo->color_textures[2], 2);
			shader->setTexture("u_depth_texture", gbuffer_fbo->depth_texture, 3);
			shader->setUniform("u_ambient_light", scene->ambient_light);
			
			quad->render(GL_TRIANGLES);

			for (int i = 0; i < lights.size(); i++)
			{
				LightEntity* light = lights[i];

				//if (light->light_type == eLightType::SPOT || light->light_type == eLightType::POINT)
				//	shader = GFX::Shader::Get("deferred_geometry");
				//else
					shader = GFX::Shader::Get("deferred_light"); 
				
				//we have commented the lines above to at least be albe to see all of the lights, even if they are rendered with quads
				
				shader->enable();
				shader->setTexture("u_albedo_texture", gbuffer_fbo->color_textures[0], 0);
				shader->setTexture("u_normal_texture", gbuffer_fbo->color_textures[1], 1);
				shader->setTexture("u_emissive_texture", gbuffer_fbo->color_textures[2], 2);
				shader->setTexture("u_depth_texture", gbuffer_fbo->depth_texture, 3);

				shader->setUniform("u_iRes", vec2(1.0 / size.x, 1.0 / size.y));
				shader->setUniform("u_ivp", camera->inverse_viewprojection_matrix);

				lightToShader(light, shader);

				glDisable(GL_DEPTH_TEST);
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE);

				quad->render(GL_TRIANGLES);
			}

			glDisable(GL_BLEND);

			if (global_position)
			{
				shader = GFX::Shader::Get("deferred_world_color");
				shader->enable();
				shader->setTexture("u_depth_texture", gbuffer_fbo->depth_texture, 3);
				shader->setUniform("u_iRes", vec2(1.0 / size.x, 1.0 / size.y));
				shader->setMatrix44("u_ivp", camera->inverse_viewprojection_matrix);
				quad->render(GL_TRIANGLES);
			}

			//call for alpha objects
	
		illumination_fbo->unbind();

		illumination_fbo->color_textures[0]->toViewport();

		//apply tone mapper	
		if (!global_position)
		{
			shader = GFX::Shader::Get("tonemapper");
			shader->enable();
			shader->setUniform("u_scale", tonemapper_scale);
			shader->setUniform("u_average_lum", average_lum);
			shader->setUniform("u_lumwhite2", lum_white2);
			shader->setUniform("u_igamma", 1.0f / gamma);

			illumination_fbo->color_textures[0]->toViewport(shader);
		}

		ssao_fbo->bind();

			glDisable(GL_DEPTH_TEST);
			glDisable(GL_BLEND);

			shader = GFX::Shader::Get("ssao");

			shader->enable();
			shader->setTexture("u_normal_texture", gbuffer_fbo->color_textures[1], 1);
			shader->setTexture("u_depth_texture", gbuffer_fbo->depth_texture, 2);
			shader->setMatrix44("u_ivp", camera->inverse_viewprojection_matrix);
			shader->setUniform("u_iRes", vec2(1.0 / size.x, 1.0 / size.y));

			shader->setUniform3Array("u_points", (float*)(&ssao_points[0]), 64);
			shader->setUniform("u_radius", ssao_radius);

			shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
			shader->setUniform("u_camera_pos", camera->eye);
			shader->setUniform("u_camera_front", camera->front);

			quad->render(GL_TRIANGLES);

		ssao_fbo->unbind();

		if (show_ssao) ssao_fbo->color_textures[0]->toViewport();

	}
}

void Renderer::renderForward(SCN::Scene* scene, Camera* camera)
{
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);

	//set the camera as default (used by some functions in the framework)
	camera->enable();

	//set the clear color (the background color)
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);

	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	GFX::checkGLErrors();

	//render skybox
	if (skybox_cubemap && shader_mode != eShaderMode::FLAT)	renderSkybox(skybox_cubemap);

	//render entities (first opaques)
	for (int i = 0; i < render_calls.size(); ++i)
	{
		RenderCall rc = render_calls[i];
		renderNode(rc.model, rc.mesh, rc.material, camera);
	}

	//render entities
	for (int i = 0; i < render_calls_alpha.size(); ++i)
	{
		RenderCall rc = render_calls_alpha[i];
		renderNode(rc.model, rc.mesh, rc.material, camera);
	}
}

void Renderer::renderSkybox(GFX::Texture* cubemap)
{
	Camera* camera = Camera::current;

	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	if (render_wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	GFX::Shader* shader = GFX::Shader::Get("skybox");
	if (!shader)
		return;
	shader->enable();

	Matrix44 m;
	m.setTranslation(camera->eye.x, camera->eye.y, camera->eye.z);
	m.scale(10, 10, 10);
	shader->setUniform("u_model", m);
	cameraToShader(camera, shader);
	shader->setUniform("u_texture", cubemap, 0);
	sphere.render(GL_TRIANGLES);
	shader->disable();
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glEnable(GL_DEPTH_TEST);
}

void Renderer::orderRender(SCN::Node* node, Camera* camera)
{
	if (!node->visible)
		return;

	//compute global matrix
	Matrix44 node_model = node->getGlobalMatrix(true);

	//does this node have a mesh? then we must render it
	if (node->mesh && node->material)
	{
		//compute the bounding box of the object in world space (by using the mesh bounding box transformed to world space)
		BoundingBox world_bounding = transformBoundingBox(node_model, node->mesh->box);

		//if bounding box is inside the camera frustum then the object is probably visible
		if (camera->testBoxInFrustum(world_bounding.center, world_bounding.halfsize))
		{
			Vector3f node_pos = node_model.getTranslation();
			RenderCall rc;
			rc.mesh = node->mesh;
			rc.material = node->material;
			rc.model = node_model;
			rc.camera_distance = camera->eye.distance(node_pos);

			//material to the appropriate render call if it has alpha or not
			if (rc.material->alpha_mode == eAlphaMode::NO_ALPHA) render_calls.push_back(rc);
			else render_calls_alpha.push_back(rc);
		}
	}

	//iterate recursively with children
	for (int i = 0; i < node->children.size(); ++i)
		orderRender(node->children[i], camera);
}




//renders a node of the prefab and its children
void Renderer::renderNode(Matrix44 model, GFX::Mesh* mesh, SCN::Material* material, Camera* camera)
{
	//does this node have a mesh? then we must render it
	if (mesh && material)
	{
		//compute the bounding box of the object in world space (by using the mesh bounding box transformed to world space)
		BoundingBox world_bounding = transformBoundingBox(model, mesh->box);

		//if bounding box is inside the camera frustum then the object is probably visible
		if (camera->testBoxInFrustum(world_bounding.center, world_bounding.halfsize))
		{
			//switch between render modes
			if (render_boundaries)
				mesh->renderBounding(model, true);
			switch (render_mode)
			{
			case eRenderMode::TEXTURED:
			{
				if (shader_mode == eShaderMode::FLAT) renderMeshWithMaterialFlat(model, mesh, material);
				else renderMeshWithMaterial(model, mesh, material); 
				break;
			}
			case eRenderMode::LIGHTS:
			{
				if (shadowmap_on) renderMeshWithMaterialFlat(model, mesh, material); 				
				else renderMeshWithMaterialLight(model, mesh, material);	
				break;
			}
			case eRenderMode::DEFERRED: renderMeshWithMaterialGBuffers(model, mesh, material); break;
			}
		}
	}
}

//renders a mesh given its transform and material texture
void Renderer::renderMeshWithMaterial(Matrix44 model, GFX::Mesh* mesh, SCN::Material* material)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material)	return;
	assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	GFX::Shader* shader = NULL;
	Camera* camera = Camera::current;
	GFX::Texture* white = GFX::Texture::getWhiteTexture();

	GFX::Texture* albedo_texture = material->textures[SCN::eTextureChannel::ALBEDO].texture;
	GFX::Texture* emissive_texture = material->textures[SCN::eTextureChannel::EMISSIVE].texture;
	GFX::Texture* metallic_texture = material->textures[SCN::eTextureChannel::METALLIC_ROUGHNESS].texture;

	if (albedo_texture == NULL) albedo_texture = white; //a 1x1 white texture

	//select the blending
	if (material->alpha_mode == SCN::eAlphaMode::BLEND)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else glDisable(GL_BLEND);

	//select if render both sides of the triangles
	if (material->two_sided)	glDisable(GL_CULL_FACE);
	else glEnable(GL_CULL_FACE);
	assert(glGetError() == GL_NO_ERROR);

	glEnable(GL_DEPTH_TEST);

	shader->disable();

	//chose a shader
	shader = GFX::Shader::Get("texture"); 
	
	assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader) return;
	shader->enable();

	//upload uniforms
	shader->setUniform("u_model", model);
	cameraToShader(camera, shader);

	float t = getTime();
	shader->setUniform("u_time", t);

	shader->setUniform("u_albedo_factor", material->color);
	shader->setUniform("u_emissive_factor", material->emissive_factor);
	shader->setUniform("u_metallictexture", metallic_texture ? metallic_texture : white, 2);
	shader->setUniform("u_albedo_texture", albedo_texture ? albedo_texture : white, 0);
	shader->setUniform("u_emissive_texture", emissive_texture ? emissive_texture : white, 1);

	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == SCN::eAlphaMode::MASK ? material->alpha_cutoff : 0.001f);

	if (render_wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	//do the draw call that renders the mesh into the screen
	mesh->render(GL_TRIANGLES);

	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

//renders a mesh given its transform flat texture
void Renderer::renderMeshWithMaterialFlat(Matrix44 model, GFX::Mesh* mesh, SCN::Material* material)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material)	return;
	assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	GFX::Shader* shader = NULL;
	Camera* camera = Camera::current;

	//select if render both sides of the triangles
	if (material->two_sided) glDisable(GL_CULL_FACE);
	else glEnable(GL_CULL_FACE);
	assert(glGetError() == GL_NO_ERROR);

	glEnable(GL_DEPTH_TEST);

	shader = GFX::Shader::Get("flat");

	assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	//upload uniforms
	shader->setUniform("u_model", model);
	cameraToShader(camera, shader);
	if (render_wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	//do the draw call that renders the mesh into the screen
	mesh->render(GL_TRIANGLES);

	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

//renders a mesh given its transform and material, lights, shadows, normal, metal
void Renderer::renderMeshWithMaterialLight(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material) return;
	assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	GFX::Shader* shader = NULL;
	Camera* camera = Camera::current;
	GFX::Texture* white = GFX::Texture::getWhiteTexture();

	GFX::Texture* albedo_texture = material->textures[SCN::eTextureChannel::ALBEDO].texture;
	GFX::Texture* emissive_texture = material->textures[SCN::eTextureChannel::EMISSIVE].texture;
	GFX::Texture* normal_texture = material->textures[SCN::eTextureChannel::NORMALMAP].texture;
	GFX::Texture* metallic_texture = material->textures[SCN::eTextureChannel::METALLIC_ROUGHNESS].texture;
	// occlusion (.r) roughness (.g) metalness (.b)

	if (albedo_texture == NULL) albedo_texture = white; //a 1x1 white texture

	//select the blending
	if (material->alpha_mode == SCN::eAlphaMode::BLEND)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else glDisable(GL_BLEND);

	//select if render both sides of the triangles
	if (material->two_sided) glDisable(GL_CULL_FACE);
	else glEnable(GL_CULL_FACE);
	assert(glGetError() == GL_NO_ERROR);

	glEnable(GL_DEPTH_TEST);

	//chose a shader
	switch (shader_mode)
	{
	case eShaderMode::MULTIPASS: shader = GFX::Shader::Get("light"); break;
	case eShaderMode::PBR: shader = GFX::Shader::Get("pbr"); break;
	}

	assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader) return;
	shader->enable();

	//upload uniforms
	shader->setUniform("u_model", model);
	cameraToShader(camera, shader);
	float t = getTime();
	shader->setUniform("u_time", t);

	shader->setUniform("u_albedo_factor", material->color);
	shader->setUniform("u_emissive_factor", material->emissive_factor);
	shader->setUniform("u_metallic_factor", material->metallic_factor);
	shader->setUniform("u_roughness_factor", material->roughness_factor);
	shader->setUniform("u_albedo_texture", albedo_texture ? albedo_texture : white, 0);
	shader->setUniform("u_emissive_texture", emissive_texture ? emissive_texture : white, 1);
	shader->setUniform("u_normal_texture", normal_texture ? normal_texture : white, 2);
	shader->setUniform("u_metallic_texture", metallic_texture ? metallic_texture : white, 3);
	shader->setUniform("u_ambient_light", scene->ambient_light);

	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == SCN::eAlphaMode::MASK ? material->alpha_cutoff : 0.001f);

	if (render_wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	glDepthFunc(GL_LEQUAL); //draw pixels if depth is less or equal to camera

	if (lights.size() == 0)
	{
		shader->setUniform("u_light_info", 0);
		mesh->render(GL_TRIANGLES);
	}
	else
	{
		for (int i = 0; i < lights.size(); i++)
		{
			LightEntity* light = lights[i];	
			BoundingBox bb = transformBoundingBox(model, mesh->box);

			if (light->light_type != eLightType::DIRECTIONAL && !BoundingBoxSphereOverlap(bb, light->root.model.getTranslation(), light->max_distance))	continue;
			
			lightToShader(light, shader);

			mesh->render(GL_TRIANGLES);

			glEnable(GL_BLEND);
			glBlendFunc(GL_ONE, GL_ONE);

			shader->setUniform("u_emissive_factor", vec3(0.0));
			shader->setUniform("u_metallic_factor", vec3(0.0));
			shader->setUniform("u_roughness_factor", vec3(0.0));
			shader->setUniform("u_ambient_light", vec3(0.0));
			
		}
	}

	//do the draw call that renders the mesh into the screen	
	mesh->render(GL_TRIANGLES);

	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glDepthFunc(GL_LESS);
}

//renders a mesh given its transform and material with gbffers
void Renderer::renderMeshWithMaterialGBuffers(Matrix44 model, GFX::Mesh* mesh, SCN::Material* material)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material) return;
	assert(glGetError() == GL_NO_ERROR);

	if (material->alpha_mode == eAlphaMode::BLEND) return;

	//define locals to simplify coding
	GFX::Shader* shader = NULL;
	Camera* camera = Camera::current;
	GFX::Texture* white = GFX::Texture::getWhiteTexture();

	GFX::Texture* albedo_texture = material->textures[SCN::eTextureChannel::ALBEDO].texture;
	GFX::Texture* emissive_texture = material->textures[SCN::eTextureChannel::EMISSIVE].texture;
	GFX::Texture* normal_texture = material->textures[SCN::eTextureChannel::NORMALMAP].texture;
	GFX::Texture* metallic_texture = material->textures[SCN::eTextureChannel::METALLIC_ROUGHNESS].texture;

	if (albedo_texture == NULL) albedo_texture = white; //a 1x1 white texture

	glDisable(GL_BLEND);

	//select if render both sides of the triangles
	if (material->two_sided) glDisable(GL_CULL_FACE);
	else glEnable(GL_CULL_FACE);
	assert(glGetError() == GL_NO_ERROR);

	glEnable(GL_DEPTH_TEST);

	//chose a shader
	shader = GFX::Shader::Get("gbuffers");

	assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader) return;
	shader->enable();

	//upload uniforms
	shader->setUniform("u_model", model);
	cameraToShader(camera, shader);
	float t = getTime();
	shader->setUniform("u_time", t);

	shader->setUniform("u_albedo_factor", material->color);
	shader->setUniform("u_emissive_factor", material->emissive_factor);
	shader->setUniform("u_metallic_factor", material->metallic_factor);
	shader->setUniform("u_roughness_factor", material->roughness_factor);
	shader->setUniform("u_albedo_texture", albedo_texture ? albedo_texture : white, 0);
	shader->setUniform("u_emissive_texture", emissive_texture ? emissive_texture : white, 1);
	shader->setUniform("u_normal_texture", normal_texture ? normal_texture : white, 2);
	shader->setUniform("u_metallic_texture", metallic_texture ? metallic_texture : white, 3);

	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == SCN::eAlphaMode::MASK ? material->alpha_cutoff : 0.001f);
	shader->setUniform("u_ambient_light", scene->ambient_light);

	if (render_wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	//do the draw call that renders the mesh into the screen
	mesh->render(GL_TRIANGLES);

	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}



void SCN::Renderer::cameraToShader(Camera* camera, GFX::Shader* shader)
{
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
}

void SCN::Renderer::lightToShader(LightEntity* light, GFX::Shader* shader)
{
	shader->setUniform("u_light_position", light->root.model.getTranslation()); //for point and spot
	shader->setUniform("u_light_color", light->color * light->intensity);
	shader->setUniform("u_light_info", vec4((int)light->light_type, light->near_distance, light->max_distance, 0)); //0 as a place holder for another porperty

	if (light->light_type == eLightType::SPOT || light->light_type == eLightType::DIRECTIONAL)
		shader->setUniform("u_light_front", light->root.model.rotateVector(vec3(0, 0, 1)));

	if (light->light_type == eLightType::SPOT)
		shader->setUniform("u_light_cone", vec2(cos(light->cone_info.x * DEG2RAD), cos(light->cone_info.y * DEG2RAD))); //cone for the spot light

	shader->setUniform("u_shadow_param", vec2(light->shadowmap ? 1 : 0, light->shadow_bias));
	if (light->shadowmap && light->cast_shadows)
	{
		shader->setUniform("u_shadowmap", light->shadowmap, 8); //use one of the last slots (16 max)
		shader->setUniform("u_shadow_viewproj", light->shadow_viewproj);
	}
}

#ifndef SKIP_IMGUI

void Renderer::showUI()
{
	ImGui::Checkbox("Wireframe", &render_wireframe);
	ImGui::Checkbox("Boundaries", &render_boundaries);

	ImGui::Combo("Render Mode", (int*)&render_mode, "TEXTURED\0LIGHTS\0DEFERRED", 3);

	if (render_mode == eRenderMode::TEXTURED)
	{
		ImGui::Combo("Shader", (int*)&shader_mode, "FLAT\0TEXTURE", 2);
	}
	else if (render_mode == eRenderMode::LIGHTS) 
	{		
		static int shader = shader_mode;
		ImGui::Combo("Shader", &shader, "MULTIPASS\0PBR", 2);

		if (shader == 0) shader_mode = eShaderMode::MULTIPASS;
		if (shader == 1) shader_mode = eShaderMode::PBR;

		ImGui::Checkbox("Show ShadowMaps", &show_shadowmaps);

	}
	else if (render_mode == eRenderMode::DEFERRED) //and choose deferred shader
	{
		ImGui::Checkbox("Show ShadowMaps", &show_shadowmaps);
		ImGui::Checkbox("Show Gbuffers", &show_gbuffers);
		ImGui::Checkbox("Show GlobalPosition", &global_position);
		ImGui::Checkbox("Show SSAO", &show_ssao);

		ImGui::SliderFloat("tonemapper_scale", &tonemapper_scale, 0, 2);
		ImGui::SliderFloat("average_lum", &average_lum, 0, 2);
		ImGui::SliderFloat("lum_white2", &lum_white2, 0, 2);
		ImGui::SliderFloat("gamma", &gamma, 0, 2);

		if (show_ssao) ImGui::SliderFloat("SSAO radius", &ssao_radius, 0, 50);
	}	
}

void Renderer::generateShadowMaps()
{
	Camera* camera = new Camera();

	GFX::startGPULabel("Shadowmaps");

	shadowmap_on = true;

	for (auto light : lights)
	{
		if (!light->cast_shadows) continue;

		if (light->light_type == eLightType::POINT) continue;

		//TODO: check if light is inside camera

		if (!light->shadowmap_fbo) //build shadowmap fbo if we dont have one
		{
			int size;
			if (light->light_type == eLightType::SPOT) size = 4096;
			if (light->light_type == eLightType::DIRECTIONAL) size = 15000;

			light->shadowmap_fbo = new GFX::FBO();
			light->shadowmap_fbo->setDepthOnly(size, size);
			light->shadowmap = light->shadowmap_fbo->depth_texture;
		}

		//create camera
		Vector3f pos = light->root.model.getTranslation();
		Vector3f front = light->root.model.rotateVector(Vector3f(0, 0, -1));
		Vector3f up = Vector3f(0, 1, 0);
		camera->lookAt(pos, pos + front, up);

		if (light->light_type == eLightType::SPOT)
		{
			camera->setPerspective(light->cone_info.y * 2, 1.0, light->near_distance, light->max_distance);
		}
		if (light->light_type == eLightType::DIRECTIONAL)
		{
			camera->setOrthographic(-1000.0, 1000.0, -1000.0, 1000.0, light->near_distance, light->max_distance);
		}

		light->shadowmap_fbo->bind(); //everything we render until the unbind will be inside this texture

		renderFrame(scene, camera);

		light->shadowmap_fbo->unbind();

		light->shadow_viewproj = camera->viewprojection_matrix;
	}

	shadowmap_on = false;

	GFX::endGPULabel();
}

void Renderer::debugShadowMaps()
{
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	int x = 310;
	for (auto light : lights)
	{
		if (!light->shadowmap) continue;

		GFX::Shader* shader = GFX::Shader::getDefaultShader("linear_depth");
		shader->enable();
		shader->setUniform("u_camera_nearfar", vec2(light->near_distance, light->max_distance));

		glViewport(x, 10, 256, 256);

		light->shadowmap->toViewport(shader);

		x += 260;
	}

	vec2 size = CORE::getWindowSize();
	glViewport(0, 0, size.x, size.y);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);

}

std::vector<vec3> generateSpherePoints(int num,	float radius, bool hemi)
{
	std::vector<vec3> points;
	points.resize(num);
	for (int i = 0; i < num; i += 1)
	{
		vec3& p = points[i];
		float u = random(1.0);
		float v = random(1.0);
		float theta = u * 2.0 * PI;
		float phi = acos(2.0 * v - 1.0);
		float r = cbrt(random(1.0) * 0.9 + 0.1) * radius;
		float sinTheta = sin(theta);
		float cosTheta = cos(theta);
		float sinPhi = sin(phi);
		float cosPhi = cos(phi);
		p.x = r * sinPhi * cosTheta;
		p.y = r * sinPhi * sinTheta;
		p.z = r * cosPhi;
		if (hemi && p.z < 0)
			p.z *= -1.0;
	}
	return points;
}


#else
void Renderer::showUI() {}
#endif