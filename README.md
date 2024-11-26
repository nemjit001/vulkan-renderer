# Vulkan Renderer

Vulkan toy renderer written in C++. Based on previous experience with Vulkan.
The aim of this project is to combine lessons learned in previous renderers into a high quality renderer.

Sister repository to [directx12-renderer](https://github.com/nemjit001/directx12-renderer), in which the same capabilities are implemented using DX12.

## Features

- [X] OBJ mesh loading
- [X] Texture mapping
	- [X] automatic mipmap generation
- [X] ImGui support
- [ ] Virtual camera support
	- [X] Perspective camera
	- [X] Orthographic camera
	- [ ] Camera controller
- [X] Scene loading (Assimp)
- [ ] Lights
	- [ ] Sun light (directional)
	- [ ] Multiple point lights
- [ ] Render pipeline
	- [ ] Shadow mapping pass
		- [ ] Sun only, cascaded
		- [ ] Point lights
	- [ ] Depth prepass
	- [ ] Forward opaque pass
	- [ ] Transparency pass
- [ ] PBR materials
- [ ] Animations
	- [ ] Linear skinning
	- [ ] Blend shapes
	- [ ] DQ skinning
- [ ] Volumetric clouds (raymarched)

![A sample image that was rendered using Vulkan renderer](render_sample.png?raw=true "Render Sample")
