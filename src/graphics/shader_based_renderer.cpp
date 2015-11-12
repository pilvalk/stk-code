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

#include "graphics/shader_based_renderer.hpp"

#include "graphics/central_settings.hpp"
#include "graphics/glwrap.hpp"
#include "graphics/graphics_restrictions.hpp"
#include "graphics/lod_node.hpp"
#include "graphics/post_processing.hpp"
#include "graphics/rtts.hpp"
#include "graphics/shaders.hpp"
#include "graphics/stk_scene_manager.hpp"
#include "graphics/texture_manager.hpp"
#include "items/item_manager.hpp"
#include "items/powerup_manager.hpp"
#include "modes/world.hpp"
#include "physics/physics.hpp"
#include "states_screens/race_gui_base.hpp"
#include "tracks/track.hpp"
#include "utils/profiler.hpp"

#include <algorithm> 

extern std::vector<float> BoundingBoxes; //TODO


void ShaderBasedRenderer::compressPowerUpTextures()
{
    for (unsigned i = 0; i < PowerupManager::POWERUP_MAX; i++)
    {
        scene::IMesh *mesh = powerup_manager->m_all_meshes[i];
        if (!mesh)
            continue;
        for (unsigned j = 0; j < mesh->getMeshBufferCount(); j++)
        {
            scene::IMeshBuffer *mb = mesh->getMeshBuffer(j);
            if (!mb)
                continue;
            for (unsigned k = 0; k < 4; k++)
            {
                video::ITexture *tex = mb->getMaterial().getTexture(k);
                if (!tex)
                    continue;
                compressTexture(tex, true);
            }
        }
    }
}

void ShaderBasedRenderer::setOverrideMaterial()
{
    // Overrides
    video::SOverrideMaterial &overridemat = irr_driver->getVideoDriver()->getOverrideMaterial();
    overridemat.EnablePasses = scene::ESNRP_SOLID | scene::ESNRP_TRANSPARENT;
    overridemat.EnableFlags = 0;

    if (m_wireframe)
    {
        overridemat.Material.Wireframe = 1;
        overridemat.EnableFlags |= video::EMF_WIREFRAME;
    }
    if (m_mipviz)
    {
        overridemat.Material.MaterialType = Shaders::getShader(ES_MIPVIZ);
        overridemat.EnableFlags |= video::EMF_MATERIAL_TYPE;
        overridemat.EnablePasses = scene::ESNRP_SOLID;
    }       
}

std::vector<GlowData> ShaderBasedRenderer::updateGlowingList()
{
    // Get a list of all glowing things. The driver's list contains the static ones,
    // here we add items, as they may disappear each frame.
    std::vector<GlowData> glows = irr_driver->getGlowingNodes();

    ItemManager * const items = ItemManager::get();
    const u32 itemcount = items->getNumberOfItems();
    u32 i;

    for (i = 0; i < itemcount; i++)
    {
        Item * const item = items->getItem(i);
        if (!item) continue;
        const Item::ItemType type = item->getType();

        if (type != Item::ITEM_NITRO_BIG && type != Item::ITEM_NITRO_SMALL &&
            type != Item::ITEM_BONUS_BOX && type != Item::ITEM_BANANA && type != Item::ITEM_BUBBLEGUM)
            continue;

        LODNode * const lod = (LODNode *) item->getSceneNode();
        if (!lod->isVisible()) continue;

        const int level = lod->getLevel();
        if (level < 0) continue;

        scene::ISceneNode * const node = lod->getAllNodes()[level];
        node->updateAbsolutePosition();

        GlowData dat;
        dat.node = node;

        dat.r = 1.0f;
        dat.g = 1.0f;
        dat.b = 1.0f;

        const video::SColorf &c = ItemManager::getGlowColor(type);
        dat.r = c.getRed();
        dat.g = c.getGreen();
        dat.b = c.getBlue();

        glows.push_back(dat);
    }
    
    return glows;
}

void ShaderBasedRenderer::prepareForwardRenderer()
{
    irr::video::SColor clearColor(0, 150, 150, 150);
    if (World::getWorld() != NULL)
        clearColor = World::getWorld()->getClearColor();

    glClear(GL_COLOR_BUFFER_BIT);
    glDepthMask(GL_TRUE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClearColor(clearColor.getRed() / 255.f, clearColor.getGreen() / 255.f,
        clearColor.getBlue() / 255.f, clearColor.getAlpha() / 255.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);    
}

// ----------------------------------------------------------------------------
void ShaderBasedRenderer::updateLightsInfo(scene::ICameraSceneNode * const camnode,
                                           float dt)
{
    m_lighting_passes.updateLightsInfo(camnode, dt);
}

// ----------------------------------------------------------------------------
void ShaderBasedRenderer::computeMatrixesAndCameras(scene::ICameraSceneNode *const camnode,
                                                    size_t width, size_t height)
{
    m_current_screen_size = core::vector2df(float(width), float(height));
    m_shadow_matrices.computeMatrixesAndCameras(camnode, width, height);
}   // computeMatrixesAndCameras

// ============================================================================
void ShaderBasedRenderer::renderScene(scene::ICameraSceneNode * const camnode,
                                      std::vector<GlowData>& glows,
                                      float dt,
                                      bool hasShadow,
                                      bool forceRTT)
{
    PostProcessing *post_processing = irr_driver->getPostProcessing();
    m_wind_dir = getWindDir(); //TODO
    
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, SharedGPUObjects::getViewProjectionMatricesUBO());
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, SharedGPUObjects::getLightingDataUBO());
    irr_driver->getSceneManager()->setActiveCamera(camnode);

    PROFILER_PUSH_CPU_MARKER("- Draw Call Generation", 0xFF, 0xFF, 0xFF);
    m_draw_calls.prepareDrawCalls(m_shadow_matrices, camnode);
    PROFILER_POP_CPU_MARKER();
    // Shadows
    {
        // To avoid wrong culling, use the largest view possible
        irr_driver->getSceneManager()->setActiveCamera(m_shadow_matrices.getSunCam());
        if (CVS->isDefferedEnabled() &&
            CVS->isShadowEnabled() && hasShadow)
        {
            PROFILER_PUSH_CPU_MARKER("- Shadow", 0x30, 0x6F, 0x90);
            m_geometry_passes.renderShadows(m_shadow_matrices,
                                            irr_driver->getRTT()->getShadowFrameBuffer());
            PROFILER_POP_CPU_MARKER();
            if (CVS->isGlobalIlluminationEnabled())
            {
                if (!m_shadow_matrices.isRSMMapAvail())
                {
                    PROFILER_PUSH_CPU_MARKER("- RSM", 0xFF, 0x0, 0xFF);
                    m_geometry_passes.renderReflectiveShadowMap(m_draw_calls,
                                                                m_shadow_matrices, 
                                                                irr_driver->getRTT()->getReflectiveShadowMapFrameBuffer()); //TODO: move somewhere else as RSM are computed only once per track
                    m_shadow_matrices.setRSMMapAvail(true);
                    PROFILER_POP_CPU_MARKER();
                }
            }
        }
        irr_driver->getSceneManager()->setActiveCamera(camnode);

    }


    PROFILER_PUSH_CPU_MARKER("- Solid Pass 1", 0xFF, 0x00, 0x00);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    RTT *rtts = irr_driver->getRTT();
    if (CVS->isDefferedEnabled() || forceRTT)
    {
        rtts->getFBO(FBO_NORMAL_AND_DEPTHS).bind();
        glClearColor(0., 0., 0., 0.);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        m_geometry_passes.renderSolidFirstPass(m_draw_calls);
    }
    else
    {
        // We need a cleared depth buffer for some effect (eg particles depth blending)
        if (GraphicsRestrictions::isDisabled(GraphicsRestrictions::GR_FRAMEBUFFER_SRGB_WORKING))
            glDisable(GL_FRAMEBUFFER_SRGB);
        rtts->getFBO(FBO_NORMAL_AND_DEPTHS).bind();
        // Bind() modifies the viewport. In order not to affect anything else,
        // the viewport is just reset here and not removed in Bind().
        const core::recti &vp = Camera::getActiveCamera()->getViewport();
        glViewport(vp.UpperLeftCorner.X,
                   irr_driver->getActualScreenSize().Height - vp.LowerRightCorner.Y,
                   vp.LowerRightCorner.X - vp.UpperLeftCorner.X,
                   vp.LowerRightCorner.Y - vp.UpperLeftCorner.Y);
        glClear(GL_DEPTH_BUFFER_BIT);
        if (GraphicsRestrictions::isDisabled(GraphicsRestrictions::GR_FRAMEBUFFER_SRGB_WORKING))
            glEnable(GL_FRAMEBUFFER_SRGB);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    PROFILER_POP_CPU_MARKER();



    // Lights
    {
        PROFILER_PUSH_CPU_MARKER("- Light", 0x00, 0xFF, 0x00);
        if (CVS->isDefferedEnabled())
            if (CVS->isGlobalIlluminationEnabled() && hasShadow)
            {    
                m_lighting_passes.renderGlobalIllumination( m_shadow_matrices,
                                                            irr_driver->getRTT()->getRadianceHintFrameBuffer(),
                                                            irr_driver->getRTT()->getReflectiveShadowMapFrameBuffer(),
                                                            irr_driver->getRTT()->getFBO(FBO_DIFFUSE));
            }            
            
            m_lighting_passes.renderLights( hasShadow,
                                            irr_driver->getRTT()->getShadowFrameBuffer(),
                                            irr_driver->getRTT()->getFBO(FBO_COMBINED_DIFFUSE_SPECULAR));
        PROFILER_POP_CPU_MARKER();
    }

    // Handle SSAO
    {
        PROFILER_PUSH_CPU_MARKER("- SSAO", 0xFF, 0xFF, 0x00);
        ScopedGPUTimer Timer(irr_driver->getGPUTimer(Q_SSAO));
        if (UserConfigParams::m_ssao)
            irr_driver->renderSSAO();
        PROFILER_POP_CPU_MARKER();
    }

    PROFILER_PUSH_CPU_MARKER("- Solid Pass 2", 0x00, 0x00, 0xFF);
    if (CVS->isDefferedEnabled() || forceRTT)
    {
        rtts->getFBO(FBO_COLORS).bind();
        video::SColor clearColor(0, 150, 150, 150);
        if (World::getWorld() != NULL)
            clearColor = World::getWorld()->getClearColor();

        glClearColor(clearColor.getRed() / 255.f, clearColor.getGreen() / 255.f,
            clearColor.getBlue() / 255.f, clearColor.getAlpha() / 255.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDepthMask(GL_FALSE);
    }
    m_geometry_passes.renderSolidSecondPass( m_draw_calls,
                                             rtts->getRenderTarget(RTT_DIFFUSE),
                                             rtts->getRenderTarget(RTT_SPECULAR),
                                             rtts->getRenderTarget(RTT_HALF1_R));
    PROFILER_POP_CPU_MARKER();

    if (irr_driver->getNormals())
    {
        rtts->getFBO(FBO_NORMAL_AND_DEPTHS).bind();
        m_geometry_passes.renderNormalsVisualisation(m_draw_calls);
        rtts->getFBO(FBO_COLORS).bind();
    }

    // Render ambient scattering
    if (CVS->isDefferedEnabled() && World::getWorld() != NULL &&
        World::getWorld()->isFogEnabled())
    {
        PROFILER_PUSH_CPU_MARKER("- Ambient scatter", 0xFF, 0x00, 0x00);
        ScopedGPUTimer Timer(irr_driver->getGPUTimer(Q_FOG));
        m_lighting_passes.renderAmbientScatter();
        PROFILER_POP_CPU_MARKER();
    }

    {
        PROFILER_PUSH_CPU_MARKER("- Skybox", 0xFF, 0x00, 0xFF);
        ScopedGPUTimer Timer(irr_driver->getGPUTimer(Q_SKYBOX));
        irr_driver->renderSkybox(camnode);
        PROFILER_POP_CPU_MARKER();
    }

    // Render discrete lights scattering
    if (CVS->isDefferedEnabled() && World::getWorld() != NULL &&
        World::getWorld()->isFogEnabled())
    {
        PROFILER_PUSH_CPU_MARKER("- PointLight Scatter", 0xFF, 0x00, 0x00);
        ScopedGPUTimer Timer(irr_driver->getGPUTimer(Q_FOG));
        m_lighting_passes.renderLightsScatter();
        PROFILER_POP_CPU_MARKER();
    }

    if (irr_driver->getRH())
    {
        glDisable(GL_BLEND);
        rtts->getFBO(FBO_COLORS).bind();
        post_processing->renderRHDebug(rtts->getRadianceHintFrameBuffer().getRTT()[0],
                                       rtts->getRadianceHintFrameBuffer().getRTT()[1], 
                                       rtts->getRadianceHintFrameBuffer().getRTT()[2],
                                       m_shadow_matrices.getRHMatrix(),
                                       m_shadow_matrices.getRHExtend());
    }

    if (irr_driver->getGI())
    {
        glDisable(GL_BLEND);
        rtts->getFBO(FBO_COLORS).bind();
        post_processing->renderGI(m_shadow_matrices.getRHMatrix(),
                                  m_shadow_matrices.getRHExtend(),
                                  rtts->getRadianceHintFrameBuffer());
    }

    PROFILER_PUSH_CPU_MARKER("- Glow", 0xFF, 0xFF, 0x00);
    // Render anything glowing.
    if (!m_mipviz && !m_wireframe && UserConfigParams::m_glow)
    {
        ScopedGPUTimer Timer(irr_driver->getGPUTimer(Q_GLOW));
        irr_driver->setPhase(GLOW_PASS);
        irr_driver->renderGlow(glows);
    } // end glow
    PROFILER_POP_CPU_MARKER();

    // Render transparent
    {
        PROFILER_PUSH_CPU_MARKER("- Transparent Pass", 0xFF, 0x00, 0x00);
        ScopedGPUTimer Timer(irr_driver->getGPUTimer(Q_TRANSPARENT));
        m_geometry_passes.renderTransparent(m_draw_calls,
                                            irr_driver->getRTT()->getRenderTarget(RTT_DISPLACE));
        PROFILER_POP_CPU_MARKER();
    }

    m_draw_calls.m_sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

    // Render particles
    {
        PROFILER_PUSH_CPU_MARKER("- Particles", 0xFF, 0xFF, 0x00);
        ScopedGPUTimer Timer(irr_driver->getGPUTimer(Q_PARTICLES));
        renderParticles();
        PROFILER_POP_CPU_MARKER();
    }
    if (!CVS->isDefferedEnabled() && !forceRTT)
    {
        glDisable(GL_FRAMEBUFFER_SRGB);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        return;
    }

    // Ensure that no object will be drawn after that by using invalid pass
    irr_driver->setPhase(PASS_COUNT);
}

// ----------------------------------------------------------------------------

void ShaderBasedRenderer::renderParticles()
{
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    m_draw_calls.renderParticlesList();

//    m_scene_manager->drawAll(scene::ESNRP_TRANSPARENT_EFFECT);
}


void ShaderBasedRenderer::renderBoundingBoxes()
{
    Shaders::ColoredLine *line = Shaders::ColoredLine::getInstance();
    line->use();
    line->bindVertexArray();
    line->bindBuffer();
    line->setUniforms(irr::video::SColor(255, 255, 0, 0));
    const float *tmp = BoundingBoxes.data();
    for (unsigned int i = 0; i < BoundingBoxes.size(); i += 1024 * 6)
    {
        unsigned count = std::min((unsigned)BoundingBoxes.size() - i, (unsigned)1024 * 6);
        glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(float), &tmp[i]);

        glDrawArrays(GL_LINES, 0, count / 3);
    }
}

void ShaderBasedRenderer::debugPhysics()
{
    // Note that drawAll must be called before rendering
    // the bullet debug view, since otherwise the camera
    // is not set up properly. This is only used for
    // the bullet debug view.
    World *world = World::getWorld();
    if (UserConfigParams::m_artist_debug_mode)
        world->getPhysics()->draw();
    if (world != NULL && world->getPhysics() != NULL)
    {
        IrrDebugDrawer* debug_drawer = world->getPhysics()->getDebugDrawer();
        if (debug_drawer != NULL && debug_drawer->debugEnabled())
        {
            const std::map<video::SColor, std::vector<float> >& lines = debug_drawer->getLines();
            std::map<video::SColor, std::vector<float> >::const_iterator it;

            Shaders::ColoredLine *line = Shaders::ColoredLine::getInstance();
            line->use();
            line->bindVertexArray();
            line->bindBuffer();
            for (it = lines.begin(); it != lines.end(); it++)
            {
                line->setUniforms(it->first);
                const std::vector<float> &vertex = it->second;
                const float *tmp = vertex.data();
                for (unsigned int i = 0; i < vertex.size(); i += 1024 * 6)
                {
                    unsigned count = std::min((unsigned)vertex.size() - i, (unsigned)1024 * 6);
                    glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(float), &tmp[i]);

                    glDrawArrays(GL_LINES, 0, count / 3);
                }
            }
            glUseProgram(0);
            glBindVertexArray(0);
        }
    }
}

void ShaderBasedRenderer::renderPostProcessing(Camera * const camera)
{
    scene::ICameraSceneNode * const camnode = camera->getCameraSceneNode();
    const core::recti &viewport = camera->getViewport();
    
    bool isRace = StateManager::get()->getGameState() == GUIEngine::GAME;
    PostProcessing * post_processing = irr_driver->getPostProcessing();
    FrameBuffer *fbo = post_processing->render(camnode, isRace);

    if (irr_driver->getNormals())
        irr_driver->getFBO(FBO_NORMAL_AND_DEPTHS).BlitToDefault(viewport.UpperLeftCorner.X, viewport.UpperLeftCorner.Y, viewport.LowerRightCorner.X, viewport.LowerRightCorner.Y);
    else if (irr_driver->getSSAOViz())
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(viewport.UpperLeftCorner.X, viewport.UpperLeftCorner.Y, viewport.LowerRightCorner.X, viewport.LowerRightCorner.Y);
        post_processing->renderPassThrough(irr_driver->getRTT()->getFBO(FBO_HALF1_R).getRTT()[0], viewport.LowerRightCorner.X - viewport.UpperLeftCorner.X, viewport.LowerRightCorner.Y - viewport.UpperLeftCorner.Y);
    }
    else if (irr_driver->getRSM())
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(viewport.UpperLeftCorner.X, viewport.UpperLeftCorner.Y, viewport.LowerRightCorner.X, viewport.LowerRightCorner.Y);
        post_processing->renderPassThrough(irr_driver->getRTT()->getReflectiveShadowMapFrameBuffer().getRTT()[0], viewport.LowerRightCorner.X - viewport.UpperLeftCorner.X, viewport.LowerRightCorner.Y - viewport.UpperLeftCorner.Y);
    }
    else if (irr_driver->getShadowViz())
    {
        m_shadow_matrices.renderShadowsDebug();
    }
    else
    {
        glEnable(GL_FRAMEBUFFER_SRGB);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        camera->activate();
        post_processing->renderPassThrough(fbo->getRTT()[0], viewport.LowerRightCorner.X - viewport.UpperLeftCorner.X, viewport.LowerRightCorner.Y - viewport.UpperLeftCorner.Y);
        glDisable(GL_FRAMEBUFFER_SRGB);
    }
}


ShaderBasedRenderer::ShaderBasedRenderer()
{
}

ShaderBasedRenderer::~ShaderBasedRenderer()
{
}

void ShaderBasedRenderer::addSunLight(const core::vector3df &pos) {
    m_shadow_matrices.addLight(pos);
}


void ShaderBasedRenderer::render(float dt)
{
    BoundingBoxes.clear(); //TODO: what is it doing here?
    
    compressPowerUpTextures(); //TODO: is it useful every frame?
    
    setOverrideMaterial(); //TODO: is it useful every frame?
    
    std::vector<GlowData> glows = updateGlowingList();
    
    // Start the RTT for post-processing.
    // We do this before beginScene() because we want to capture the glClear()
    // because of tracks that do not have skyboxes (generally add-on tracks)
    irr_driver->getPostProcessing()->begin();

    World *world = World::getWorld(); // Never NULL.
    Track *track = world->getTrack();
    
    RaceGUIBase *rg = world->getRaceGUI();
    if (rg) rg->update(dt);
    
    if (!CVS->isDefferedEnabled())
    {
        prepareForwardRenderer();
    }
    
    for(unsigned int cam = 0; cam < Camera::getNumCameras(); cam++)
    {    
        Camera * const camera = Camera::getCamera(cam);
        scene::ICameraSceneNode * const camnode = camera->getCameraSceneNode();

        std::ostringstream oss;
        oss << "drawAll() for kart " << cam;
        PROFILER_PUSH_CPU_MARKER(oss.str().c_str(), (cam+1)*60,
                                 0x00, 0x00);
        camera->activate(!CVS->isDefferedEnabled());
        rg->preRenderCallback(camera);   // adjusts start referee
        irr_driver->getSceneManager()->setActiveCamera(camnode);

        const core::recti &viewport = camera->getViewport();

        if (!CVS->isDefferedEnabled())
            glEnable(GL_FRAMEBUFFER_SRGB);
        
        PROFILER_PUSH_CPU_MARKER("Update Light Info", 0xFF, 0x0, 0x0);
        m_lighting_passes.updateLightsInfo(camnode, dt);
        PROFILER_POP_CPU_MARKER();
        PROFILER_PUSH_CPU_MARKER("UBO upload", 0x0, 0xFF, 0x0);
        computeMatrixesAndCameras(camnode, viewport.LowerRightCorner.X - viewport.UpperLeftCorner.X, viewport.LowerRightCorner.Y - viewport.UpperLeftCorner.Y);
        m_shadow_matrices.updateSunOrthoMatrices();
        irr_driver->uploadLightingData(); //TODO: move method; update "global" lighting (sun and spherical harmonics)
        PROFILER_POP_CPU_MARKER();
        renderScene(camnode, glows, dt, track->hasShadows(), false); 
        
        if (irr_driver->getBoundingBoxesViz())
        {        
            renderBoundingBoxes();
        }
        
        debugPhysics();
        
        if (CVS->isDefferedEnabled())
        {
            renderPostProcessing(camera);
        }
        
        // Save projection-view matrix for the next frame
        camera->setPreviousPVMatrix(irr_driver->getProjViewMatrix());

        PROFILER_POP_CPU_MARKER();
        
    }  // for i<world->getNumKarts()
    
    // Use full screen size
    float tmp[2];
    tmp[0] = float(irr_driver->getActualScreenSize().Width);
    tmp[1] = float(irr_driver->getActualScreenSize().Height);
    glBindBuffer(GL_UNIFORM_BUFFER, 
                 SharedGPUObjects::getViewProjectionMatricesUBO());
    glBufferSubData(GL_UNIFORM_BUFFER, (16 * 9) * sizeof(float),
                    2 * sizeof(float), tmp);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glUseProgram(0);

    // Set the viewport back to the full screen for race gui
    irr_driver->getVideoDriver()->setViewPort(core::recti(0, 0,
        irr_driver->getActualScreenSize().Width,
        irr_driver->getActualScreenSize().Height));
    
    for(unsigned int i=0; i<Camera::getNumCameras(); i++)
    {
        Camera *camera = Camera::getCamera(i);
        std::ostringstream oss;
        oss << "renderPlayerView() for kart " << i;

        PROFILER_PUSH_CPU_MARKER(oss.str().c_str(), 0x00, 0x00, (i+1)*60);
        rg->renderPlayerView(camera, dt);

        PROFILER_POP_CPU_MARKER();
    }  // for i<getNumKarts

    {
        ScopedGPUTimer Timer(irr_driver->getGPUTimer(Q_GUI));
        PROFILER_PUSH_CPU_MARKER("GUIEngine", 0x75, 0x75, 0x75);
        // Either render the gui, or the global elements of the race gui.
        GUIEngine::render(dt);
        PROFILER_POP_CPU_MARKER();
    }

    // Render the profiler
    if(UserConfigParams::m_profiler_enabled)
    {
        PROFILER_DRAW();
    }

#ifdef DEBUG
    drawDebugMeshes();
#endif

    PROFILER_PUSH_CPU_MARKER("EndSccene", 0x45, 0x75, 0x45);
    irr_driver->getVideoDriver()->endScene();
    PROFILER_POP_CPU_MARKER();

    irr_driver->getPostProcessing()->update(dt);    
}

