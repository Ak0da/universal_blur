#pragma once

#ifndef GLG3D_UniversalBlur_h
#define GLG3D_UniversalBlur_h

#include "G3D-base/platform.h"
#include "G3D-base/ReferenceCount.h"
#include "G3D-app/GBuffer.h"

// Bullshit imports because of direct definition because of linker error when defining in the .cpp
#include "G3D-gfx/Profiler.h"
#include "G3D-gfx/Texture.h"
#include "G3D-gfx/RenderDevice.h"
#include "G3D-gfx/Shader.h"
#include "G3D-app/Camera.h"
#include "G3D-app/Draw.h"
#include "G3D-app/SlowMesh.h"

namespace G3D {

    class Texture;
    class RenderDevice;
    class Shader;
    class MotionBlurSettings;
    class Camera;

    static void matchTarget
    (const String& textureName,
        const shared_ptr<Texture>& target,
        int                            divWidth,
        int                            divHeight,
        int                            guardBandRemoveX,
        int                            guardBandRemoveY,
        const ImageFormat* format,
        shared_ptr<Texture>& texture,
        shared_ptr<Framebuffer>& framebuffer,
        Framebuffer::AttachmentPoint   attachmentPoint = Framebuffer::COLOR0,
        bool                           generateMipMaps = false);

    class UniversalBlur : public ReferenceCountedObject {
        // MOTION BLUR PART STARTS HERE ----------------------------------------------


    protected:
        UniversalBlur::UniversalBlur();/* : m_debugShowTiles(false) {}*/

        /** The source color is copied into this if needed. Saved between invocations to avoid reallocating the texture.*/
        shared_ptr<Texture>         m_cachedSrc;

        bool                        m_debugShowTiles;

        /** Size ceil(w / maxBlurRadius) x ceil(h / maxBlurRadius). RG = max velocity in tile, B = min speed in tile */
        shared_ptr<Framebuffer>     m_tileMinMaxFramebuffer;

        /** Size h x ceil(w / maxBlurRadius). RG = max velocity in tile, B = min speed in tile */
        shared_ptr<Framebuffer>     m_tileMinMaxTempFramebuffer;

        /** Size ceil(w / maxBlurRadius) x ceil(h / maxBlurRadius).
            RG = max velocity in neighborhood, B = min speed in neighborhood */
        shared_ptr<Framebuffer>     m_neighborMinMaxFramebuffer;

        /** 32x32 buffer of RG values on [0, 1) */
        shared_ptr<Texture>         m_randomBuffer;

        /** Compute m_tileMax from sharpVelocity */
        void computeTileMinMax
        (RenderDevice* rd,
            const shared_ptr<Texture>& velocity,
            int                               maxBlurRadiusPixels,
            const Vector2int16                trimBandThickness);

        /** Compute m_neighborMax from m_tileMax */
        void computeNeighborMinMax
        (RenderDevice* rd,
            const shared_ptr<Texture>& tileMax);

        /** Called from apply() to compute the blurry image to the current
            frame buffer by gathering. */
        virtual void gatherBlur
        (RenderDevice* rd,
            const shared_ptr<Texture>& color,
            const shared_ptr<Texture>& neighborMax,
            const shared_ptr<Texture>& velocity,
            const shared_ptr<Texture>& depth,
            int                               numSamplesOdd,
            int                               maxBlurRadiusPixels,
            float                             exposureTimeFraction,
            Vector2int16                      trimBandThickness);
        
        /** Allocates tileMax and neighborMax as needed */
        void updateBuffers
        (const shared_ptr<Texture>& velocityTexture,
            int                              maxBlurRadiusPixels,
            Vector2int16                     inputGuardBandThickness);

        void makeRandomBuffer();

        /** Debug visualization of the motion blur tiles and directions. Called from apply() */
        void debugDrawTiles
        (RenderDevice* rd,
            const shared_ptr<Texture>& neighborMax,
            int                             maxBlurRadiusPixels);

        /** Returns n if it is odd, otherwise returns n + 1 */
        inline static int nextOdd(int n) {
            return n + 1 - (n & 1);
        }

    public:

        enum DebugOption {
            NONE,
            SHOW_COC,
            SHOW_REGION,
            SHOW_NEAR,
            SHOW_BLURRY,
            SHOW_INPUT,
            SHOW_MID_AND_FAR,
            SHOW_SIGNED_COC,
        };

        //UniversalBlur();

        static shared_ptr<UniversalBlur> create(const String& debugName = "G3D::UniversalBlur");
        
        //static shared_ptr<UniversalBlur> create();
        /*
        {
            return shared_ptr<UniversalBlur>(new UniversalBlur());
        }
        */

        /**
            \param trimBandThickness Input texture coordinates are clamped to
            a region inset on all sides by this amount.  Set this to non-zero if
            the input color buffer is larger than the desired output region but does
            not have useful data around the border.  Typically m_settings.hdrFramebuffer.depthGuardBandThickness - m_settings.hdrFramebuffer.colorGuardBandThickness.

        */
        /*
        virtual void apply
        (RenderDevice* rd,
            const shared_ptr<Texture>& color,
            const shared_ptr<Texture>& velocity,
            const shared_ptr<Texture>& depth,
            const shared_ptr<Camera>& camera,
            Vector2int16                      trimBandThickness);
        */

        /** Toggle visualization showing the tile boundaries, which are
            set by maxBlurRadiusPixels.*/
        void setDebugShowTiles(bool b) {
            m_debugShowTiles = b;
        }

        bool debugShowTiles() const {
            return m_debugShowTiles;
        }


    // DoF PART STARTS HERE ----------------------------------------------

        

    protected:

        String              m_debugName;

        /** Color in RGB, circle of confusion and 'near field' bit in A.
            Precision determined by the input, RGB8, RGB16F, or RGB32F.

            The A channel values are always written with only 8 bits of
            effective precision.

            The radius (A channel) values are scaled and biased to [0, 1].
            Unpack them to pixel radii with:

            \code
            r = ((a * 2) - 1) * maxRadius
            \endcode

            Where maxRadius the larger of the maximum near and far field
            blurs.  The decoded radius is negative in the far field (the packed
            alpha channel should look like a head lamp on a dark night, with
            nearby objects bright, the focus field gray, and the distance black).
        */
        shared_ptr<Texture>                 m_packedBuffer;
        shared_ptr<Framebuffer>             m_packedFramebuffer;

        shared_ptr<Framebuffer>             m_horizontalFramebuffer;
        shared_ptr<Texture>                 m_tempNearBuffer;
        shared_ptr<Texture>                 m_tempBlurBuffer;

        shared_ptr<Framebuffer>             m_verticalFramebuffer;
        shared_ptr<Texture>                 m_nearBuffer;
        shared_ptr<Texture>                 m_blurBuffer;

        /** Allocates and resizes buffers */
        void resizeBuffers(shared_ptr<Texture> target, int reducedResolutionFactor, Vector2int16 trimBandThickness);

        /** Writes m_packedBuffer */
        void computeCoC
        (RenderDevice* rd,
            const shared_ptr<Texture>& color,
            const shared_ptr<Texture>& depth,
            const shared_ptr<Camera>& camera,
            Vector2int16                    trimBandThickness,
            float& farRadiusRescale,
            float                           maxCoCRadiusPixels);

        void blurPass
        (RenderDevice* rd,
            const shared_ptr<Texture>& velocity,
            const shared_ptr<Texture>& blurInput,
            const shared_ptr<Texture>& nearInput,
            const shared_ptr<Texture>& neighborMax,
            const shared_ptr<Framebuffer>& output,
            bool                            horizontal,
            const shared_ptr<Camera>& camera,
            const Rect2D& fullViewport,
            float                           maxCoCRadiusPixels,
            Vector2int16                    trimBandThickness,
            float                           exposureTimeFraction,
            int                             maxBlurRadiusPixels,
            bool                            diskFramebuffer);

        /** Writes to the currently-bound framebuffer. */
        void composite
        (RenderDevice* rd,
            shared_ptr<Texture>             packedBuffer,
            shared_ptr<Texture>             blurBuffer,
            shared_ptr<Texture>             nearBuffer,
            DebugOption                     debugOption,
            Vector2int16                    outputGuardBandThickness,
            float                           farRadiusRescale,
            bool                            diskFramebuffer);

    public:

        /** \param debugName Used for naming textures. Does not affect which shaders are loaded.*/
        //static shared_ptr<UniversalBlur> create(const String& debugName = "G3D::UniversalBlur");

        /** Applies depth of field blur to supplied images and renders to
            the currently-bound framebuffer.  The current framebuffer may
            have the \a color and \a depth values bound to it.

            Reads depth reconstruction and circle of confusion parameters
            from \a camera.

            Centers the output on the target framebuffer, so no explicit
            output guard band is specified.
        */
        /*
        virtual void apply
        (RenderDevice* rd,
            shared_ptr<Texture>       color,
            shared_ptr<Texture>       depth,
            const shared_ptr<Camera>& camera,
            Vector2int16              trimBandThickness,
            DebugOption               debugOption = NONE);
        };
        */
    

        // COMMON PART STARTS HERE -------------------------------------------------

        /* MB
            virtual void apply
            (RenderDevice* rd,
                const shared_ptr<Texture>& color,
                const shared_ptr<Texture>& velocity,
                const shared_ptr<Texture>& depth,
                const shared_ptr<Camera>& camera,
                Vector2int16                      trimBandThickness);
            */

        virtual void UniversalBlur::apply
        (RenderDevice* rd,
            shared_ptr<Texture> color,
            const shared_ptr<Texture>& depth,
            const shared_ptr<Texture>& velocity,
            const shared_ptr<Camera>& camera,
            Vector2int16 trimBandThickness,
            DebugOption debugOption = NONE);
        
    };

} // namespace

#endif // GLG3D_UniversalBlur_h