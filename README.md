# Vulkan Renderer

Vulkan toy renderer written in C++. Based on previous experience with Vulkan.
The aim of this project is to combine lessons learned in previous renderers into a high quality renderer.

## TODOs

- [ ] Automatic descriptor layout / set / pool management
	- [ ] Descriptor layout builder
	- [ ] Descriptor set manager w/ layout cache for reuse
	- [ ] Allocate from manager w/ automatic pool realloc and reset per frame

## Features

- [X] OBJ mesh loading
- [X] Texture mapping
	- [X] automatic mipmap generation
- [X] ImGui support
- [X] Virtual camera support
	- [X] Perspective camera
	- [X] Orthographic camera
	- [X] Camera controller
- [X] Scene loading (Assimp)
	- [ ] Camera loading
- [X] Lights
	- [X] Sun light (directional)
	- [X] Multiple lights
		- [X] Point
		- [X] Directional
	- [ ] Shadow mapped lights
- [X] Skybox
- [ ] Render pipeline
	- [X] Shadow mapping pass
	- [ ] Depth prepass
	- [X] Forward opaque pass
	- [ ] Transparency pass
- [ ] PBR materials
- [ ] Animations
	- [ ] Linear skinning
	- [ ] Blend shapes
	- [ ] DQ skinning
- [ ] Volumetric clouds (raymarched)

![A sample image that was rendered using Vulkan renderer](render_sample.png?raw=true "Render Sample")
