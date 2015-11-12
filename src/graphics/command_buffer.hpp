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

#ifndef HEADER_COMMAND_BUFFER_HPP
#define HEADER_COMMAND_BUFFER_HPP

#include "graphics/draw_tools.hpp"
#include "graphics/gl_headers.hpp"
#include "graphics/material.hpp"
#include "graphics/materials.hpp"


#include "graphics/stk_mesh_scene_node.hpp"
#include "graphics/vao_manager.hpp"
#include <irrlicht.h>
#include <unordered_map>


typedef std::vector<std::pair<GLMesh *, irr::scene::ISceneNode*> > InstanceList;
typedef std::unordered_map <irr::scene::IMeshBuffer *, InstanceList > MeshMap;


template<typename InstanceData>
void fillOriginOrientationScale(scene::ISceneNode *node, InstanceData &instance)
{
    const core::matrix4 &mat = node->getAbsoluteTransformation();
    const core::vector3df &Origin = mat.getTranslation();
    const core::vector3df &Orientation = mat.getRotationDegrees();
    const core::vector3df &Scale = mat.getScale();
    instance.Origin.X = Origin.X;
    instance.Origin.Y = Origin.Y;
    instance.Origin.Z = Origin.Z;
    instance.Orientation.X = Orientation.X;
    instance.Orientation.Y = Orientation.Y;
    instance.Orientation.Z = Orientation.Z;
    instance.Scale.X = Scale.X;
    instance.Scale.Y = Scale.Y;
    instance.Scale.Z = Scale.Z;        
}

template<typename InstanceData>
struct InstanceFiller
{
    static void add(GLMesh *, scene::ISceneNode *, InstanceData &);
};

template<typename T>
void FillInstances_impl(InstanceList instance_list,
                        T * instance_buffer,
                        DrawElementsIndirectCommand *command_buffer,
                        size_t &instance_buffer_offset,
                        size_t &command_buffer_offset,
                        size_t &poly_count)
{
    // Should never be empty
    GLMesh *mesh = instance_list.front().first;
    size_t initial_offset = instance_buffer_offset;

    for (unsigned i = 0; i < instance_list.size(); i++)
    {
        auto &Tp = instance_list[i];
        scene::ISceneNode *node = Tp.second;
        InstanceFiller<T>::add(mesh, node, instance_buffer[instance_buffer_offset++]);
        assert(instance_buffer_offset * sizeof(T) < 10000 * sizeof(InstanceDataDualTex)); //TODO
    }

    DrawElementsIndirectCommand &CurrentCommand = command_buffer[command_buffer_offset++];
    CurrentCommand.baseVertex = mesh->vaoBaseVertex;
    CurrentCommand.count = mesh->IndexCount;
    CurrentCommand.firstIndex = mesh->vaoOffset / 2;
    CurrentCommand.baseInstance = initial_offset;
    CurrentCommand.instanceCount = instance_buffer_offset - initial_offset;

    poly_count += (instance_buffer_offset - initial_offset) * mesh->IndexCount / 3;
}

//TODO: clean draw_calls and remove this function
template<typename T>
void FillInstances( const MeshMap &gathered_GL_mesh,
                    std::vector<GLMesh *> &instanced_list,
                    T *instance_buffer,
                    DrawElementsIndirectCommand *command_buffer,
                    size_t &instance_buffer_offset,
                    size_t &command_buffer_offset,
                    size_t &poly_count)
{
    auto It = gathered_GL_mesh.begin(), E = gathered_GL_mesh.end();
    for (; It != E; ++It)
    {
        FillInstances_impl<T>(It->second, instance_buffer, command_buffer, instance_buffer_offset, command_buffer_offset, poly_count);
        if (!CVS->isAZDOEnabled())
            instanced_list.push_back(It->second.front().first);
    }
}

template<typename T>
void expandTexSecondPass(const GLMesh &mesh,
                         const std::vector<GLuint> &prefilled_tex)
{
    TexExpander<typename T::InstancedSecondPassShader>::template
        expandTex(mesh, T::SecondPassTextures, prefilled_tex[0],
                  prefilled_tex[1], prefilled_tex[2]);
}

template<>
void expandTexSecondPass<GrassMat>(const GLMesh &mesh,
                                   const std::vector<GLuint> &prefilled_tex);

template<int N>
class CommandBuffer
{
protected:
    GLuint m_draw_indirect_cmd_id;
    DrawElementsIndirectCommand *m_draw_indirect_cmd;
    std::vector<GLMesh *> m_meshes[N];
    size_t m_offset[N];
    size_t m_size[N];
    
    size_t m_poly_count;
    size_t m_instance_buffer_offset;
    size_t m_command_buffer_offset;

    template<typename T>
    void fillMaterial(int material_id,
                      MeshMap *mesh_map,
                      T *instance_buffer)
    {
        m_offset[material_id] = m_command_buffer_offset;
        for(auto& instance_list : mesh_map[material_id])
        {
            FillInstances_impl<T>(instance_list.second, instance_buffer, m_draw_indirect_cmd, m_instance_buffer_offset, m_command_buffer_offset, m_poly_count);
            if (!CVS->isAZDOEnabled())
                m_meshes[material_id].push_back(instance_list.second.front().first);
            
        }
                
        m_size[material_id] = m_command_buffer_offset - m_instance_buffer_offset; 
    }
    
public:
    CommandBuffer();
    virtual ~CommandBuffer();

    inline size_t getPolyCount() const {return m_poly_count;}

    inline bool isEmpty(Material::ShaderType shader_type) const
    { return m_size[static_cast<int>(shader_type)] == 0;}

    inline void bind() const
    {
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_draw_indirect_cmd_id);        
    }
 
 
    template<typename T, typename...Uniforms>
    void drawIndirectFirstPass(Uniforms...uniforms) const
    {
        T::InstancedFirstPassShader::getInstance()->use();
        T::InstancedFirstPassShader::getInstance()->setUniforms(uniforms...);
        
        glBindVertexArray(VAOManager::getInstance()->getInstanceVAO(T::VertexType,
                                                                    T::Instance));
        for (unsigned i = 0; i < m_meshes[T::MaterialType].size(); i++)
        {
            GLMesh *mesh = m_meshes[T::MaterialType][i];
#ifdef DEBUG
            if (mesh->VAOType != T::VertexType)
            {
                Log::error("CommandBuffer", "Wrong instanced vertex format (hint : %s)", 
                    mesh->textures[0]->getName().getPath().c_str());
                continue;
            }
#endif
            TexExpander<typename T::InstancedFirstPassShader>::template expandTex(*mesh, T::FirstPassTextures);

            glDrawElementsIndirect(GL_TRIANGLES,
                                   GL_UNSIGNED_SHORT,
                                   (const void*)((m_offset[T::MaterialType] + i) * sizeof(DrawElementsIndirectCommand)));
        }        
        
    } //drawIndirectFirstPass

    
    // ----------------------------------------------------------------------------
    template<typename T, typename...Uniforms>
    void drawIndirectSecondPass(const std::vector<GLuint> &prefilled_tex,
                             Uniforms...uniforms                       ) const
    {
        T::InstancedSecondPassShader::getInstance()->use();
        T::InstancedSecondPassShader::getInstance()->setUniforms(uniforms...);

        glBindVertexArray(VAOManager::getInstance()->getInstanceVAO(T::VertexType,
                                                                    T::Instance));
        for (unsigned i = 0; i < m_meshes[T::MaterialType].size(); i++)
        {
            GLMesh *mesh = m_meshes[T::MaterialType][i];
            expandTexSecondPass<T>(*mesh, prefilled_tex);
            glDrawElementsIndirect(GL_TRIANGLES,
                                   GL_UNSIGNED_SHORT,
                                   (const void*)((m_offset[T::MaterialType] + i) * sizeof(DrawElementsIndirectCommand)));
        }
    } //drawIndirectSecondPass
    


    // ----------------------------------------------------------------------------
    template<typename T, typename...Uniforms>
    void drawIndirectReflectiveShadowMap(Uniforms ...uniforms) const
    {
        T::InstancedRSMShader::getInstance()->use();
        T::InstancedRSMShader::getInstance()->setUniforms(uniforms...);
        
        glBindVertexArray(VAOManager::getInstance()->getInstanceVAO(T::VertexType,
                                                                    InstanceTypeRSM));
                                                                    
        for (unsigned i = 0; i < m_meshes[T::MaterialType].size(); i++)

        {
            GLMesh *mesh = m_meshes[T::MaterialType][i];

            TexExpander<typename T::InstancedRSMShader>::template expandTex(*mesh, T::RSMTextures);
            glDrawElementsIndirect(GL_TRIANGLES,
                                   GL_UNSIGNED_SHORT,
                                   (const void*)((m_offset[T::MaterialType] + i) * sizeof(DrawElementsIndirectCommand)));
        }
    } //drawIndirectReflectiveShadowMap



 
     /** Draw the i-th mesh with the specified material
     * (require at least OpenGL 4.0
     * or GL_ARB_base_instance and GL_ARB_draw_indirect extensions)
     */ 
    inline void drawIndirect(int material_id, int i) const
    {
        glDrawElementsIndirect(GL_TRIANGLES,
                               GL_UNSIGNED_SHORT,
                               (const void*)((m_offset[material_id] + i) * sizeof(DrawElementsIndirectCommand)));
    }
 
    /** Draw the meshes with the specified material
     * (require at least OpenGL 4.3 or AZDO extensions)
     */ 
    inline void multidrawIndirect(int material_id) const
    {
        glMultiDrawElementsIndirect(GL_TRIANGLES,
                                    GL_UNSIGNED_SHORT,
                                    (const void*)(m_offset[material_id] * sizeof(DrawElementsIndirectCommand)),
                                    (int) m_size[material_id],
                                    sizeof(DrawElementsIndirectCommand));
    }
};



class SolidCommandBuffer: public CommandBuffer<static_cast<int>(Material::SHADERTYPE_COUNT)>
{
public:
    SolidCommandBuffer();
    void fill(MeshMap *mesh_map);
    

};

class ShadowCommandBuffer: public CommandBuffer<4*static_cast<int>(Material::SHADERTYPE_COUNT)>
{
public:
    ShadowCommandBuffer();
    void fill(MeshMap *mesh_map);
};

class ReflectiveShadowMapCommandBuffer: public CommandBuffer<static_cast<int>(Material::SHADERTYPE_COUNT)>
{
public:
    ReflectiveShadowMapCommandBuffer();
    void fill(MeshMap *mesh_map);
};

class GlowCommandBuffer: public CommandBuffer<1>
{
public:
    GlowCommandBuffer();
    void fill(MeshMap *mesh_map);
};

#endif //HEADER_COMMAND_BUFFER_HPP