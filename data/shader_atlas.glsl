//example of some shaders compiled
flat basic.vs flat.fs
texture basic.vs texture.fs
light basic.vs light.fs
pbr basic.vs pbr.fs
skybox basic.vs skybox.fs
depth quad.vs depth.fs
multi basic.vs multi.fs

gbuffers basic.vs gbuffers.fs
deferred_global quad.vs deferred_global.fs
deferred_light quad.vs deferred_light.fs 
deferred_geometry basic.vs deferred_geometry.fs 
deferred_world_color quad.vs deferred_world_color.fs 
tonemapper quad.vs tonemapper.fs

ssao quad.vs ssao.fs
spherical_probe basic.vs spherical_probe.fs
irradiance quad.vs irradiance.fs



\basic.vs

#version 330 core

in vec3 a_vertex;
in vec3 a_normal;
in vec2 a_coord;
in vec4 a_color;

uniform vec3 u_camera_pos;

uniform mat4 u_model;
uniform mat4 u_viewprojection;

//this will store the color for the pixel shader
out vec3 v_position;
out vec3 v_world_position;
out vec3 v_normal;
out vec2 v_uv;
out vec4 v_color;

uniform float u_time;

void main()
{	
	//calcule the normal in camera space (the NormalMatrix is like ViewMatrix but without traslation)
	v_normal = (u_model * vec4( a_normal, 0.0) ).xyz;
	
	//calcule the vertex in object space
	v_position = a_vertex;
	v_world_position = (u_model * vec4( v_position, 1.0) ).xyz;
	
	//store the color in the varying var to use it from the pixel shader
	v_color = a_color;

	//store the texture coordinates
	v_uv = a_coord;

	//calcule the position of the vertex using the matrices
	gl_Position = u_viewprojection * vec4( v_world_position, 1.0 );
}

\quad.vs

#version 330 core

in vec3 a_vertex;
in vec2 a_coord;
out vec2 v_uv;

void main()
{	
	v_uv = a_coord;
	gl_Position = vec4( a_vertex, 1.0 );
}

\flat.fs

#version 330 core

uniform vec4 u_color;

out vec4 FragColor;

void main()
{
	FragColor = u_color;
}

\texture.fs

#version 330 core

in vec3 v_position;
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_color;

uniform vec4 u_albedo_factor;
uniform vec3 u_emissive_factor;
uniform sampler2D u_albedo_texture;
uniform sampler2D u_emissive_texture;
uniform sampler2D u_metallic_texture;

uniform float u_time;
uniform float u_alpha_cutoff;

out vec4 FragColor;

void main()
{
	vec2 uv = v_uv;
	vec4 albedo = u_albedo_factor;
	albedo *= texture( u_albedo_texture, v_uv );

	vec3 emissive = u_emissive_factor * texture(u_emissive_texture, v_uv).xyz;
	float occlusion = texture(u_metallic_texture, v_uv).r;

	if(albedo.a < u_alpha_cutoff)
		discard;

	vec3 color = emissive + albedo.rgb * occlusion;

	FragColor = vec4(color, albedo.a);
}






\tonemapper.fs

#version 330 core

in vec2 v_uv;

uniform sampler2D u_albedo_texture;

uniform float u_scale; //color scale before tonemapper
uniform float u_average_lum; 
uniform float u_lumwhite2;
uniform float u_igamma; //inverse gamma

out vec4 FragColor;

void main() {
	vec4 color = texture2D( u_albedo_texture, v_uv );
	vec3 rgb = color.xyz;

	float lum = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
	float L = (u_scale / u_average_lum) * lum;
	float Ld = (L * (1.0 + L / u_lumwhite2)) / (1.0 + L);

	rgb = (rgb / lum) * Ld;
	rgb = max(rgb,vec3(0.001));
	rgb = pow( rgb, vec3( u_igamma ) );
	FragColor = vec4( rgb, color.a );
}






\gbuffers.fs

#version 330 core

in vec3 v_position;
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_color;

uniform vec4 u_albedo_factor;
uniform vec3 u_emissive_factor;
uniform float u_metallic_factor;
uniform float u_roughness_factor;

uniform sampler2D u_albedo_texture;
uniform sampler2D u_emissive_texture;
uniform sampler2D u_normal_texture;
uniform sampler2D u_metallic_texture;


uniform float u_time;
uniform float u_alpha_cutoff;

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 NormalColor;
layout(location = 2) out vec4 ExtraColor; //for now only emissive

#include "normal"

void main()
{
	vec2 uv = v_uv;
	vec4 albedo = u_albedo_factor;
	albedo *= texture( u_albedo_texture, v_uv );

	vec3 emissive = u_emissive_factor * texture(u_emissive_texture, v_uv).xyz;

	float occlusion = texture(u_metallic_texture, v_uv).r;
	float metallic = texture(u_metallic_texture, v_uv).g;
	float roughness = texture(u_metallic_texture, v_uv).b;

	vec3 normal_map = texture(u_normal_texture, v_uv).rgb;
	normal_map = normalize(normal_map * 2.0 - vec3(1.0));

	vec3 N = normalize(v_normal);
	vec3 WP = v_world_position;
	mat3 TBN = cotangent_frame(N, WP, v_uv);
	vec3 normal = normalize(TBN * normal_map);

	if(albedo.a < u_alpha_cutoff)
		discard;

	albedo.a += occlusion;
	vec4 color = albedo;

	FragColor = color;
	NormalColor = vec4(normal*0.5 + vec3(0.5), 1.0);
	ExtraColor = vec4(emissive, roughness);
}






\light.fs

#version 330 core

in vec3 v_position;
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_color;

uniform vec4 u_albedo_factor;
uniform vec3 u_emissive_factor;
uniform float u_metallic_factor;
uniform float u_roughness_factor;

uniform sampler2D u_albedo_texture;
uniform sampler2D u_emissive_texture;
uniform sampler2D u_normal_texture;
uniform sampler2D u_metallic_texture;

uniform vec3 u_camera_position;

uniform float u_time;
uniform float u_alpha_cutoff;

#include "lights"
#include "normal"
#include "pbr_equations"

out vec4 FragColor;

void main()
{
	vec4 albedo = u_albedo_factor * texture( u_albedo_texture, v_uv );
	vec3 emissive = u_emissive_factor * texture( u_emissive_texture, v_uv ).xyz; 

	if(albedo.a < u_alpha_cutoff)
		discard;

	float occlusion = texture(u_metallic_texture, v_uv).r;
	float roughness = texture(u_metallic_texture, v_uv).g * 0.1; // * u_roughness_factor;
	float metallicness = texture(u_metallic_texture, v_uv).b; // * u_metallic_factor;

	vec3 normal_map = texture(u_normal_texture, v_uv).rgb;
	normal_map = normalize(normal_map * 2.0 - 1.0);

	vec3 WP = v_world_position;
	vec3 N = normalize(v_normal);
	vec3 V = normalize(u_camera_position - WP); 
	vec3 L = u_light_front;
	vec3 P = u_light_position;

	mat3 TBN = cotangent_frame(N, WP, v_uv);
	vec3 normal = normalize(TBN * normal_map); //perturbed

	vec3 light = vec3(0.0);

	float shadow_factor = 1.0;
	if(u_shadow_param.x != 0.0 && u_light_info.x != NO_LIGHT) shadow_factor = testShadow(WP);

	light += compute_light(u_light_info, normal, u_light_color, u_light_position, u_light_front, u_light_cone, v_world_position);
	
	if (metallicness != 0.0) light += specular_phong(N, L, V, WP, roughness, metallicness, P) * u_light_color;	
	
	light *= shadow_factor;

	light += u_ambient_light * occlusion;

	vec3 color = albedo.rgb * light + emissive;
	
	FragColor = vec4(color, albedo.a);
}






\pbr.fs

#version 330 core

in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_color;

uniform vec4 u_albedo_factor;
uniform vec3 u_emissive_factor;
uniform float u_metallic_factor;
uniform float u_roughness_factor;

uniform sampler2D u_albedo_texture;
uniform sampler2D u_emissive_texture;
uniform sampler2D u_normal_texture;
uniform sampler2D u_metallic_texture;

uniform vec3 u_camera_position;

uniform float u_time;
uniform float u_alpha_cutoff;

#include "lights"
#include "normal"
#include "pbr_equations"

out vec4 FragColor;

void main()
{
	vec4 albedo = u_albedo_factor * texture( u_albedo_texture, v_uv );
	vec3 emissive = u_emissive_factor * texture( u_emissive_texture, v_uv ).xyz; 

	if(albedo.a < u_alpha_cutoff)
		discard;

	float occlusion = texture(u_metallic_texture, v_uv).r;
	float roughness = texture(u_metallic_texture, v_uv).g * 0.2;
	float metallicness = texture(u_metallic_texture, v_uv).b * 0.2;
	//roughness *= u_roughness_factor;
	//metallicness *= u_metallic_factor;

	vec3 normal_map = texture(u_normal_texture, v_uv).rgb;
	normal_map = normalize(normal_map * 2.0 - 1.0);

	vec3 WP = v_world_position;
	vec3 N = normalize(v_normal);
	vec3 V = normalize(u_camera_position - WP); 

	mat3 TBN = cotangent_frame(N, WP, v_uv);
	vec3 normal = normalize(TBN * normal_map); //perturbed
	vec3 L = u_light_front;
	vec3 H = normalize(V + L);

	vec3 light = vec3(0.0);

	float shadow_factor = 1.0;
	if(u_shadow_param.x != 0.0 && u_light_info.x != NO_LIGHT) shadow_factor = testShadow(v_world_position);

	vec3 f0 = mix( vec3(0.5), albedo.xyz, metallicness);
	vec3 diffuseColor = (1.0 - metallicness) * albedo.xyz;

	light += compute_light(u_light_info, normal, u_light_color, u_light_position, u_light_front, u_light_cone, v_world_position) * diffuseColor;
	
    if(metallicness != 0.0) light += specular_phong_pbr(normal, L, V, WP, roughness, f0, u_light_position) * u_light_color;
	
	light *= shadow_factor;

	light += u_ambient_light * occlusion;

	vec3 color = albedo.rgb * light + emissive;
	
	FragColor = vec4(color, albedo.a);
}







\deferred_light.fs

#version 330 core

in vec3 v_normal;
in vec3 v_position;

uniform vec3 u_camera_position;

uniform sampler2D u_albedo_texture;
uniform sampler2D u_emissive_texture;
uniform sampler2D u_normal_texture;
uniform sampler2D u_depth_texture;
uniform sampler2D u_metallic_texture;

#include "lights"
#include "pbr_equations"
#include "normal"

uniform mat4 u_ivp;
uniform vec2 u_iRes;

out vec4 FragColor;

void main()
{	
	vec2 uv = gl_FragCoord.xy * u_iRes.xy;

	vec4 albedo = texture(u_albedo_texture, uv);	

	float depth = texture(u_depth_texture, uv).x;
	if (depth == 1.0) discard;

	vec4 screen_pos = vec4(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
	vec4 world_pos_proj = u_ivp * screen_pos;
	vec3 world_pos = world_pos_proj.xyz / world_pos_proj.w;

	vec3 light = vec3(0.0);

	vec3 normal_map = texture(u_normal_texture, uv).rgb;
	normal_map = normalize(normal_map * 2.0 - vec3(1.0));

	float roughness = texture(u_metallic_texture, uv).g * 0.2;
	float metallicness = texture(u_metallic_texture, uv).b * 0.2;

	vec3 N = normalize(v_normal);
	vec3 V = normalize(u_camera_position - world_pos); 
	vec3 L = u_light_front;
	vec3 P = u_light_position;

	float shadow_factor = 1.0;
	if(u_shadow_param.x != 0.0) shadow_factor = testShadow(world_pos);

	light += compute_light(u_light_info, normal_map, u_light_color, u_light_position, u_light_front, u_light_cone, world_pos);
	
	if(metallicness != 0.0) light += specular_phong(N, L, V, world_pos, roughness, metallicness, P) * u_light_color;	
	
	light *= shadow_factor;

	vec3 color = albedo.rgb * light; 

	FragColor = vec4(color, albedo.a);
	gl_FragDepth = depth;
}






\deferred_geometry.fs

#version 330 core

in vec3 v_normal;
in vec3 v_position;

uniform vec3 u_camera_position;

uniform sampler2D u_albedo_texture;
uniform sampler2D u_emissive_texture;
uniform sampler2D u_normal_texture;
uniform sampler2D u_depth_texture;
uniform sampler2D u_metallic_texture;

#include "lights"
#include "pbr_equations"
#include "normal"

uniform mat4 u_ivp;
uniform vec2 u_iRes;

out vec4 FragColor;

void main()
{	
	vec2 uv = gl_FragCoord.xy * u_iRes.xy;

	vec4 albedo = texture(u_albedo_texture, uv);	

	float depth = texture(u_depth_texture, uv).x;
	if (depth == 1.0) discard;

	vec4 screen_pos = vec4(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
	vec4 world_pos_proj = u_ivp * screen_pos;
	vec3 world_pos = world_pos_proj.xyz / world_pos_proj.w;

	vec3 light = vec3(0.0);

	vec3 normal_map = texture(u_normal_texture, uv).rgb;
	normal_map = normalize(normal_map * 2.0 - vec3(1.0));

	float roughness = texture(u_metallic_texture, uv).g * 0.2;
	float metallicness = texture(u_metallic_texture, uv).b * 0.2;

	vec3 N = normalize(v_normal);
	vec3 V = normalize(u_camera_position - world_pos); 
	vec3 L = u_light_front;
	vec3 P = u_light_position;

	float shadow_factor = 1.0;
	if(u_shadow_param.x != 0.0) shadow_factor = testShadow(world_pos);

	light += compute_light(u_light_info, normal_map, u_light_color, u_light_position, u_light_front, u_light_cone, world_pos);

	if(metallicness != 0.0) light += specular_phong(N, L, V, world_pos, roughness, metallicness, P) * u_light_color;	

	light *= shadow_factor;

	vec3 color = albedo.rgb * light; 

	FragColor = vec4(color,albedo.a);
	gl_FragDepth = depth;
}






\deferred_world_color.fs

#version 330 core

uniform sampler2D u_depth_texture;

uniform mat4 u_ivp;
uniform vec2 u_iRes;

out vec4 FragColor;

void main()
{	
	vec2 uv = gl_FragCoord.xy * u_iRes.xy;

	float depth = texture(u_depth_texture, uv).r;
	if(depth == 1.0) discard;

	vec4 screen_pos = vec4(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
	vec4 world_pos_proj = u_ivp * screen_pos;
	vec3 world_pos = world_pos_proj.xyz / world_pos_proj.w;

	vec3 color = mod(abs(world_pos * 0.01), vec3(1.0)); //to show world pos as a color

	FragColor = vec4(color, 1.0);
}






\deferred_global.fs

#version 330 core

in vec2 v_uv;

uniform sampler2D u_albedo_texture;
uniform sampler2D u_emissive_texture;
uniform sampler2D u_depth_texture;
uniform sampler2D u_metallic_texture;

uniform vec3 u_ambient_light;

out vec4 FragColor;

void main()
{	

	float depth = texture(u_depth_texture, v_uv).x;
	if (depth == 1.0) discard;

	vec4 albedo = texture(u_albedo_texture, v_uv);	
	vec4 emissive = texture(u_emissive_texture, v_uv);
	float occlusion = texture(u_metallic_texture, v_uv).r;

	vec3 color = vec3(0.0);

	color.xyz += emissive.xyz + albedo.xyz * u_ambient_light * occlusion;

	FragColor = vec4(color, 1.0);
	gl_FragDepth = depth;
}







\ssao.fs

#version 330 core

in vec3 v_world_position;
in vec3 v_normal;

uniform sampler2D u_depth_texture;
uniform sampler2D u_normal_texture;

uniform mat4 u_viewprojection;
uniform mat4 u_ivp;
uniform vec2 u_iRes;
uniform vec3 u_camera_pos;

#define NUM_POINTS 64

uniform vec3 u_points[NUM_POINTS];
uniform float u_radius; 

layout(location = 0) out vec4 FragColor;

#include "normal"

void main()
{
	vec2 uv = gl_FragCoord.xy * u_iRes.xy;
		
	float depth = texture(u_depth_texture, uv).r;

	float ao = 1.0;
	vec3 final_ao = vec3(ao);

	if(depth < 1.0)	//skip skybox pixels
	{
		vec4 screen_coord = vec4(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
		vec4 world_proj = u_ivp * screen_coord;

		vec3 world_pos = world_proj.xyz / world_proj.w;

		vec3 normal_map = texture(u_normal_texture, uv).rgb;
		normal_map = normalize(normal_map * 2.0 - 1.0);

		int outside = 0;
		for( int i = 0; i < NUM_POINTS; i++)
		{
			vec3 p = world_pos + u_points[i] * u_radius;
		
			vec4 proj = u_viewprojection * vec4(p,1.0);
			proj.xy /= proj.w; 
		
			proj.z = (proj.z - 0.005) / proj.w;
			proj.xyz = proj.xyz * 0.5 + vec3(0.5); //to [0..1]
		
			float pdepth = texture( u_depth_texture, proj.xy ).x;
		
			if( pdepth > proj.z ) 
				outside++; 

		}

		ao = float(outside) / float(NUM_POINTS);

		final_ao = vec3(ao); // * normal_map
	}

	FragColor = vec4(final_ao, 1.0);
	
}








\spherical_probe.fs

#version 330 core

in vec3 v_world_position;
in vec3 v_normal;

uniform vec3 u_coeffs[9];

const float Pi = 3.141592654;
const float CosineA0 = Pi;
const float CosineA1 = (2.0 * Pi) / 3.0;
const float CosineA2 = Pi * 0.25;
struct SH9 { float c[9]; }; //to store weights
struct SH9Color { vec3 c[9]; }; //to store colors

void SHCosineLobe(in vec3 dir, out SH9 sh) //SH9
{
	// Band 0
	sh.c[0] = 0.282095 * CosineA0;
	// Band 1
	sh.c[1] = 0.488603 * dir.y * CosineA1; 
	sh.c[2] = 0.488603 * dir.z * CosineA1;
	sh.c[3] = 0.488603 * dir.x * CosineA1;
	// Band 2
	sh.c[4] = 1.092548 * dir.x * dir.y * CosineA2;
	sh.c[5] = 1.092548 * dir.y * dir.z * CosineA2;
	sh.c[6] = 0.315392 * (3.0 * dir.z * dir.z - 1.0) * CosineA2;
	sh.c[7] = 1.092548 * dir.x * dir.z * CosineA2;
	sh.c[8] = 0.546274 * (dir.x * dir.x - dir.y * dir.y) * CosineA2;
}

vec3 ComputeSHIrradiance(in vec3 normal, in SH9Color sh)
{
	// Compute the cosine lobe in SH, oriented about the normal direction
	SH9 shCosine;
	SHCosineLobe(normal, shCosine);
	// Compute the SH dot product to get irradiance
	vec3 irradiance = vec3(0.0);
	for(int i = 0; i < 9; ++i)
		irradiance += sh.c[i] * shCosine.c[i];

	return irradiance;
}

out vec4 FragColor;

void main()
{
	vec4 color = vec4(1.0);

	vec3 N = normalize(v_normal);

	SH9Color sh;
	for (int i = 0; i < 9; i++)	sh.c[i] = u_coeffs[i];

	color.xyz = max(vec3(0.0), ComputeSHIrradiance(N, sh));

	FragColor = color;
}





\irradiance.fs

#version 330 core

uniform sampler2D u_albedo_texture;
uniform sampler2D u_emissive_texture;
uniform sampler2D u_normal_texture;
uniform sampler2D u_depth_texture;
uniform sampler2D u_probes_texture;

uniform float u_irr_normal_distance;
uniform int u_num_probes;
uniform float u_irr_multiplier;

uniform mat4 u_ivp;
uniform vec2 u_iRes;
uniform vec3 u_irr_start;
uniform vec3 u_irr_end;
uniform vec3 u_irr_dims;
uniform vec3 u_irr_delta;

const float Pi = 3.141592654;
const float CosineA0 = Pi;
const float CosineA1 = (2.0 * Pi) / 3.0;
const float CosineA2 = Pi * 0.25;
struct SH9 { float c[9]; }; //to store weights
struct SH9Color { vec3 c[9]; }; //to store colors

void SHCosineLobe(in vec3 dir, out SH9 sh) //SH9
{
	// Band 0
	sh.c[0] = 0.282095 * CosineA0;
	// Band 1
	sh.c[1] = 0.488603 * dir.y * CosineA1; 
	sh.c[2] = 0.488603 * dir.z * CosineA1;
	sh.c[3] = 0.488603 * dir.x * CosineA1;
	// Band 2
	sh.c[4] = 1.092548 * dir.x * dir.y * CosineA2;
	sh.c[5] = 1.092548 * dir.y * dir.z * CosineA2;
	sh.c[6] = 0.315392 * (3.0 * dir.z * dir.z - 1.0) * CosineA2;
	sh.c[7] = 1.092548 * dir.x * dir.z * CosineA2;
	sh.c[8] = 0.546274 * (dir.x * dir.x - dir.y * dir.y) * CosineA2;
}

vec3 ComputeSHIrradiance(in vec3 normal, in SH9Color sh)
{
	// Compute the cosine lobe in SH, oriented about the normal direction
	SH9 shCosine;
	SHCosineLobe(normal, shCosine);
	// Compute the SH dot product to get irradiance
	vec3 irradiance = vec3(0.0);
	for(int i = 0; i < 9; ++i)
		irradiance += sh.c[i] * shCosine.c[i];

	return irradiance;
}

out vec4 FragColor;

void main()
{
	vec2 uv = gl_FragCoord.xy * u_iRes.xy;

	vec4 albedo = texture(u_albedo_texture, uv);	
	vec4 emissive = texture(u_emissive_texture, uv);	

	float depth = texture(u_depth_texture, uv).x;
	if (depth == 1.0) discard;

	vec4 screen_pos = vec4(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
	vec4 world_pos_proj = u_ivp * screen_pos;
	vec3 world_pos = world_pos_proj.xyz / world_pos_proj.w;

	vec3 normal_map = texture(u_normal_texture, uv).rgb;
	normal_map = normalize(normal_map * 2.0 - vec3(1.0));



	//computing nearest probe index based on world position
	vec3 irr_range = u_irr_end - u_irr_start;
	vec3 irr_local_pos = clamp( world_pos - u_irr_start + normal_map * u_irr_normal_distance, vec3(0.0), irr_range );
	
	//convert from world pos to grid pos
	vec3 irr_norm_pos = irr_local_pos / u_irr_delta;
	
	//round values as we cannot fetch between rows for now
	vec3 local_indices = round( irr_norm_pos );
	
	//compute in which row is the probe stored
	float row = local_indices.x + local_indices.y * u_irr_dims.x + local_indices.z * u_irr_dims.x * u_irr_dims.y;
	
	//find the UV.y coord of that row in the probes texture
	float row_uv = (row + 1.0) / (u_num_probes + 1.0);




	SH9Color sh;
	
	//fill the coefficients
	const float d_uvx = 1.0 / 9.0;
	for(int i = 0; i < 9; ++i)
	{
		vec2 coeffs_uv = vec2( (float(i)+0.5) * d_uvx, row_uv );
		sh.c[i] = texture( u_probes_texture, coeffs_uv).xyz;
	}
	
	vec3 irradiance = max(vec3(0.0), ComputeSHIrradiance( normal_map, sh ) * u_irr_multiplier);

	FragColor = vec4(irradiance, 1.0); //*albedo
}






\skybox.fs

#version 330 core

in vec3 v_position;
in vec3 v_world_position;

uniform float u_skybox_intensity;

uniform samplerCube u_texture;
uniform vec3 u_camera_position;
out vec4 FragColor;

void main()
{
	vec3 E = v_world_position - u_camera_position;
	vec4 color = texture( u_texture, E ) * u_skybox_intensity;
	FragColor = color;
}







\multi.fs

#version 330 core

in vec3 v_position;
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;

uniform vec4 u_color;
uniform sampler2D u_texture;
uniform float u_time;
uniform float u_alpha_cutoff;

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 NormalColor;

void main()
{
	vec2 uv = v_uv;
	vec4 color = u_color;
	color *= texture( u_texture, uv );

	if(color.a < u_alpha_cutoff)
		discard;

	vec3 N = normalize(v_normal);

	FragColor = color;
	NormalColor = vec4(N,1.0);
}







\depth.fs

#version 330 core

uniform vec2 u_camera_nearfar;
uniform sampler2D u_texture; //depth map
in vec2 v_uv;
out vec4 FragColor;

void main()
{
	float n = u_camera_nearfar.x;
	float f = u_camera_nearfar.y;
	float z = texture2D(u_texture,v_uv).x;
	if( n == 0.0 && f == 1.0 )
		FragColor = vec4(z);
	else
		FragColor = vec4( n * (z + 1.0) / (f + n - z * (f - n)) );
}







\instanced.vs

#version 330 core

in vec3 a_vertex;
in vec3 a_normal;
in vec2 a_coord;

in mat4 u_model;

uniform vec3 u_camera_pos;

uniform mat4 u_viewprojection;

//this will store the color for the pixel shader
out vec3 v_position;
out vec3 v_world_position;
out vec3 v_normal;
out vec2 v_uv;

void main()
{	
	//calcule the normal in camera space (the NormalMatrix is like ViewMatrix but without traslation)
	v_normal = (u_model * vec4( a_normal, 0.0) ).xyz;
	
	//calcule the vertex in object space
	v_position = a_vertex;
	v_world_position = (u_model * vec4( a_vertex, 1.0) ).xyz;
	
	//store the texture coordinates
	v_uv = a_coord;

	//calcule the position of the vertex using the matrices
	gl_Position = u_viewprojection * vec4( v_world_position, 1.0 );
}












\normal

mat3 cotangent_frame(vec3 N, vec3 p, vec2 uv)
{
	// get edge vectors of the pixel triangle
	vec3 dp1 = dFdx( p );
	vec3 dp2 = dFdy( p );
	vec2 duv1 = dFdx( uv );
	vec2 duv2 = dFdy( uv );
	
	// solve the linear system
	vec3 dp2perp = cross( dp2, N );
	vec3 dp1perp = cross( N, dp1 );
	vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
	vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
 
	// construct a scale-invariant frame 
	float invmax = inversesqrt( max( dot(T,T), dot(B,B) ) );
	return mat3( T * invmax, B * invmax, N );
}




\pbr_equations

#define RECIPROCAL_PI 0.3183098861837697
#define PI 3.14159265359

float D_GGX (const in float NoH, const in float linearRoughness)
{
	float a2 = linearRoughness * linearRoughness;
	float f = (NoH * NoH) * (a2 - 1.0) + 1.0;
	return a2 / (PI * f * f);
}
float F_Schlick( const in float VoH, const in float f0)
{
	float f = pow(1.0 - VoH, 5.0);
	return f0 + (1.0 - f0) * f;
}
vec3 F_Schlick( const in float VoH, const in vec3 f0)
{
	float f = pow(1.0 - VoH, 5.0);
	return f0 + (vec3(1.0) - f0) * f;
}

float GGX(float NdotV, float k)
{
	return NdotV / (NdotV * (1.0 - k) + k);
}
float G_Smith( float NdotV, float NdotL, float roughness)
{
	float k = pow(roughness + 1.0, 2.0) / 8.0;
	return GGX(NdotL, k) * GGX(NdotV, k);
}
float Fd_Burley (const in float NoV, const in float NoL, const in float LoH, const in float linearRoughness)
{
        float f90 = 0.5 + 2.0 * linearRoughness * LoH * LoH;
        float lightScatter = F_Schlick(NoL, f90);
        float viewScatter  = F_Schlick(NoV, f90);
        return lightScatter * viewScatter * RECIPROCAL_PI;
}
vec3 specularBRDF(float roughness, vec3 f0, float NoH, float NoV, float NoL, float LoH)
{
	float a = roughness * roughness;
	float D = D_GGX( NoH, a );
	vec3 F = F_Schlick( LoH, f0 );
	float G = G_Smith( NoV, NoL, roughness );
		
	vec3 spec = D * G * F;
	spec /= (4.0 * NoL * NoV + 1e-6);

	return spec;
}
float specular_phong(vec3 N, vec3 L, vec3 V, vec3 WP, float ks, float alpha, vec3 P)
{
	float specular = 0.0;

	if (int(u_light_info.x) == POINT_LIGHT || int(u_light_info.x) == SPOT_LIGHT)
	{
		L =  P - WP;
		float dist = length(L);
		L /= dist; //to normalize L
	}	
	
	//vec3 R = reflect(N, L);
	vec3 R = normalize(reflect(L, N));

	float RdotV = max(dot(-R,V), 0.0);
	specular = ks * pow(RdotV, alpha);

	return specular;
}
vec3 specular_phong_pbr(vec3 N, vec3 L, vec3 V, vec3 WP, float ks, vec3 alpha, vec3 P)
{
	vec3 specular = vec3(0.0);

	if (int(u_light_info.x) == POINT_LIGHT || int(u_light_info.x) == SPOT_LIGHT)
	{
		L =  P - WP;
		float dist = length(L);
		L /= dist; //to normalize L
	}

	vec3 H = normalize(V+L);

	float NdotH = max(dot(N, H), 0.0);
	float NdotV = max(dot(N, V), 0.0);
	float NdotL = max(dot(N, L), 0.0);
	float LdotH = max(dot(L, H), 0.0);
	
	specular = specularBRDF(ks, alpha, NdotH, NdotV, NdotL, LdotH);
	return specular;
}




\lights

//light_type
#define NO_LIGHT 0
#define POINT_LIGHT 1
#define SPOT_LIGHT 2
#define DIRECTIONAL_LIGHT 3

uniform vec4 u_light_info; //light_type, near_distance, max_distance, 0
uniform vec3 u_light_position;
uniform vec3 u_light_front;
uniform vec3 u_light_color;
uniform vec2 u_light_cone; //cos(min_angle), cos(max_angle)

uniform vec3 u_ambient_light;

uniform vec2 u_shadow_param; //(0 or 1 in there is shadowmap, bias)
uniform mat4 u_shadow_viewproj;
uniform sampler2D u_shadowmap;

float testShadow(vec3 pos)
{
	vec4 proj_pos = u_shadow_viewproj * vec4(pos,1.0);

	vec2 shadow_uv = proj_pos.xy / proj_pos.w;
	shadow_uv = shadow_uv * 0.5 + vec2(0.5);

	float real_depth = (proj_pos.z - u_shadow_param.y) / proj_pos.w;
	real_depth = real_depth * 0.5 + 0.5;

	float shadow_depth = texture( u_shadowmap, shadow_uv).x;

	if( shadow_uv.x < 0.0 || shadow_uv.x > 1.0 || shadow_uv.y < 0.0 || shadow_uv.y > 1.0 )
			return 1.0;

	if(real_depth < 0.0 || real_depth > 1.0) return 1.0;

	float shadow_factor = 1.0;

	if( shadow_depth < real_depth )	shadow_factor = 0.0;
	return shadow_factor;
}
vec3 compute_light(vec4 u_light_info, vec3 normal, vec3 u_light_color, vec3 u_light_position, vec3 u_light_front, vec2 u_light_cone, vec3 v_world_position)
{			
	vec3 light = vec3(0.0);

	if (int(u_light_info.x) == DIRECTIONAL_LIGHT)
	{
		float NdotL = dot(normal, u_light_front);
		light = max(NdotL, 0.0)  * u_light_color;		
	}
	else if (int(u_light_info.x) == POINT_LIGHT || int(u_light_info.x) == SPOT_LIGHT)
	{
		vec3 L = u_light_position - v_world_position;
		float dist = length(L);
		L /= dist; //to normalize L

		float NdotL = dot(normal, L);
		float attenuation = max(0.0, (u_light_info.z - dist) / u_light_info.z);

		if (int(u_light_info.x) == SPOT_LIGHT)
		{
			float cos_angle = dot(u_light_front, L);
			if (cos_angle < u_light_cone.y) attenuation = 0.0;
			else if (cos_angle < u_light_cone.x) attenuation *= 1.0 - (cos_angle - u_light_cone.x) / (u_light_cone.y - u_light_cone.x);
		}
		light = max(NdotL, 0.0)  * u_light_color * attenuation;
	}

	return light;
}















