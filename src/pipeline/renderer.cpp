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

SCN::Renderer::Renderer(const char* shader_atlas_filename)
{
	render_wireframe = false;
	render_boundaries = false;
	render_mode = eRenderMode::DEFERRED; //default
	shader_mode = eShaderMode::PBR;

	scene = nullptr;
	skybox_cubemap = nullptr;
	show_shadowmaps = false;
	show_gbuffers = false;
	shadowmap_on = false;
	show_tonemapper = false;
	show_ssao = false;
	show_global_position = false;
	show_probes = false;
	show_irradiance = false;
	show_ref_probes = false;
	show_volumetric = false;
	show_postFX = false;

	ssao_points = generateSpherePoints(64, 1, false);
	ssao_radius = 5.0;

	irr_mulitplier = 1.0;

	air_density = 0.01;

	gbuffer_fbo = nullptr;
	illumination_fbo = nullptr;
	ssao_fbo = nullptr;
	irr_fbo = nullptr;
	ref_fbo = nullptr;
	plane_ref_fbo = nullptr;
	volumetric_fbo = nullptr;
	clone_depth_buffer = nullptr;
	postFX_fbo_A = nullptr;
	postFX_fbo_B = nullptr;
	postFX_fbo_temp = nullptr;

	probes_texture = nullptr;

	brightness = 1.0;
	tonemapper_scale = 1.0;
	average_lum = 1.0;
	lum_white2 = 1.0;
	gamma = 1.0;

	if (!GFX::Shader::LoadAtlas(shader_atlas_filename))	exit(1);

	GFX::checkGLErrors();

	sphere.createSphere(1.0f);
	sphere.uploadToVRAM();
	quad = GFX::Mesh::getQuad();
	quad->uploadToVRAM();
	cube.createCube(1.0f);
	cube.uploadToVRAM();

	irradiance_cache_info.num_probes = 0;
}

void SCN::Renderer::setupScene(Camera* camera)
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
	decals.clear();

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
		//decal entity
		else if (ent->getType() == eEntityType::DECAL)
		{
			DecalEntity* decal = (SCN::DecalEntity*)ent;
			decals.push_back(decal);
		}
	}
	std::sort(render_calls.begin(), render_calls.end(), [](const RenderCall a, const RenderCall b)
		{ return a.camera_distance > b.camera_distance; });

}

void SCN::Renderer::renderScene(SCN::Scene* scene, Camera* camera)
{
	this->scene = scene;

	setupScene(camera);

	if (shader_mode != eShaderMode::FLAT) generateShadowMaps();

	renderFrame(scene, camera);

	if (show_shadowmaps) debugShadowMaps();
}

void SCN::Renderer::renderFrame(SCN::Scene* scene, Camera* camera)
{
	static Camera simmetric_camera = *camera;

	if (render_mode == eRenderMode::DEFERRED)
		renderDeferred(scene, camera);
	else
	{
		renderForward(scene, camera, render_mode);
		showProbes(); //if outside it conflicts with deferred probes
	}

	//renderPlanarReflection(scene, &simmetric_camera);
}

void SCN::Renderer::renderDeferred(SCN::Scene* scene, Camera* camera)
{
	vec2 size = CORE::getWindowSize();
	GFX::Shader* shader = nullptr;

	//generate FBOs
	if (!gbuffer_fbo)
	{
		gbuffer_fbo = new GFX::FBO();
		gbuffer_fbo->create(size.x, size.y, 3, GL_RGBA, GL_UNSIGNED_BYTE, true);

		clone_depth_buffer = new GFX::Texture(size.x, size.y, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT);
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
	if (!volumetric_fbo)
	{
		volumetric_fbo = new GFX::FBO();
		volumetric_fbo->create(size.x, size.y, 3, GL_RGBA, GL_UNSIGNED_BYTE, false); //GL_LUMINANCE?
	}

	//render inside the fbo all that is in the bind 
	gbuffer_fbo->bind();
	{
		//gbuffer_fbo->enableBuffers(true, false, false, false);
		glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		//gbuffer_fbo->enableAllBuffers();

		camera->enable();
		renderObjects(camera, render_mode);
	}
	gbuffer_fbo->unbind();

	if(decals.size())
	{		
		gbuffer_fbo->depth_texture->copyTo(clone_depth_buffer);

		glEnable(GL_DEPTH_TEST);
		glDepthMask(false);
		glDepthFunc(GL_GREATER);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glFrontFace(GL_CW);
		glEnable(GL_CULL_FACE);

		gbuffer_fbo->bind();
		{
			camera->enable();
			GFX::Shader* shader = GFX::Shader::Get("decals");
			shader->enable();
			shader->setTexture("u_depth_texture", clone_depth_buffer, 4);
			shader->setUniform("u_iRes", vec2(1.0 / gbuffer_fbo->color_textures[0]->width, 1.0 / gbuffer_fbo->color_textures[0]->height));
			shader->setMatrix44("u_ivp", camera->inverse_viewprojection_matrix);
			cameraToShader(camera, shader);
	
			for (auto decal : decals)
			{
				if (!decal->filename.size()) continue;

				mat4 imodel = decal->root.model;
				imodel.inverse();
				GFX::Texture* decal_texture = GFX::Texture::Get((std::string("data/") + decal->filename).c_str());
				shader->setTexture("u_color_texture", decal_texture, 5);
				shader->setUniform("u_model", decal->root.model);
				shader->setUniform("u_imodel", imodel);
				cube.render(GL_TRIANGLES);
			}
		}
		glDepthMask(true);
		glFrontFace(GL_CCW);
		glDepthFunc(GL_LESS);
		gbuffer_fbo->unbind();
	}

	illumination_fbo->bind();
	{
		camera->enable();

		//to clear the scene
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);

		glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		if (skybox_cubemap) renderSkybox(skybox_cubemap, scene->skybox_intensity);

		shader = GFX::Shader::Get("deferred_global");

		shader->enable();
		shader->setTexture("u_albedo_texture", gbuffer_fbo->color_textures[0], 0);
		shader->setTexture("u_normal_texture", gbuffer_fbo->color_textures[1], 1);
		shader->setTexture("u_emissive_texture", gbuffer_fbo->color_textures[2], 2);
		shader->setTexture("u_depth_texture", gbuffer_fbo->depth_texture, 3);
		shader->setUniform("u_ambient_light", show_irradiance ? 0.0 : scene->ambient_light);

		quad->render(GL_TRIANGLES);

		//DIRECTIONAL LIGHTS
		//chose a shader
		switch (shader_mode)
		{
		case eShaderMode::MULTIPASS: shader = GFX::Shader::Get("deferred_light"); break;
		case eShaderMode::PBR: shader = GFX::Shader::Get("deferred_pbr"); break;
		}

		shader->enable();

		shader->setTexture("u_albedo_texture", gbuffer_fbo->color_textures[0], 0);
		shader->setTexture("u_normal_texture", gbuffer_fbo->color_textures[1], 1);
		shader->setTexture("u_emissive_texture", gbuffer_fbo->color_textures[2], 2);
		shader->setTexture("u_depth_texture", gbuffer_fbo->depth_texture, 3);
		shader->setUniform("u_iRes", vec2(1.0 / illumination_fbo->color_textures[0]->width, 1.0 / illumination_fbo->color_textures[0]->height));
		shader->setMatrix44("u_ivp", camera->inverse_viewprojection_matrix);
		cameraToShader(camera, shader);

		glDisable(GL_DEPTH_TEST);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);

		for (auto light : lights)
		{
			if (light->light_type == eLightType::DIRECTIONAL)
			{
				lightToShader(light, shader);
				quad->render(GL_TRIANGLES);
			}
		}

		glDisable(GL_BLEND);
		glEnable(GL_DEPTH_TEST);

		//OTHER LIGHTS
		switch (shader_mode)
		{
		case eShaderMode::MULTIPASS: shader = GFX::Shader::Get("deferred_geometry"); glDisable(GL_DEPTH_TEST); break;
		case eShaderMode::PBR: shader = GFX::Shader::Get("deferred_geometry_pbr"); glEnable(GL_DEPTH_TEST); break;
		}
		
		shader->enable();
		shader->setTexture("u_albedo_texture", gbuffer_fbo->color_textures[0], 0);
		shader->setTexture("u_normal_texture", gbuffer_fbo->color_textures[1], 1);
		shader->setTexture("u_emissive_texture", gbuffer_fbo->color_textures[2], 2);
		shader->setTexture("u_depth_texture", gbuffer_fbo->depth_texture, 3);
		shader->setUniform("u_iRes", vec2(1.0 / size.x, 1.0 / size.y));
		shader->setMatrix44("u_ivp", camera->inverse_viewprojection_matrix);
		cameraToShader(camera, shader);
		
		glDepthFunc(GL_GREATER);
		glEnable(GL_BLEND);
		glFrontFace(GL_CW);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		glDisable(GL_CULL_FACE);
		glDepthMask(false);
		
		for (auto light : lights)
		{
			Matrix44 model;
		
			if (light->light_type != eLightType::DIRECTIONAL)
			{
				lightToShader(light, shader);
		
				vec3 center = light->root.model.getTranslation();
				float radius = light->max_distance;
				model.setTranslation(center.x, center.y, center.z);
				model.scale(radius, radius, radius);
		
				shader->setUniform("u_model", model);
		
				sphere.render(GL_TRIANGLES);
			}
		}
		
		glFrontFace(GL_CCW);
		glDisable(GL_BLEND);
		glDepthFunc(GL_LESS);
		glDisable(GL_DEPTH_TEST);
		glDepthMask(true);
		
		shader->disable();

		if (show_irradiance) applyIrradiance();

		//reflection and illumination probes
		showProbes();
	}
	illumination_fbo->unbind();
	
	ssao_fbo->bind();
	{
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);

		shader = GFX::Shader::Get("ssao");

		shader->enable();
		shader->setTexture("u_normal_texture", gbuffer_fbo->color_textures[1], 1);
		shader->setTexture("u_depth_texture", gbuffer_fbo->depth_texture, 2);
		shader->setMatrix44("u_ivp", camera->inverse_viewprojection_matrix);
		shader->setUniform("u_iRes", vec2(1.0 / ssao_fbo->color_textures[0]->width, 1.0 / ssao_fbo->color_textures[0]->height));

		shader->setUniform3Array("u_points", (float*)(&ssao_points[0]), 64);
		shader->setUniform("u_radius", ssao_radius);

		shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
		shader->setUniform("u_camera_pos", camera->eye);
		shader->setUniform("u_camera_front", camera->front);

		quad->render(GL_TRIANGLES);
	}
	ssao_fbo->unbind();

	volumetric_fbo->bind();
	{
		shader = GFX::Shader::Get("volumetric");

		shader->enable();
		shader->setTexture("u_depth_texture", gbuffer_fbo->depth_texture, 2);
		shader->setMatrix44("u_ivp", camera->inverse_viewprojection_matrix);
		shader->setUniform("u_iRes", vec2(1.0 / volumetric_fbo->color_textures[0]->width, 1.0 / volumetric_fbo->color_textures[0]->height));
		shader->setUniform("u_camera_position", camera->eye);
		shader->setUniform("u_air_density", air_density); //place air_density in scene (scene->air_density)
		shader->setUniform("u_ambient_light", scene->ambient_light);
		shader->setUniform("u_time", getTime() * 0.001f);
		shader->setUniform("u_rand", random());

		for (auto light : lights)
		{
			if (light->light_type != eLightType::POINT)
			{
				lightToShader(light, shader);
				quad->render(GL_TRIANGLES);	
				glEnable(GL_BLEND);
			}
		}
		glDisable(GL_BLEND);
	}
	volumetric_fbo->unbind();

	if (show_gbuffers)
	{
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);

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
	else illumination_fbo->color_textures[0]->toViewport();

	if (show_global_position)
	{	
		shader = GFX::Shader::Get("deferred_world_color");
		shader->enable();
		shader->setTexture("u_depth_texture", gbuffer_fbo->depth_texture, 3);
		shader->setUniform("u_iRes", vec2(1.0 / gbuffer_fbo->color_textures[0]->width, 1.0 / gbuffer_fbo->color_textures[0]->height));
		shader->setUniform("u_ivp", camera->inverse_viewprojection_matrix);
		quad->render(GL_TRIANGLES);
	}
	if (show_ssao) ssao_fbo->color_textures[0]->toViewport();	
	if (show_tonemapper)
	{
		shader = GFX::Shader::Get("tonemapper");

		shader->enable();
		shader->setUniform("u_scale", tonemapper_scale);
		shader->setUniform("u_average_lum", average_lum);
		shader->setUniform("u_lumwhite2", lum_white2);
		shader->setUniform("u_igamma", 1.0f / gamma);
		shader->setUniform("u_brightness", brightness);

		illumination_fbo->color_textures[0]->toViewport(shader);
	}
	if (show_volumetric)
		{
			glEnable(GL_BLEND);
			glDisable(GL_DEPTH_TEST);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			volumetric_fbo->color_textures[0]->toViewport();
			glDisable(GL_BLEND);
		}

	if (show_postFX)
	{
		renderPostFX(illumination_fbo->color_textures[0], gbuffer_fbo->depth_texture, camera);
	}
	
}

void SCN::Renderer::renderForward(SCN::Scene* scene, Camera* camera, eRenderMode mode)
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
	if (skybox_cubemap && shader_mode != eShaderMode::FLAT)	renderSkybox(skybox_cubemap, scene->skybox_intensity);

	renderObjects(camera, mode);

}

void SCN::Renderer::renderSkybox(GFX::Texture* cubemap, float intensity)
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
	shader->setUniform("u_skybox_intensity", intensity);
	sphere.render(GL_TRIANGLES);
	shader->disable();
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glEnable(GL_DEPTH_TEST);
}

void SCN::Renderer::orderRender(SCN::Node* node, Camera* camera)
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

void SCN::Renderer::renderObjects(Camera* camera, eRenderMode mode)
{
	//render entities (first opaques)
	for (int i = 0; i < render_calls.size(); ++i)
	{	
		RenderCall rc = render_calls[i];
		renderNode(rc.model, rc.mesh, rc.material, camera, mode);
	}
	//render entities
	for (int i = 0; i < render_calls_alpha.size(); ++i)
	{
		RenderCall rc = render_calls_alpha[i];
		renderNode(rc.model, rc.mesh, rc.material, camera, mode);
	}
}



//renders a node of the prefab and its children
void SCN::Renderer::renderNode(Matrix44 model, GFX::Mesh* mesh, SCN::Material* material, Camera* camera, eRenderMode mode)
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
			switch (mode)
			{
				case eRenderMode::TEXTURED:
				{
					if (shader_mode == eShaderMode::FLAT) renderMeshWithMaterialFlat(model, mesh, material);
					else renderMeshWithMaterial(model, mesh, material);
					break;
				}
				case eRenderMode::LIGHTS:
				{
					if (shadowmap_on || shader_mode == eShaderMode::FLAT) renderMeshWithMaterialFlat(model, mesh, material);
					else renderMeshWithMaterialLight(model, mesh, material);
					break;
				}
				case eRenderMode::DEFERRED:
				{
					if (shadowmap_on || shader_mode == eShaderMode::FLAT) renderMeshWithMaterialFlat(model, mesh, material);
					else renderMeshWithMaterialGBuffers(model, mesh, material);
					break;
				}
			}
		}
	}
}

//renders a mesh given its transform and material texture
void SCN::Renderer::renderMeshWithMaterial(Matrix44 model, GFX::Mesh* mesh, SCN::Material* material)
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
void SCN::Renderer::renderMeshWithMaterialFlat(Matrix44 model, GFX::Mesh* mesh, SCN::Material* material)
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
void SCN::Renderer::renderMeshWithMaterialLight(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material)
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
void SCN::Renderer::renderMeshWithMaterialGBuffers(Matrix44 model, GFX::Mesh* mesh, SCN::Material* material)
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


void::SCN::Renderer::captureIrradiance()
{
	//define the corners of the axis aligned grid
	//this can be done using the boundings of our scene
	vec3 start_pos(-300, 5, -400);
	vec3 end_pos(300, 150, 400);

	//define how many probes you want per dimension
	vec3 dim(10, 4, 10);

	//compute the vector from one corner to the other
	vec3 delta = (end_pos - start_pos);

	//and scale it down according to the subdivisions
	//we substract one to be sure the last probe is at end pos
	delta.x /= (dim.x - 1);
	delta.y /= (dim.y - 1);
	delta.z /= (dim.z - 1);

	probes.resize(dim.x * dim.y * dim.z);

	//lets compute the centers
	//pay attention at the order at which we add them
	for (int z = 0; z < dim.z; ++z)
		for (int y = 0; y < dim.y; ++y)
			for (int x = 0; x < dim.x; ++x)
			{
				sProbe p;
				p.local.set(x, y, z);

				//index in the linear array
				p.index = x + y * dim.x + z * dim.x * dim.y;

				//and its position
				p.pos = start_pos + delta * vec3(x, y, z);
				probes[p.index] = p;
			}

	show_probes = false;

	for (int i = 0; i < probes.size(); i++)
	{
		sProbe& p = probes[i];
		captureProbe(p);
	}

	FILE* f = fopen("irradiance_cache.bin", "wb");

	if (f == NULL) return;

	irradiance_cache_info.dims = dim;
	irradiance_cache_info.start = start_pos;
	irradiance_cache_info.end = end_pos;
	irradiance_cache_info.num_probes = probes.size();

	fwrite(&irradiance_cache_info, sizeof(irradiance_cache_info), 1, f);
	fwrite(&probes[0], sizeof(sProbe), probes.size(), f);
	fclose(f);

	uploadIrradianceCache();
}

void SCN::Renderer::loadIrradianceCache()
{
	FILE* f = fopen("irradiance_cache.bin", "rb");

	if (f == NULL) return;

	fread(&irradiance_cache_info, sizeof(irradiance_cache_info), 1, f);
	probes.resize(irradiance_cache_info.num_probes);
	fread(&probes[0], sizeof(sProbe), irradiance_cache_info.num_probes, f);
	fclose(f);

	uploadIrradianceCache();
}

void SCN::Renderer::uploadIrradianceCache()
{
	if (probes_texture) delete probes_texture;

	vec3 dim = irradiance_cache_info.dims;

	probes_texture = new GFX::Texture(9, probes.size(), GL_RGB, GL_FLOAT);

	SphericalHarmonics* sh_data = NULL;
	sh_data = new SphericalHarmonics[dim.x * dim.y * dim.z];

	for (int i = 0; i < probes.size(); ++i)	sh_data[i] = probes[i].sh;

	//now upload the data to the GPU as a texture
	probes_texture->upload(GL_RGB, GL_FLOAT, false, (uint8*)sh_data);

	//disable any texture filtering when reading
	probes_texture->bind();
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	//always free memory after allocating it!!!
	delete[] sh_data;
}

void SCN::Renderer::captureProbe(sProbe& probe) {

	FloatImage images[6]; //here we will store the six views

	Camera cam;
	Camera* global_cam = Camera::current;
	cam.setPerspective(90, 1, 0.1, global_cam->far_plane);

	if (!irr_fbo) 
	{
		irr_fbo = new GFX::FBO();
		irr_fbo->create(64, 64, 1, GL_RGB, GL_FLOAT);
	}

	for (int i = 0; i < 6; ++i) //for every cubemap face
	{
		//compute camera orientation using defined vectors
		vec3 eye = probe.pos;
		vec3 front = cubemapFaceNormals[i][2];
		vec3 center = eye + front;
		vec3 up = cubemapFaceNormals[i][1];
		cam.lookAt(eye, center, up);
		cam.enable();

		//render the scene from this point of view
		irr_fbo->bind();

			glDisable(GL_BLEND);
			glEnable(GL_DEPTH_TEST);
			glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			GFX::checkGLErrors();

			if (skybox_cubemap && shader_mode != eShaderMode::FLAT)	renderSkybox(skybox_cubemap, scene->skybox_intensity);

			renderForward(scene, &cam, eRenderMode::LIGHTS);
			
		irr_fbo->unbind();

		//read the pixels back and store in a FloatImage
		images[i].fromTexture(irr_fbo->color_textures[0]);
	}

	//compute the coefficients given the six images
	probe.sh = computeSH(images);
}

void SCN::Renderer::renderProbe(sProbe& probe) 
{
	Camera* camera = Camera::current;
	GFX::Shader* shader = GFX::Shader::Get("spherical_probe");
	shader->enable();

	Matrix44 model;
	model.setTranslation(probe.pos.x, probe.pos.y, probe.pos.z);
	model.scale(10, 10, 10);

	shader->setUniform("u_model", model);
	shader->setUniform3Array("u_coeffs", probe.sh.coeffs[0].v, 9);
	cameraToShader(camera, shader);

	sphere.render(GL_TRIANGLES);

}

void::SCN::Renderer::applyIrradiance()
{
	if (!probes_texture) return;
	Camera* camera = Camera::current;

	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND); //disabled just to see irradiance
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);

	GFX::Shader* shader = GFX::Shader::Get("irradiance");

	shader->enable();
	shader->setTexture("u_albedo_texture", gbuffer_fbo->color_textures[0], 0);
	shader->setTexture("u_normal_texture", gbuffer_fbo->color_textures[1], 1);
	shader->setTexture("u_depth_texture", gbuffer_fbo->depth_texture, 3);
	shader->setUniform("u_probes_texture", probes_texture, 4);

	shader->setUniform("u_iRes", vec2(1.0 / gbuffer_fbo->width, 1.0 / gbuffer_fbo->height));
	shader->setUniform("u_ivp", camera->inverse_viewprojection_matrix);
	cameraToShader(camera, shader);

	vec3 delta = irradiance_cache_info.end - irradiance_cache_info.start;
	delta.x /= (irradiance_cache_info.dims.x - 1);
	delta.y /= (irradiance_cache_info.dims.y - 1);
	delta.z /= (irradiance_cache_info.dims.z - 1);

	shader->setUniform("u_irr_start", irradiance_cache_info.start);
	shader->setUniform("u_irr_end", irradiance_cache_info.end);
	shader->setUniform("u_irr_dims", irradiance_cache_info.dims);
	shader->setUniform("u_irr_delta", delta);
	shader->setUniform("u_irr_normal_distance", 5.0f); 
	shader->setUniform("u_irr_multiplier", irr_mulitplier);
	shader->setUniform("u_num_probes", irradiance_cache_info.num_probes);

	quad->render(GL_TRIANGLES);
}


void SCN::Renderer::captureReflection(SCN::Scene* scene)
{
	Camera cam;
	cam.setPerspective(90, 1, 0.1, 1000);

	if (!ref_fbo)
	{
		ref_fbo = new GFX::FBO();
		//ref_fbo->create(64, 64, 1, GL_RGB, GL_FLOAT);
	}

	//define the corners of the axis aligned grid
	//this can be done using the boundings of our scene
	vec3 start_pos(-200, 20, -300);
	vec3 end_pos(200, 150, 300);

	//define how many probes you want per dimension
	vec3 dim(4, 3, 4);

	//compute the vector from one corner to the other
	vec3 delta = (end_pos - start_pos);

	//and scale it down according to the subdivisions
	//we substract one to be sure the last probe is at end pos
	delta.x /= (dim.x - 1);
	delta.y /= (dim.y - 1);
	delta.z /= (dim.z - 1);

	ref_probes.resize(dim.x * dim.y * dim.z);

	//lets compute the centers
	//pay attention at the order at which we add them
	for (int z = 0; z < dim.z; ++z)
		for (int y = 0; y < dim.y; ++y)
			for (int x = 0; x < dim.x; ++x)
			{
				sReflectionProbe ref_probe;
				ref_probe.texture = new GFX::Texture();
				ref_probe.texture->createCubemap(256, 256, nullptr, GL_RGB, GL_FLOAT); //increase size if its too pixelated

				ref_probe.pos = start_pos + delta * vec3(x, y, z);

				for (int i = 0; i < 6; i++)
				{
					vec3 eye = ref_probe.pos;
					vec3 front = cubemapFaceNormals[i][2];
					vec3 center = eye + front;
					vec3 up = cubemapFaceNormals[i][1];
					cam.lookAt(eye, center, up);
					cam.enable();

					ref_fbo->setTexture(ref_probe.texture, i);

					ref_fbo->bind();

					renderForward(scene, &cam, eRenderMode::LIGHTS);

					ref_fbo->unbind();
				}
				glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
				ref_probe.texture->generateMipmaps();
				ref_probes.push_back(ref_probe);
			}
		
}

void SCN::Renderer::rendereReflectionProbe(sReflectionProbe& ref_probe)
{
	//if it doesn't have a texture, it uses the skybox
	GFX::Texture* texture = ref_probe.texture ? ref_probe.texture : skybox_cubemap;

	Camera* camera = Camera::current;
	GFX::Shader* shader = GFX::Shader::Get("reflection_probe");
	shader->enable();

	Matrix44 model;

	model.setTranslation(ref_probe.pos.x, ref_probe.pos.y, ref_probe.pos.z);
	model.scale(10, 10, 10);

	shader->setUniform("u_model", model);
	shader->setUniform("u_texture", texture, 0);
	cameraToShader(camera, shader);

	sphere.render(GL_TRIANGLES);
}


void SCN::Renderer::renderPlanarReflection(SCN::Scene* scene, Camera* camera)
{
	vec2 size = CORE::getWindowSize();
	Camera cam;

	if (!plane_ref_fbo)
	{
		plane_ref_fbo = new::GFX::FBO();
		plane_ref_fbo->create(size.x, size.y, 1, GL_RGB, GL_FLOAT);
	}

	vec3 pos = camera->eye;
	vec3 center = camera->center;
	vec3 up = camera->up;
	pos.y *= -1.0; center.y *= -1.0; up.y *= -1.0;
	cam.lookAt(pos, center, up);

	plane_ref_fbo->bind();
		renderForward(scene, &cam, render_mode);
	plane_ref_fbo->unbind();
}


void  SCN::Renderer::renderPostFX(GFX::Texture* color_buffer, GFX::Texture* depth_buffer, Camera* camera)
{
	assert(color_buffer && depth_buffer);

	GFX::Shader* shader = nullptr;
	float width = color_buffer->width;
	float height = color_buffer->height;

	Matrix44 prev_viewprojection_matrix;

	if (!postFX_fbo_A)
	{
		postFX_fbo_A = new GFX::FBO();
		postFX_fbo_B = new GFX::FBO();
		postFX_fbo_temp = new GFX::FBO();
		postFX_fbo_A->create(width, height, 1, GL_RGB, GL_HALF_FLOAT);
		postFX_fbo_B->create(width, height, 1, GL_RGB, GL_HALF_FLOAT);
		postFX_fbo_temp->create(width, height, 1, GL_RGB, GL_HALF_FLOAT);
	}

	postFX_fbo_A->bind();
		color_buffer->toViewport();
	postFX_fbo_A->unbind();

	//MOTION BLUR
	shader = GFX::Shader::Get("motion_blur");
	shader->enable();
	shader->setUniform("u_iRes", vec2(1.0 / width, 1.0 / height));
	shader->setMatrix44("u_ivp", camera->inverse_viewprojection_matrix);
	shader->setMatrix44("u_prev_vp", prev_viewprojection_matrix);
	shader->setTexture("u_depth_texture", depth_buffer, 4);
	postFX_fbo_B->bind();
		postFX_fbo_A->color_textures[0]->toViewport(shader);
	postFX_fbo_B->unbind();

	std::swap(postFX_fbo_A, postFX_fbo_B);

	prev_viewprojection_matrix = camera->viewprojection_matrix;

	//save image
	postFX_fbo_temp->bind();
		postFX_fbo_A->color_textures[0]->toViewport(shader);
	postFX_fbo_temp->unbind();
	
	//BLOOM
	shader = GFX::Shader::Get("blur");
	shader->enable();
	shader->setUniform("u_intensity", 1.0f);
	
	int power = 1;
	for (int i = 0; i < 4; i++)
	{		
	
		shader->setUniform("u_offset", vec2(0.0f, (float)power / height));
		shader->setUniform("u_texture", postFX_fbo_A->color_textures[0], 0);
		postFX_fbo_B->bind();
			quad->render(GL_TRIANGLES);
		postFX_fbo_B->unbind();
	
		std::swap(postFX_fbo_A, postFX_fbo_B);
	
		shader->setUniform("u_offset", vec2((float)power / width, 0.0f));
		shader->setUniform("u_texture", postFX_fbo_A->color_textures[0], 0);
		postFX_fbo_B->bind();
			quad->render(GL_TRIANGLES);
		postFX_fbo_B->unbind();
	
		std::swap(postFX_fbo_A, postFX_fbo_B);
	
		power = power << 1;
	}

	postFX_fbo_A->bind();
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		postFX_fbo_temp->color_textures[0]->toViewport();
	postFX_fbo_A->unbind();
	glDisable(GL_BLEND);

	postFX_fbo_A->color_textures[0]->toViewport(shader);

}




#ifndef SKIP_IMGUI

void SCN::Renderer::showUI()
{
	ImGui::Checkbox("Wireframe", &render_wireframe);
	ImGui::Checkbox("Boundaries", &render_boundaries);

	ImGui::SliderFloat("Skybox intensity", &scene->skybox_intensity, 0, 10);

	ImGui::Checkbox("Show volumetric", &show_volumetric);
	if (show_volumetric) ImGui::DragFloat("Air density", &air_density, 0.0001, 0.0, 0.1);

	ImGui::Checkbox("Show irradiance", &show_irradiance);
	if (show_irradiance) ImGui::SliderFloat("Irradiance multiplier", &irr_mulitplier, 0, 10);

	ImGui::Checkbox("Show probes", &show_probes);
	if (ImGui::Button("Update Probes"))
	{
		captureIrradiance();
		show_probes = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Load Probes"))
	{
		loadIrradianceCache();
		show_probes = true;
	}

	ImGui::Checkbox("Show reflection probes", &show_ref_probes);
	if (ImGui::Button("Update Reflections"))
	{
		captureReflection(scene);
		show_ref_probes = true;
	}

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
		static int shader = shader_mode;
		ImGui::Combo("Shader", &shader, "MULTIPASS\0PBR", 2);

		if (shader == 0) shader_mode = eShaderMode::MULTIPASS;
		if (shader == 1) shader_mode = eShaderMode::PBR;

		ImGui::Checkbox("Show ShadowMaps", &show_shadowmaps);
		ImGui::Checkbox("Show Gbuffers", &show_gbuffers);
		ImGui::Checkbox("Show GlobalPosition", &show_global_position);
		ImGui::Checkbox("Show SSAO", &show_ssao);
		ImGui::SliderFloat("SSAO radius", &ssao_radius, 0, 50);

		ImGui::Checkbox("Show Tonemapper", &show_tonemapper);
		if (show_tonemapper)
		{
			ImGui::SliderFloat("tonemapper_scale", &tonemapper_scale, 0, 2);
			ImGui::SliderFloat("average_lum", &average_lum, 0, 2);
			ImGui::SliderFloat("lum_white2", &lum_white2, 0, 2);
			ImGui::SliderFloat("gamma", &gamma, 0, 2);
			ImGui::SliderFloat("brightness", &brightness, 0, 2);
		}

		ImGui::Checkbox("show_postFX", &show_postFX);

	}	
}

void SCN::Renderer::generateShadowMaps()
{
	GFX::startGPULabel("Shadowmaps");

	Camera* cam = Camera::current;
	Camera camera;

	shadowmap_on = true;

	for (auto light : lights)
	{

		if (!light->cast_shadows || light->light_type == eLightType::POINT) continue;

		//TODO: check if light is inside camera

		if (!light->shadowmap_fbo) //build shadowmap fbo if we dont have one
		{
			light->shadowmap_fbo = new GFX::FBO();
			light->shadowmap_fbo->setDepthOnly(4096, 4096);
			light->shadowmap = light->shadowmap_fbo->depth_texture;
		}

		//create camera
		Vector3f pos = light->root.model.getTranslation();
		Vector3f front = light->root.model.rotateVector(Vector3f(0, 0, -1));
		Vector3f up = Vector3f(0, 1, 0);
		camera.lookAt(pos, pos + front, up);

		if (light->light_type == eLightType::SPOT)
		{
			camera.setPerspective(light->cone_info.y * 2, 1.0, light->near_distance, light->max_distance);
		}
		if (light->light_type == eLightType::DIRECTIONAL)
		{
			camera.setOrthographic(-1000.0, 1000.0, -1000.0, 1000.0, light->near_distance, light->max_distance);
		}

		eShaderMode prev = shader_mode;
		shader_mode = eShaderMode::FLAT;

		light->shadowmap_fbo->bind(); //everything we render until the unbind will be inside this texture

			renderForward(scene, &camera, eRenderMode::LIGHTS);

		light->shadowmap_fbo->unbind();

		shader_mode = prev;

		light->shadow_viewproj = camera.viewprojection_matrix;
	}

	shadowmap_on = false;

	GFX::endGPULabel();
}

void SCN::Renderer::debugShadowMaps()
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
		float u = random();
		float v = random();
		float theta = u * 2.0 * PI;
		float phi = acos(2.0 * v - 1.0);
		float r = cbrt(random() * 0.9 + 0.1) * radius;
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

void  SCN::Renderer::showProbes()
{
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	if (show_probes)
	{
		for (int i = 0; i < probes.size(); i++)
			renderProbe(probes[i]);
	}
	if (show_ref_probes)
	{
		for (int i = 0; i < ref_probes.size(); i++)
		rendereReflectionProbe(ref_probes[i]);
	}

	glDisable(GL_DEPTH_TEST);
}

#else
void Renderer::showUI() {}
#endif