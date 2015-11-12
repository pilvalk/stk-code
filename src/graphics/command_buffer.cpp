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
    m_instance_buffer_offset = 0;
    m_command_buffer_offset = 0;
    m_poly_count = 0;    
    
    //clear meshes
    for(int i=0;i<Material::SHADERTYPE_COUNT;i++)
    {
        m_meshes[i].clear();
    }
    
    
    //Dual textures materials
    InstanceDataDualTex *instance_buffer_dual_tex;

    if (CVS->supportsAsyncInstanceUpload())
    {
        instance_buffer_dual_tex =
            (InstanceDataDualTex*)VAOManager::getInstance()->getInstanceBufferPtr(InstanceTypeDualTex);
    }
    else
    {
        glBindBuffer(GL_ARRAY_BUFFER, VAOManager::getInstance()->getInstanceBuffer(InstanceTypeDualTex));
        instance_buffer_dual_tex =
            (InstanceDataDualTex*) glMapBufferRange(GL_ARRAY_BUFFER, 0,
                                                    10000 * sizeof(InstanceDataDualTex),
                                                    GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_draw_indirect_cmd_id);
        
        
        m_draw_indirect_cmd =
            (DrawElementsIndirectCommand*)glMapBufferRange(GL_DRAW_INDIRECT_BUFFER, 0,
                                                           10000 * sizeof(DrawElementsIndirectCommand),
                                                           GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    }

    Material::ShaderType dual_tex_materials[5] =
    {
        Material::SHADERTYPE_SOLID,
        Material::SHADERTYPE_ALPHA_TEST,
        Material::SHADERTYPE_SOLID_UNLIT,
        Material::SHADERTYPE_SPHERE_MAP,
        Material::SHADERTYPE_VEGETATION
    };

    int material_id;
    for(int i=0;i<5;i++)
    {
        material_id = static_cast<int>(dual_tex_materials[i]);
        fillMaterial( material_id,
                      mesh_map,
                      instance_buffer_dual_tex);
    }
        

    //Three textures materials
    InstanceDataThreeTex *instance_buffer_three_tex;



    if (CVS->supportsAsyncInstanceUpload())
    {
        instance_buffer_three_tex =
            (InstanceDataThreeTex*)VAOManager::getInstance()->getInstanceBufferPtr(InstanceTypeThreeTex);
    }
    else
    {
        glUnmapBuffer(GL_ARRAY_BUFFER);
        glBindBuffer(GL_ARRAY_BUFFER, VAOManager::getInstance()->getInstanceBuffer(InstanceTypeThreeTex));
        instance_buffer_three_tex =
            (InstanceDataThreeTex*) glMapBufferRange(GL_ARRAY_BUFFER, 0,
                                                     10000 * sizeof(InstanceDataSingleTex), //TODO: why single?
                                                     GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    }
    
    Material::ShaderType three_tex_materials[2] =
    {
        Material::SHADERTYPE_DETAIL_MAP,
        Material::SHADERTYPE_NORMAL_MAP
    }; 
    for(int i=0;i<2;i++)
    {
        material_id = static_cast<int>(three_tex_materials[i]);
        fillMaterial( material_id,
                      mesh_map,
                      instance_buffer_three_tex);
    } 
    
    if (!CVS->supportsAsyncInstanceUpload())
    {
        glUnmapBuffer(GL_ARRAY_BUFFER);
        glUnmapBuffer(GL_DRAW_INDIRECT_BUFFER);
    }
} //SolidCommandBuffer::fill

ShadowCommandBuffer::ShadowCommandBuffer(): CommandBuffer()
{
}

void ShadowCommandBuffer::fill(MeshMap *mesh_map)
{
    m_instance_buffer_offset = 0;
    m_command_buffer_offset = 0;
    m_poly_count = 0;
    
    //clear meshes
    for(int i=0;i<4*Material::SHADERTYPE_COUNT;i++)
    {
        m_meshes[i].clear();
    }
    
    InstanceDataSingleTex *shadow_instance_buffer;
    
    if (CVS->supportsAsyncInstanceUpload())
    {
        shadow_instance_buffer = (InstanceDataSingleTex*)VAOManager::getInstance()->getInstanceBufferPtr(InstanceTypeShadow);
    }
    else
    {
        glBindBuffer(GL_ARRAY_BUFFER, VAOManager::getInstance()->getInstanceBuffer(InstanceTypeShadow));
        shadow_instance_buffer =
            (InstanceDataSingleTex*) glMapBufferRange(GL_ARRAY_BUFFER, 0,
                                                      10000 * sizeof(InstanceDataDualTex),
                                                      GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_draw_indirect_cmd_id);
        m_draw_indirect_cmd =
            (DrawElementsIndirectCommand*) glMapBufferRange(GL_DRAW_INDIRECT_BUFFER, 0,
                                                            10000 * sizeof(DrawElementsIndirectCommand),
                                                            GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    }

    Material::ShaderType materials[7] =
    {
        Material::SHADERTYPE_SOLID,
        Material::SHADERTYPE_ALPHA_TEST,
        Material::SHADERTYPE_SOLID_UNLIT,
        Material::SHADERTYPE_NORMAL_MAP,
        Material::SHADERTYPE_SPHERE_MAP,
        Material::SHADERTYPE_DETAIL_MAP,
        Material::SHADERTYPE_VEGETATION
    };
    int material_id;
    
    for(int cascade=0;cascade<4;cascade++)
    {
        for(int i=0;i<7;i++)
        {
            material_id = cascade * 7 + static_cast<int>(materials[i]);
            fillMaterial( material_id,
                          mesh_map,
                          shadow_instance_buffer);            
        }
    }
    
    if (!CVS->supportsAsyncInstanceUpload())
    {
        glUnmapBuffer(GL_ARRAY_BUFFER);
        glUnmapBuffer(GL_DRAW_INDIRECT_BUFFER);
    }
    
} //ShadowCommandBuffer::fill

ReflectiveShadowMapCommandBuffer::ReflectiveShadowMapCommandBuffer(): CommandBuffer()
{
}


void ReflectiveShadowMapCommandBuffer::fill(MeshMap *mesh_map)
{
    m_instance_buffer_offset = 0;
    m_command_buffer_offset = 0;
    m_poly_count = 0;
    
    //clear meshes
    for(int i=0;i<Material::SHADERTYPE_COUNT;i++)
    {
        m_meshes[i].clear();
    }
    
    InstanceDataSingleTex *rsm_instance_buffer;
    
    if (CVS->supportsAsyncInstanceUpload())
    {
        rsm_instance_buffer = (InstanceDataSingleTex*)VAOManager::getInstance()->getInstanceBufferPtr(InstanceTypeRSM);
    }
    else
    {
        glBindBuffer(GL_ARRAY_BUFFER,
                     VAOManager::getInstance()->getInstanceBuffer(InstanceTypeRSM));
        rsm_instance_buffer =
            (InstanceDataSingleTex*)glMapBufferRange(GL_ARRAY_BUFFER, 0,
                                                     10000 * sizeof(InstanceDataDualTex),
                                                     GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_draw_indirect_cmd_id);
        m_draw_indirect_cmd =
            (DrawElementsIndirectCommand*)glMapBufferRange(GL_DRAW_INDIRECT_BUFFER, 0,
                                                           10000 * sizeof(DrawElementsIndirectCommand),
                                                           GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    }
    
    Material::ShaderType materials[5] =
    {
        Material::SHADERTYPE_SOLID,
        Material::SHADERTYPE_ALPHA_TEST,
        Material::SHADERTYPE_SOLID_UNLIT,
        Material::SHADERTYPE_DETAIL_MAP,
        Material::SHADERTYPE_NORMAL_MAP
    };
    int material_id;
    
    for(int i=0;i<5;i++)
    {
        material_id = static_cast<int>(materials[i]);
        fillMaterial( material_id,
                      mesh_map,
                      rsm_instance_buffer);
    }

    if (!CVS->supportsAsyncInstanceUpload())
    {
        glUnmapBuffer(GL_ARRAY_BUFFER);
        glUnmapBuffer(GL_DRAW_INDIRECT_BUFFER);
    }    
    
} //ReflectiveShadowMapCommandBuffer::fill

GlowCommandBuffer::GlowCommandBuffer()
{   
}

void GlowCommandBuffer::fill(MeshMap *mesh_map)
{
    m_instance_buffer_offset = 0;
    m_command_buffer_offset = 0;
    m_poly_count = 0;
    
    m_meshes[0].clear();
    
    GlowInstanceData *glow_instance_buffer;

    if (CVS->supportsAsyncInstanceUpload())
    {
        glow_instance_buffer = (GlowInstanceData*)VAOManager::getInstance()->getInstanceBufferPtr(InstanceTypeGlow);
    }
    else
    {
        glBindBuffer(GL_ARRAY_BUFFER,
                     VAOManager::getInstance()->getInstanceBuffer(InstanceTypeGlow));
        glow_instance_buffer =
            (GlowInstanceData*)glMapBufferRange(GL_ARRAY_BUFFER, 0,
                                                10000 * sizeof(InstanceDataDualTex),
                                                GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_draw_indirect_cmd_id);
        m_draw_indirect_cmd =
            (DrawElementsIndirectCommand*)glMapBufferRange(GL_DRAW_INDIRECT_BUFFER, 0,
                                                           10000 * sizeof(DrawElementsIndirectCommand),
                                                           GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    }
 
     fillMaterial( 0,
                  mesh_map,
                  glow_instance_buffer);   
    
    if (!CVS->supportsAsyncInstanceUpload())
    {
        glUnmapBuffer(GL_ARRAY_BUFFER);
        glUnmapBuffer(GL_DRAW_INDIRECT_BUFFER);
    }   
} //GlowCommandBuffer::fill