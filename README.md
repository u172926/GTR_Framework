Andrea Tortato - andrea.tortato01@estudiant.upf.edu - NIA: 231603

Edgar Espinos - edgar.espinos01@estudiant.upf.edu - NIA: 230024

# GTR Framework
The GTR Framework is an OpenGL C++ framework used for teaching real-time graphics at Universitat Pompeu Fabra. This framework allows you to move around the scene using the WASD, Q and E keys and render and manipulate elements of a scene such as meshes with specific textures and lights, altering the illumination of the scene in an intuitive way using a graphical user interface (GUI).

To execute the application open the GTR_application.exe file.

### User Interface
The framework provides a graphical user interface that lets you select specific objects in the scene and change their state, such as position, color, or render the entire scene in different modes. You can also select prefab objects and change their position, rotation, and scale, as well as alter the different parameters of the lights, such as intensity, color, shadow bias, etc.
You are also going to be able to view the scene in seperate buffers to differentiate between the different elements and textures that compose the scene

### Lights
It also supports different types of lights, including point lights, directional lights, and spotlights. Each type of light affects the scene in a different way, and you can adjust the different parameters of each light using the GUI.
The directional and spot light cast shadows and we can observe the casted shadow by enabeling the Shadow Map box.

### Rendering Modes
Apart from just rendering the elements of the scene you can also render their bounding boxes or view them by their wireframes. You can also choose to view the shadow maps of the lights which cast shadows.
All of the above can be done choosing firm three rendering modes:
- TEXTURE mode: Renders the scene with just the objects themselves and their textures.
- LIGHTS mode: Like texture mode but also renders the different types of lights, how they contribute to the illumination of the objects in the scene and cast shadows, including normal maps and reflections which make the scene look more realistic.
- DEFFERED mode: Like lights mode but the scene is rendered in two steps, geometry and lighting, allowing for efficient rendering of complex scenes with multiple light sources and advanced lighting effects.

### Shader Modes
For the three rendering modes explained above we have different shaders we can apply:
- For the TEXTURE rendering mode we can either pass the TEXTURE shader, whcih renders the scene only with the color of the textures and occlusion, or the FLAT shader, which is mostly used to enhance the performance of our application when rendering other elements, such as shadows, in which we render the polygons of the objects in our scene without textures or lighting.
- For the LIGHTS mode we can choose the MULTIPASS shader, which passes all of the lights in the scene and perfors multiple rendering passes to obtain realistic lighting effects, or the PBR (Physically based rendering) shader which simulates how light reflects in the real world based on how reflective the texture of an objects is

### Deferred rendering
While a PBR shader is not yet implemented for the DEFERRED rendering mode we can still use it to observe how the scene is construcetd and how changing different parameters changes our scene, such as the tonemapper slider whcih we can modify to change how the lighting affects our scene.
We can for instance show the different GBuffers that compose our scene in 4 separe ones: texture, which only displays the textures and colors of our scne, normal, which shows the normal map, emissive, which showa the emissive textures of the scene, and zbuffer, with which we can visualize the depth our our scene.
By activating the GlobalPosition box we are able to clearly take a look at all of the objects in our scene to distinguish them form one another.
Lastly we can enable the SSAO to look at the scene usign ambient occlusion, whcih is helpfull for getting a sence of the geometry of the scene without computing textures or lights. We can change the radius of the SSAO (which is what determines the radius of the speheres that are comoputed in each point of the scene to obtain the ambient occlusion) to blur the scene, if we want to look at it from far away, or enhance the details (by reducing the radius) to see them more clearly

### Known issues
We were able to correct issues fpr the first assignment such as fixing the normal maps of the objects of the scene and implementing metallic, roughness and occlusion textures. Also shadow mapping issues that we had before have been solved
Said this, we have created the shader to compute point lights and spot lights in the DEFERRED rendering mode using geometry instead of quads, but we haven't been able to correclty implement them in our scene.
Also for the PBR rendering, while the look of a wet and reflective floor is very nice, it should be rough enough such that there aren't as many clear reflections as a more metallic texture like the one for the car.



