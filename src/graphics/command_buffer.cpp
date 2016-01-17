//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2015 SuperTuxKart-Team
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 3
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

#include "graphics/command_buffer.hpp"
#include "graphics/central_settings.hpp"
#include "utils/cpp2011.hpp"


template<>
void InstanceFiller<InstanceDataSingleTex>::add(GLMesh *mesh, scene::ISceneNode *node, InstanceDataSingleTex &instance)
{
    fillOriginOrientationScale<InstanceDataSingleTex>(node, instance);
    instance.Texture = mesh->TextureHandles[0];
}

template<>
void InstanceFiller<InstanceDataDualTex>::add(GLMesh *mesh, scene::ISceneNode *node, InstanceDataDualTex &instance)
{
    fillOriginOrientationScale<InstanceDataDualTex>(node, instance);
    instance.Texture = mesh->TextureHandles[0];
    instance.SecondTexture = mesh->TextureHandles[1];
}

template<>
void InstanceFiller<InstanceDataThreeTex>::add(GLMesh *mesh, scene::ISceneNode *node, InstanceDataThreeTex &instance)
{
    fillOriginOrientationScale<InstanceDataThreeTex>(node, instance);
    instance.Texture = mesh->TextureHandles[0];
    instance.SecondTexture = mesh->TextureHandles[1];
    instance.ThirdTexture = mesh->TextureHandles[2];
}

template<>
void InstanceFiller<GlowInstanceData>::add(GLMesh *mesh, scene::ISceneNode *node, GlowInstanceData &instance)
{
    fillOriginOrientationScale<GlowInstanceData>(node, instance);
    STKMeshSceneNode *nd = dynamic_cast<STKMeshSceneNode*>(node);
    instance.Color = nd->getGlowColor().color;
}

template<>
void expandTexSecondPass<GrassMat>(const GLMesh &mesh,
                                   const std::vector<GLuint> &prefilled_tex)
{
    TexExpander<typename GrassMat::InstancedSecondPassShader>::template
        expandTex(mesh, GrassMat::SecondPassTextures, prefilled_tex[0],
                  prefilled_tex[1], prefilled_tex[2], prefilled_tex[3]);
}

template<>
void expandHandlesSecondPass<GrassMat>(const std::vector<uint64_t> &handles)
{
    uint64_t nulltex[10] = {};
    HandleExpander<GrassMat::InstancedSecondPassShader>::template
        expand(nulltex, GrassMat::SecondPassTextures,
               handles[0], handles[1], handles[2], handles[3]);
}


template<int N>
void CommandBuffer<N>::clearMeshes()
{
    m_instance_buffer_offset = 0;
    m_command_buffer_offset = 0;
    m_poly_count = 0;    
    
    for(int i=0;i<N;i++)
    {
        m_meshes[i].clear();
    }   
}

template<int N>
void CommandBuffer<N>::unmapBuffers()
{
    if (!CVS->supportsAsyncInstanceUpload())
    {
        glUnmapBuffer(GL_ARRAY_BUFFER);
        glUnmapBuffer(GL_DRAW_INDIRECT_BUFFER);
    }    
}

template<int N>
CommandBuffer<N>::CommandBuffer():
m_poly_count(0)
{
    glGenBuffers(1, &m_draw_indirect_cmd_id);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_draw_indirect_cmd_id);
    if (CVS->supportsAsyncInstanceUpload())
    {
        glBufferStorage(GL_DRAW_INDIRECT_BUFFER,
                        10000 * sizeof(DrawElementsIndirectCommand),
                        0, GL_MAP_PERSISTENT_BIT | GL_MAP_WRITE_BIT);
        m_draw_indirect_cmd = (DrawElementsIndirectCommand *)
            glMapBufferRange(GL_DRAW_INDIRECT_BUFFER,
                             0, 10000 * sizeof(DrawElementsIndirectCommand),
                             GL_MAP_PERSISTENT_BIT | GL_MAP_WRITE_BIT);
    }
    else
    {
        glBufferData(GL_DRAW_INDIRECT_BUFFER,
                     10000 * sizeof(DrawElementsIndirectCommand),
                     0, GL_STREAM_DRAW);
    }    
}

template<int N>
CommandBuffer<N>::~CommandBuffer()
{
    glDeleteBuffers(1, &m_draw_indirect_cmd_id);
}

SolidCommandBuffer::SolidCommandBuffer(): CommandBuffer()
{
}

void SolidCommandBuffer::fill(MeshMap *mesh_map)
{
    clearMeshes();
    
    std::vector<int> dual_tex_material_list =
        createVector<int>(Material::SHADERTYPE_SOLID,
                          Material::SHADERTYPE_ALPHA_TEST,
                          Material::SHADERTYPE_SOLID_UNLIT,
                          Material::SHADERTYPE_SPHERE_MAP,
                          Material::SHADERTYPE_VEGETATION);
                          
    fillInstanceData<InstanceDataDualTex>(mesh_map,
                                          dual_tex_material_list,
                                          InstanceTypeDualTex);
                                           
    std::vector<int> three_tex_material_list =
        createVector<int>(Material::SHADERTYPE_DETAIL_MAP,
                          Material::SHADERTYPE_NORMAL_MAP);
                          
    fillInstanceData<InstanceDataThreeTex>(mesh_map,
                                           three_tex_material_list,
                                           InstanceTypeThreeTex);
        
    
    unmapBuffers();
    
} //SolidCommandBuffer::fill


ShadowCommandBuffer::ShadowCommandBuffer(): CommandBuffer()
{
}

void ShadowCommandBuffer::fill(MeshMap *mesh_map)
{
    clearMeshes();
    
    std::vector<int> shadow_tex_material_list;
    for(int cascade=0; cascade<4; cascade++)
    {
        shadow_tex_material_list.push_back(cascade * Material::SHADERTYPE_COUNT
                                           + Material::SHADERTYPE_SOLID);
        shadow_tex_material_list.push_back(cascade * Material::SHADERTYPE_COUNT
                                           + Material::SHADERTYPE_ALPHA_TEST);
        shadow_tex_material_list.push_back(cascade * Material::SHADERTYPE_COUNT
                                           + Material::SHADERTYPE_SOLID_UNLIT);
        shadow_tex_material_list.push_back(cascade * Material::SHADERTYPE_COUNT
                                           + Material::SHADERTYPE_NORMAL_MAP);
        shadow_tex_material_list.push_back(cascade * Material::SHADERTYPE_COUNT
                                           + Material::SHADERTYPE_SPHERE_MAP);
        shadow_tex_material_list.push_back(cascade * Material::SHADERTYPE_COUNT
                                           + Material::SHADERTYPE_DETAIL_MAP);
        shadow_tex_material_list.push_back(cascade * Material::SHADERTYPE_COUNT
                                           + Material::SHADERTYPE_VEGETATION);
        shadow_tex_material_list.push_back(cascade * Material::SHADERTYPE_COUNT
                                           + Material::SHADERTYPE_SPLATTING);
    }
    
    fillInstanceData<InstanceDataSingleTex>(mesh_map, shadow_tex_material_list, InstanceTypeShadow);
    
    unmapBuffers();
    
} //ShadowCommandBuffer::fill

ReflectiveShadowMapCommandBuffer::ReflectiveShadowMapCommandBuffer()
{
}


void ReflectiveShadowMapCommandBuffer::fill(MeshMap *mesh_map)
{
    clearMeshes();
    
    std::vector<int> rsm_material_list =
        createVector<int>(Material::SHADERTYPE_SOLID,
                          Material::SHADERTYPE_ALPHA_TEST,
                          Material::SHADERTYPE_SOLID_UNLIT,
                          Material::SHADERTYPE_DETAIL_MAP,
                          Material::SHADERTYPE_NORMAL_MAP);
                          
    fillInstanceData<InstanceDataSingleTex>(mesh_map,
                                            rsm_material_list,
                                            InstanceTypeRSM);

    unmapBuffers();   
    
} //ReflectiveShadowMapCommandBuffer::fill

GlowCommandBuffer::GlowCommandBuffer()
{   
}

void GlowCommandBuffer::fill(MeshMap *mesh_map)
{
    clearMeshes();
    fillInstanceData<GlowInstanceData>(mesh_map,
                                          createVector<int>(0),
                                          InstanceTypeGlow);
    unmapBuffers();
} //GlowCommandBuffer::fill