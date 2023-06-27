Andrea Tortato - andrea.tortato01@estudiant.upf.edu - NIA: 231603

Edgar Espinos - edgar.espinos01@estudiant.upf.edu - NIA: 230024

# GTR Framework
The GTR Framework is an OpenGL C++ framework used for teaching real-time graphics at Universitat Pompeu Fabra. This framework allows you to navigate the scene using the WASD, Q, and E keys, and render and manipulate elements such as meshes with specific textures and lights. You can alter the illumination of the scene through an intuitive graphical user interface (GUI).

To execute the application, open the GTR_application.exe file.

### User Interface
The framework provides a graphical user interface that allows you to select specific objects in the scene and modify their properties, such as position and color. You can also render the entire scene in different modes and adjust the parameters of the lights, including intensity, color, and shadow bias. Additionally, you can view the scene in separate buffers to distinguish between different elements and textures.
You will be able to manipulate the scene by adding reflective elements, volumetric elements, change how the light interacts with the scene, color correction, post-processing effects and much more.

### Lights
The framework supports various types of lights, including point lights, directional lights, and spotlights. Each type of light affects the scene differently, and you can adjust their parameters using the GUI. The directional and spotlights cast shadows, which can be observed by enabling the Shadow Map option.
You can decide to enable the Irradiance for a more realistic look (be sure to upload the probes firsts to calculate it) as well as the reflections also be calculating the reflection probes in advance. Also, enable planar reflections to be balke to add to the scene 100% reflective objects such as mirrors or water surfaces.
To add a more dramatic look to your scene enable the volumetric rendering option to create a fog that will cast onto your scene.

### Rendering Modes
In addition to rendering the elements of the scene, you can also choose to render their bounding boxes or view them in wireframe mode. You can also visualize the shadow maps of the lights that cast shadows. These options are available in three rendering modes:

TEXTURE mode: Renders the scene with objects and their textures.
LIGHTS mode: Similar to texture mode but also renders different types of lights, their contribution to object illumination, and shadows. This mode includes normal maps and reflections for a more realistic appearance.
DEFERRED mode: Similar to lights mode, the scene is rendered in two steps, geometry and lighting. This enables efficient rendering of complex scenes with multiple light sources and advanced lighting effects.
We have focused most of our algorithms in this rendering mode since it's the one that renders the scene in a more realistic way than the other two.

### Shader Modes
For the aforementioned rendering modes, different shaders can be applied:

In the TEXTURE rendering mode, you can choose either the TEXTURE shader, which renders the scene with textures and occlusion or the FLAT shader, primarily used for performance optimization when rendering elements such as shadows. The FLAT shader renders polygons without textures or lighting.
In the LIGHTS and DEFERRED modes, you can choose the MULTIPASS shader, which handles all lights in the scene and performs multiple rendering passes for realistic lighting effects. Alternatively, you can select the PBR (Physically Based Rendering) shader, which simulates real-world light reflection based on the reflectivity of object textures.

### Deferred rendering
As said, this rendering mode offers more tools to manipulate the scene than the others. For example, you can enable the tonemapper sliders to adjust the impact of lighting, color correct the scene and apply many kinds of filters and effects. The DEFERRED mode allows viewing the various G-buffers that compose the scene: texture, which displays textures and colors; normal, which shows the normal map; emissive, which displays emissive textures; and z-buffer, which visualizes the scene's depth. Enabling the GlobalPosition option provides a clear view of objects in the scene for better distinction. Finally, enabling SSAO (Screen Space Ambient Occlusion) allows viewing the scene with ambient occlusion, which helps understand the scene's geometry without computing textures or lights. The SSAO radius can be adjusted to blur the scene for a distant view or enhance details by reducing the radius.
Finally, we've implemented the option to add decal objects to be able to add any 2D image to the texture of an object in the scene

### Tonemapper
The tonemapper slider we offer gives you the ability to change the look of the scene to make it as aesthetic as you want. You will be able to change different parameters to correct the colors of the scene in the way you prefer as well as add different filters for a more specific look, such as sepia, noir and vignette filters, hot and cold, grain effect, change the contrast, brightness and saturation, add chromatic aberrations with lens distortions or bloom effect.

### Known issues
We have addressed several issues for the second assignment, such as correctly implementing point lights and spotlights in the DEFERRED and adding the PBR rendering mode
However, we haven't been able to successfully implement the motion blur (commented) nor the depth of field effect.
Also when saving the scene.json file after positioning certain elements, such as the decals, when reloading they rotate.
