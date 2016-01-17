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
//  You should have received a copy éof the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

#include "graphics/draw_policies.hpp"
#include "graphics/draw_tools.hpp"
#include "graphics/materials.hpp"

// ----------------------------------------------------------------------------
template<typename T, int ...List>
void renderMeshes1stPass(const DrawCalls& draw_calls)
{
    auto &meshes = T::List::getInstance()->SolidPass;
    T::FirstPassShader::getInstance()->use();
    if (CVS->isARBBaseInstanceUsable())
        glBindVertexArray(VAOManager::getInstance()->getVAO(T::VertexType));
    for (unsigned i = 0; i < meshes.size(); i++)
    {
        GLMesh &mesh = *(STK::tuple_get<0>(meshes.at(i)));
        if (!CVS->isARBBaseInstanceUsable())
            glBindVertexArray(mesh.vao);
        if (mesh.VAOType != T::VertexType)
        {
#ifdef DEBUG
            Log::error("Materials", "Wrong vertex Type associed to pass 1 (hint texture : %s)", mesh.textures[0]->getName().getPath().c_str());
#endif
            continue;
        }

        if (CVS->isAZDOEnabled())
            HandleExpander<typename T::FirstPassShader>::template expand(mesh.TextureHandles, T::FirstPassTextures);
        else
            TexExpander<typename T::FirstPassShader>::template expandTex(mesh, T::FirstPassTextures);
        CustomUnrollArgs<List...>::template drawMesh<typename T::FirstPassShader>(meshes.at(i));
    }
}   // renderMeshes1stPass

// ----------------------------------------------------------------------------
template<typename T, int...List>
void renderMeshes2ndPass( const std::vector<uint64_t> &Prefilled_Handle,
    const std::vector<GLuint> &Prefilled_Tex)
{
    auto &meshes = T::List::getInstance()->SolidPass;
    T::SecondPassShader::getInstance()->use();
    if (CVS->isARBBaseInstanceUsable())
        glBindVertexArray(VAOManager::getInstance()->getVAO(T::VertexType));
    for (unsigned i = 0; i < meshes.size(); i++)
    {
        GLMesh &mesh = *(STK::tuple_get<0>(meshes.at(i)));
        if (!CVS->isARBBaseInstanceUsable())
            glBindVertexArray(mesh.vao);

        if (mesh.VAOType != T::VertexType)
        {
#ifdef DEBUG
            Log::error("Materials", "Wrong vertex Type associed to pass 2 "
                                    "(hint texture : %s)", 
                       mesh.textures[0]->getName().getPath().c_str());
#endif
            continue;
        }

        if (CVS->isAZDOEnabled())
            HandleExpander<typename T::SecondPassShader>::template 
                expand(mesh.TextureHandles, T::SecondPassTextures, 
                       Prefilled_Handle[0], Prefilled_Handle[1],
                       Prefilled_Handle[2]);
        else
            TexExpander<typename T::SecondPassShader>::template 
                expandTex(mesh, T::SecondPassTextures, Prefilled_Tex[0], 
                          Prefilled_Tex[1], Prefilled_Tex[2]);
        CustomUnrollArgs<List...>::template drawMesh<typename T::SecondPassShader>(meshes.at(i));
    }
}   // renderMeshes2ndPass



// ----------------------------------------------------------------------------
template<typename T, int...List>
void renderShadow(unsigned cascade)
{
    auto &t = T::List::getInstance()->Shadows[cascade];
    T::ShadowPassShader::getInstance()->use();
    if (CVS->isARBBaseInstanceUsable())
        glBindVertexArray(VAOManager::getInstance()->getVAO(T::VertexType));
    for (unsigned i = 0; i < t.size(); i++)
    {
        GLMesh *mesh = STK::tuple_get<0>(t.at(i));
        if (!CVS->isARBBaseInstanceUsable())
            glBindVertexArray(mesh->vao);
        if (CVS->isAZDOEnabled())
            HandleExpander<typename T::ShadowPassShader>::template expand(mesh->TextureHandles, T::ShadowTextures);
        else
            TexExpander<typename T::ShadowPassShader>::template expandTex(*mesh, T::ShadowTextures);
        CustomUnrollArgs<List...>::template drawMesh<typename T::ShadowPassShader>(t.at(i), cascade);
    }   // for i
}   // renderShadow


// ----------------------------------------------------------------------------
template<typename T, int... Selector>
void drawRSM(const core::matrix4 & rsm_matrix)
{
    T::RSMShader::getInstance()->use();
    if (CVS->isARBBaseInstanceUsable())
        glBindVertexArray(VAOManager::getInstance()->getVAO(T::VertexType));
    auto t = T::List::getInstance()->RSM;
    for (unsigned i = 0; i < t.size(); i++)
    {
        std::vector<GLuint> Textures;
        GLMesh *mesh = STK::tuple_get<0>(t.at(i));
        if (!CVS->isARBBaseInstanceUsable())
            glBindVertexArray(mesh->vao);
        if (CVS->isAZDOEnabled())
            HandleExpander<typename T::RSMShader>::template expand(mesh->TextureHandles, T::RSMTextures);
        else
            TexExpander<typename T::RSMShader>::template expandTex(*mesh, T::RSMTextures);
        CustomUnrollArgs<Selector...>::template drawMesh<typename T::RSMShader>(t.at(i), rsm_matrix);
    }
}   // drawRSM



// ----------------------------------------------------------------------------
void GL3DrawPolicy::drawSolidFirstPass(const DrawCalls& draw_calls) const
{
    renderMeshes1stPass<DefaultMaterial, 2, 1>(draw_calls);
    renderMeshes1stPass<SplattingMat, 2, 1>(draw_calls);
    renderMeshes1stPass<UnlitMat, 3, 2, 1>(draw_calls);
    renderMeshes1stPass<AlphaRef, 3, 2, 1>(draw_calls);
    renderMeshes1stPass<GrassMat, 3, 2, 1>(draw_calls);
    renderMeshes1stPass<NormalMat, 2, 1>(draw_calls);
    renderMeshes1stPass<SphereMap, 2, 1>(draw_calls);
    renderMeshes1stPass<DetailMat, 2, 1>(draw_calls);
}

// ----------------------------------------------------------------------------
void GL3DrawPolicy::drawSolidSecondPass (const DrawCalls& draw_calls,
                                         const std::vector<uint64_t>& handles,
                                         const std::vector<GLuint>& prefilled_tex) const
{
    renderMeshes2ndPass<DefaultMaterial, 3, 1> (handles, prefilled_tex);
    renderMeshes2ndPass<AlphaRef,        3, 1> (handles, prefilled_tex);
    renderMeshes2ndPass<UnlitMat,        3, 1> (handles, prefilled_tex);
    renderMeshes2ndPass<SplattingMat,    1   > (handles, prefilled_tex);
    renderMeshes2ndPass<SphereMap,       2, 1> (handles, prefilled_tex);
    renderMeshes2ndPass<DetailMat,       1   > (handles, prefilled_tex);
    renderMeshes2ndPass<GrassMat,        3, 1> (handles, prefilled_tex);
    renderMeshes2ndPass<NormalMat,       3, 1> (handles, prefilled_tex);
}

// ----------------------------------------------------------------------------
void GL3DrawPolicy::drawGlow(const DrawCalls& draw_calls,
                             const std::vector<GlowData>& glows) const
{
    for (u32 i = 0; i < glows.size(); i++)
        glows[i].node->render();    
}


// ----------------------------------------------------------------------------
void GL3DrawPolicy::drawShadows(const DrawCalls& draw_calls, unsigned cascade) const
{
    renderShadow<DefaultMaterial, 1>(cascade);
    renderShadow<SphereMap, 1>(cascade);
    renderShadow<DetailMat, 1>(cascade);
    renderShadow<SplattingMat, 1>(cascade);
    renderShadow<NormalMat, 1>(cascade);
    renderShadow<AlphaRef, 1>(cascade);
    renderShadow<UnlitMat, 1>(cascade);
    renderShadow<GrassMat, 3, 1>(cascade);    
}

// ----------------------------------------------------------------------------
void GL3DrawPolicy::drawReflectiveShadowMap(const DrawCalls& draw_calls,
                                            const core::matrix4 &rsm_matrix) const
{
    drawRSM<DefaultMaterial, 3, 1>(rsm_matrix);
    drawRSM<AlphaRef, 3, 1>(rsm_matrix);
    drawRSM<NormalMat, 3, 1>(rsm_matrix);
    drawRSM<UnlitMat, 3, 1>(rsm_matrix);
    drawRSM<DetailMat, 3, 1>(rsm_matrix);
    drawRSM<SplattingMat, 1>(rsm_matrix);    
}


// ----------------------------------------------------------------------------
void IndirectDrawPolicy::drawSolidFirstPass(const DrawCalls& draw_calls) const
{
    //TODO: find a way to add TextureMarix in instanced shaders,
    //and remove these four lines
    renderMeshes1stPass<DefaultMaterial, 2, 1>(draw_calls);
    renderMeshes1stPass<SplattingMat, 2, 1>(draw_calls);
    renderMeshes1stPass<UnlitMat, 3, 2, 1>(draw_calls);
    renderMeshes1stPass<AlphaRef, 3, 2, 1>(draw_calls);
    
    draw_calls.drawIndirectSolidFirstPass();
}

// ----------------------------------------------------------------------------
void IndirectDrawPolicy::drawSolidSecondPass (const DrawCalls& draw_calls,
                                              const std::vector<uint64_t>& handles,
                                              const std::vector<GLuint>& prefilled_tex) const
{
    //TODO: find a way to add TextureMarix in instanced shaders,
    //and remove these four lines
    renderMeshes2ndPass<DefaultMaterial, 3, 1> (handles, prefilled_tex);
    renderMeshes2ndPass<AlphaRef,        3, 1> (handles, prefilled_tex);
    renderMeshes2ndPass<UnlitMat,        3, 1> (handles, prefilled_tex);
    renderMeshes2ndPass<SplattingMat,    1   > (handles, prefilled_tex);    
    
    draw_calls.drawIndirectSolidSecondPass(prefilled_tex);
}

// ----------------------------------------------------------------------------
void IndirectDrawPolicy::drawNormals(const DrawCalls& draw_calls) const
{
    draw_calls.drawIndirectNormals();
}

// ----------------------------------------------------------------------------
void IndirectDrawPolicy::drawGlow(const DrawCalls& draw_calls,
                                  const std::vector<GlowData>& glows) const
{
    draw_calls.drawIndirectGlow();
}

// ----------------------------------------------------------------------------
void IndirectDrawPolicy::drawShadows(const DrawCalls& draw_calls, unsigned cascade) const
{
    draw_calls.drawIndirectShadows(cascade);
}

// ----------------------------------------------------------------------------
void IndirectDrawPolicy::drawReflectiveShadowMap(const DrawCalls& draw_calls,
                                                 const core::matrix4 &rsm_matrix) const
{
    drawRSM<SplattingMat, 1>(rsm_matrix); //TODO: write instanced splatting RSM shader and remove this line
    draw_calls.drawIndirectReflectiveShadowMaps(rsm_matrix);    
}


// ----------------------------------------------------------------------------
void MultidrawPolicy::drawSolidFirstPass(const DrawCalls& draw_calls) const
{
    //TODO: find a way to add TextureMarix in instanced shaders,
    //and remove these four lines
    renderMeshes1stPass<DefaultMaterial, 2, 1>(draw_calls);
    renderMeshes1stPass<SplattingMat, 2, 1>(draw_calls);
    renderMeshes1stPass<UnlitMat, 3, 2, 1>(draw_calls);
    renderMeshes1stPass<AlphaRef, 3, 2, 1>(draw_calls);
    
    draw_calls.multidrawSolidFirstPass();
}

// ----------------------------------------------------------------------------
void MultidrawPolicy::drawSolidSecondPass (const DrawCalls& draw_calls,
                                           const std::vector<uint64_t>& handles,
                                           const std::vector<GLuint>& prefilled_tex) const
{
    //TODO: find a way to add TextureMarix in instanced shaders,
    //and remove these four lines
    renderMeshes2ndPass<DefaultMaterial, 3, 1> (handles, prefilled_tex);
    renderMeshes2ndPass<AlphaRef,        3, 1> (handles, prefilled_tex);
    renderMeshes2ndPass<UnlitMat,        3, 1> (handles, prefilled_tex);
    renderMeshes2ndPass<SplattingMat,    1   > (handles, prefilled_tex);
    
    draw_calls.multidrawSolidSecondPass(handles);
}

// ----------------------------------------------------------------------------
void MultidrawPolicy::drawGlow(const DrawCalls& draw_calls,
                               const std::vector<GlowData>& glows) const
{
    draw_calls.multidrawGlow();
}

// ----------------------------------------------------------------------------
void MultidrawPolicy::drawNormals(const DrawCalls& draw_calls) const
{
    draw_calls.multidrawNormals();
}

// ----------------------------------------------------------------------------
void MultidrawPolicy::drawShadows(const DrawCalls& draw_calls, unsigned cascade) const
{
    draw_calls.multidrawShadows(cascade);
}

// ----------------------------------------------------------------------------
void MultidrawPolicy::drawReflectiveShadowMap(const DrawCalls& draw_calls,
                                              const core::matrix4 &rsm_matrix) const
{
    drawRSM<SplattingMat, 1>(rsm_matrix); //TODO: add instanced splatting RSM shader
    draw_calls.multidrawReflectiveShadowMaps(rsm_matrix);    
}