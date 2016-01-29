![](https://img.shields.io/badge/release-v0.1.0-green.svg) ![](https://img.shields.io/badge/license-MIT-blue.svg)

# Fury3D

## Introduction

Fury3d is a cross-platform rendering engine written in c++11 and modern opengl.

Works on windows && osx operating systems currentlly.

Please note, this is just a simple project for study purpose.

Features: 

* Use modern opengl.

* C++11 smart pointers made memory management easier.

* Support fbx model format, you can load static models and lights directlly.

* Easy rendering pipeline management through json serialization functionality.

* Build-in light-pre pass rendering pipeling.

Plans:

* Add shadow maps.

* Add skeleton animation support.

## Compatibility

Tested compilers: 

* MSVC 2013 Community

* Apple LLVM version 7.0.2 (clang-700.1.81)

Because fbxsdk only offers MSVC builds on windows, so you must use MSVC to build the library.

Should work with any graphic card that supports opengl 3.3 +

## Example

You can setup custom rendering pipeline using json file:

~~~~~~~~~~json
{
    "textures": [
        {
            "name": "gbuffer_diffuse",
            "format": "rgba8",
            "filter": "linear",
            "wrap": "repeat",
            "width": 1280,
            "height": 720,
            "mipmap": false
        },
	], 
	"shaders": [
        {
            "name": "dirlight_shader",
            "type": "dir_light",
            "path": "Resource/Shader/SunLight.glsl"
        },
	], 
	"passes": [
        {
            "name": "pass_gbuffer",
            "camera": "camNode",
            "shaders": [
                "gbuffer_shader",
                "gbuffer_notexture_shader"
            ],
            "input": [],
            "output": [
                "depth_buffer",
                "gbuffer_normal",
                "gbuffer_depth",
                "gbuffer_diffuse"
            ],
            "index": 0,
            "blendMode": "replace",
            "compareMode": "less",
            "cullMode": "back",
            "drawMode": "renderable"
        },
	]
}
~~~~~~~~~~

A simple demo should look like this: 

~~~~~~~~~~cpp
// load scene
SceneNode::Ptr m_RootNode = m_RootNode::Create("Root");
// Use FileUtil::GetAbsPath to get absolute file path on osx.
FbxUtil::Instance()->LoadScene(FileUtil::Instance()->GetAbsPath("Path to fbx"), m_RootNode);

// setup octree
OcTree::Ptr m_OcTree = OcTree::Create(Vector4(-10000, -10000, -10000, 1), Vector4(10000, 10000, 10000, 1), 2);
m_OcTree->AddSceneNodeRecursively(m_RootNode);

// Load pipeline
PrelightPipeline::Ptr m_Pipeline = PrelightPipeline::Create("pipeline");
FileUtil::Instance()->LoadFromFile(m_Pipeline, FileUtil::Instance()->GetAbsPath("Path To Pipeline.json"));

// draw scene
m_Pipeline->Execute(m_OcTree);
~~~~~~~~~~

## Thirdparty

* [FbxSdk](http://www.autodesk.com/products/fbx/overview) - for loading fbx model

* [Rapidjson](https://github.com/miloyip/rapidjson) - for loading pipeline setups

* [Plog](https://github.com/SergiusTheBest/plog) - for logging

* [Stbimage](https://github.com/nothings/stb) - for image loading

* [Sfml](http://www.sfml-dev.org) - for example program

## One more thing

If you use sublimetext, you can try my [GLSLCompiler](https://github.com/sindney/GLSLCompiler) plugin to debug glsl code :D