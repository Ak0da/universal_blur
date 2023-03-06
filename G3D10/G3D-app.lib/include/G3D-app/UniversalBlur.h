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
        bool                           generateMipMaps = false) {

        alwaysAssertM(format, "Format may not be nullptr");

        const int w = (target->width() - guardBandRemoveX * 2) / divWidth;
        const int h = (target->height() - guardBandRemoveY * 2) / divHeight;
        if (isNull(texture) || (texture->format() != format)) {
            // Allocate
            texture = Texture::createEmpty
            (textureName,
                w,
                h,
                format,
                Texture::DIM_2D,
                generateMipMaps);

            if (isNull(framebuffer)) {
                framebuffer = Framebuffer::create("");
            }
            framebuffer->set(attachmentPoint, texture);

        }
        else if ((texture->width() != w) ||
            (texture->height() != h)) {
            texture->resize(w, h);
        }
    }

    class UniversalBlur : public ReferenceCountedObject {
        // MOTION BLUR PART STARTS HERE ----------------------------------------------


    protected:
        UniversalBlur::UniversalBlur() : m_debugShowTiles(false){}

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
            const Vector2int16                trimBandThickness)
        {

            Args args;
            GBuffer::bindReadArgs
            (args,
                GBuffer::Field::SS_POSITION_CHANGE,
                velocity);

            GBuffer::bindWriteUniform
            (args,
                GBuffer::Field::SS_POSITION_CHANGE,
                velocity->encoding());

            args.setMacro("maxBlurRadius", maxBlurRadiusPixels);

            // Horizontal pass
            rd->push2D(m_tileMinMaxTempFramebuffer); {
                rd->clear();
                args.setUniform("inputShift", Vector2(trimBandThickness));
                args.setMacro("INPUT_HAS_MIN_SPEED", 0);
                args.setRect(rd->viewport());
                LAUNCH_SHADER("MotionBlur_tileMinMax.*", args);
            } rd->pop2D();

            // Vertical pass
            GBuffer::bindReadArgs
            (args,
                GBuffer::Field::SS_POSITION_CHANGE,
                m_tileMinMaxTempFramebuffer->texture(0));

            rd->push2D(m_tileMinMaxFramebuffer); {
                rd->clear();
                args.setUniform("inputShift", Vector2::zero());
                args.setMacro("INPUT_HAS_MIN_SPEED", 1);
                args.setRect(rd->viewport());
                LAUNCH_SHADER("MotionBlur_tileMinMax.*", args);
            } rd->pop2D();
        }

        /** Compute m_neighborMax from m_tileMax */
        void computeNeighborMinMax
        (RenderDevice* rd,
            const shared_ptr<Texture>& tileMax)
        {

            rd->push2D(m_neighborMinMaxFramebuffer); {

                rd->setColorClearValue(Color4::zero());
                rd->clear(true, false, false);

                Args args;
                GBuffer::bindReadArgs
                (args,
                    GBuffer::Field::SS_POSITION_CHANGE,
                    tileMax);

                GBuffer::bindWriteUniform
                (args,
                    GBuffer::Field::SS_POSITION_CHANGE,
                    tileMax->encoding());

                args.setRect(rd->viewport());
                LAUNCH_SHADER("MotionBlur_neighborMinMax.*", args);

            } rd->pop2D();
        }

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
            Vector2int16                      trimBandThickness)
        {

            // Switch to 2D mode using the current framebuffer
            rd->push2D(); {
                rd->clear(true, false, false);
                rd->setGuardBandClip2D(trimBandThickness);

                Args args;

                GBuffer::bindReadArgs
                (args,
                    GBuffer::Field::SS_POSITION_CHANGE,
                    velocity);

                neighborMax->setShaderArgs(args, "neighborMinMax_", Sampler::buffer());

                args.setUniform("colorBuffer", color, Sampler::buffer());
                args.setUniform("randomBuffer", m_randomBuffer, Sampler::buffer());
                args.setUniform("exposureTime", exposureTimeFraction);

                args.setMacro("numSamplesOdd", numSamplesOdd);
                args.setMacro("maxBlurRadius", maxBlurRadiusPixels);

                args.setUniform("depthBuffer", depth, Sampler::buffer());

                args.setUniform("trimBandThickness", trimBandThickness);

                args.setRect(rd->viewport());
                LAUNCH_SHADER("MotionBlur_gather.*", args);

            } rd->pop2D();
        }
        
        /** Allocates tileMax and neighborMax as needed */
        void updateBuffers
        (const shared_ptr<Texture>& velocityTexture,
            int                              maxBlurRadiusPixels,
            Vector2int16                     inputGuardBandThickness)
        {
            const int w = (velocityTexture->width() - inputGuardBandThickness.x * 2);
            const int h = (velocityTexture->height() - inputGuardBandThickness.y * 2);

            // Tile boundaries will appear if the tiles are not radius x radius
            const int smallWidth = iCeil(w / float(maxBlurRadiusPixels));
            const int smallHeight = iCeil(h / float(maxBlurRadiusPixels));

            if (isNull(m_tileMinMaxFramebuffer)) {
                const bool generateMipMaps = false;
                Texture::Encoding encoding = velocityTexture->encoding();

                // Add a "G" channel
                if (encoding.format->numberFormat == ImageFormat::FLOATING_POINT_FORMAT) {
                    encoding.format = ImageFormat::RGB16F();
                }
                else {
                    encoding.format = ImageFormat::RGB8();
                }
                // Ensure consistent mapping across the new G channel
                encoding.readMultiplyFirst.g = encoding.readMultiplyFirst.r;
                encoding.readAddSecond.g = encoding.readAddSecond.r;

                m_tileMinMaxTempFramebuffer = Framebuffer::create(Texture::createEmpty("G3D::MotionBlur::m_tileMinMaxTempFramebuffer", h, smallWidth, encoding, Texture::DIM_2D, generateMipMaps));
                m_tileMinMaxTempFramebuffer->texture(0)->visualization = Texture::Visualization::unitVector();

                m_tileMinMaxFramebuffer = Framebuffer::create(Texture::createEmpty("G3D::MotionBlur::m_tileMinMaxFramebuffer", smallWidth, smallHeight, encoding, Texture::DIM_2D, generateMipMaps));
                m_tileMinMaxFramebuffer->texture(0)->visualization = Texture::Visualization::unitVector();

                m_neighborMinMaxFramebuffer = Framebuffer::create(Texture::createEmpty("G3D::MotionBlur::m_neighborMaxFramebuffer", smallWidth, smallHeight, encoding, Texture::DIM_2D, generateMipMaps));
                m_neighborMinMaxFramebuffer->texture(0)->visualization = m_tileMinMaxFramebuffer->texture(0)->visualization;
            }

            // Resize if needed
            m_tileMinMaxFramebuffer->resize(smallWidth, smallHeight);
            m_tileMinMaxTempFramebuffer->resize(h, smallWidth);
            m_neighborMinMaxFramebuffer->resize(smallWidth, smallHeight);
        }

        void makeRandomBuffer()
        {
            static const int N = 32;
            Color3unorm8 buf[N * N];
            Random rnd;

            for (int i = N * N - 1; i >= 0; --i) {
                Color3unorm8& p = buf[i];
                p.r = unorm8::fromBits(rnd.integer(0, 255));
            }
            const bool generateMipMaps = false;
            m_randomBuffer = Texture::fromMemory("randomBuffer", buf, ImageFormat::RGB8(),
                N, N, 1, 1, ImageFormat::R8(), Texture::DIM_2D, generateMipMaps);
        }

        /** Debug visualization of the motion blur tiles and directions. Called from apply() */
        void debugDrawTiles
        (RenderDevice* rd,
            const shared_ptr<Texture>& neighborMax,
            int                             maxBlurRadiusPixels)
        {

            // read back the neighborhood velocity for each tile
            const shared_ptr<Image> cpuNeighborMax = neighborMax->toImage();

            // Draw tile boundaries
            {
                SlowMesh mesh(PrimitiveType::LINES);
                mesh.setColor(Color3::black());

                for (int x = 0; x < rd->width(); x += maxBlurRadiusPixels) {
                    mesh.makeVertex(Point2((float)x, 0));
                    mesh.makeVertex(Point2((float)x, float(rd->height())));
                }

                for (int y = 0; y < rd->height(); y += maxBlurRadiusPixels) {
                    mesh.makeVertex(Point2(0, (float)y));
                    mesh.makeVertex(Point2(float(rd->width()), (float)y));
                }

                mesh.render(rd);
            }

            // Show velocity vectors
            {
                SlowMesh mesh(PrimitiveType::LINES);
                mesh.setColor(Color3::white());

                for (int x = 0; x < cpuNeighborMax->width(); ++x) {
                    for (int y = 0; y < cpuNeighborMax->height(); ++y) {

                        const Point2& center = Point2(x + 0.5f, y + 0.5f) * (float)maxBlurRadiusPixels;
                        mesh.makeVertex(center);

                        const Vector3& N = Vector3(cpuNeighborMax->get<Color3>(x, y) * neighborMax->encoding().readMultiplyFirst.rgb() + neighborMax->encoding().readAddSecond.rgb());
                        mesh.makeVertex(center + N.xy());
                    }
                }
                mesh.render(rd);
            }
        }

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

        //static shared_ptr<UniversalBlur> create(const String& debugName = "G3D::UniversalBlur");
        
        static shared_ptr<UniversalBlur> create()
        {
            return shared_ptr<UniversalBlur>(new UniversalBlur());
        }
        

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
        void resizeBuffers(shared_ptr<Texture> target, int reducedResolutionFactor, Vector2int16 trimBandThickness)
        {
            const ImageFormat* plusAlphaFormat = ImageFormat::getFormatWithAlpha(target->format());

            // Need an alpha channel for storing radius in the packed and far temp buffers
            matchTarget(m_debugName + "::m_packedBuffer", target, 1, 1, trimBandThickness.x, trimBandThickness.y, plusAlphaFormat, m_packedBuffer, m_packedFramebuffer, Framebuffer::COLOR0);

            matchTarget(m_debugName + "::m_tempNearBuffer", target, reducedResolutionFactor, 1, trimBandThickness.x, trimBandThickness.y, plusAlphaFormat, m_tempNearBuffer, m_horizontalFramebuffer, Framebuffer::COLOR0);
            matchTarget(m_debugName + "::m_tempBlurBuffer", target, reducedResolutionFactor, 1, trimBandThickness.x, trimBandThickness.y, plusAlphaFormat, m_tempBlurBuffer, m_horizontalFramebuffer, Framebuffer::COLOR1);

            // Need an alpha channel (for coverage) in the near buffer
            matchTarget(m_debugName + "::m_nearBuffer", target, reducedResolutionFactor, reducedResolutionFactor, trimBandThickness.x, trimBandThickness.y, plusAlphaFormat, m_nearBuffer, m_verticalFramebuffer, Framebuffer::COLOR0);
            matchTarget(m_debugName + "::m_blurBuffer", target, reducedResolutionFactor, reducedResolutionFactor, trimBandThickness.x, trimBandThickness.y, target->format(), m_blurBuffer, m_verticalFramebuffer, Framebuffer::COLOR1);
        }

        /** Writes m_packedBuffer */
        void computeCoC
        (RenderDevice* rd,
            const shared_ptr<Texture>& color,
            const shared_ptr<Texture>& depth,
            const shared_ptr<Camera>& camera,
            Vector2int16                    trimBandThickness,
            float& farRadiusRescale,
            float                           maxCoCRadiusPixels)
        {

            rd->push2D(m_packedFramebuffer); {
                rd->clear();
                Args args;

                args.setUniform("clipInfo", camera->projection().reconstructFromDepthClipInfo());
                args.setUniform("COLOR_buffer", color, Sampler::video());
                args.setUniform("DEPTH_buffer", depth, Sampler::buffer());
                args.setUniform("trimBandThickness", trimBandThickness);
                args.setRect(rd->viewport());

                const float axisSize = (camera->fieldOfViewDirection() == FOVDirection::HORIZONTAL) ? float(color->width()) : float(color->height());

                if (camera->depthOfFieldSettings().model() == DepthOfFieldModel::ARTIST) {

                    args.setUniform("nearBlurryPlaneZ", camera->depthOfFieldSettings().nearBlurryPlaneZ());
                    args.setUniform("nearSharpPlaneZ", camera->depthOfFieldSettings().nearSharpPlaneZ());
                    args.setUniform("farSharpPlaneZ", camera->depthOfFieldSettings().farSharpPlaneZ());
                    args.setUniform("farBlurryPlaneZ", camera->depthOfFieldSettings().farBlurryPlaneZ());

                    // This is a positive number
                    const float nearScale =
                        camera->depthOfFieldSettings().nearBlurRadiusFraction() /
                        (camera->depthOfFieldSettings().nearBlurryPlaneZ() - camera->depthOfFieldSettings().nearSharpPlaneZ());
                    alwaysAssertM(nearScale >= 0.0f, "Near normalization must be a non-negative factor");
                    args.setUniform("nearScale", nearScale * axisSize / maxCoCRadiusPixels);

                    // This is a positive number
                    const float farScale =
                        camera->depthOfFieldSettings().farBlurRadiusFraction() /
                        (camera->depthOfFieldSettings().farSharpPlaneZ() - camera->depthOfFieldSettings().farBlurryPlaneZ());
                    alwaysAssertM(farScale >= 0.0f, "Far normalization must be a non-negative factor");
                    args.setUniform("farScale", farScale * axisSize / maxCoCRadiusPixels);

                    farRadiusRescale =
                        max(camera->depthOfFieldSettings().farBlurRadiusFraction(), camera->depthOfFieldSettings().nearBlurRadiusFraction()) /
                        max(camera->depthOfFieldSettings().farBlurRadiusFraction(), 0.0001f);

                }
                else {
                    farRadiusRescale = 1.0f;
                    const float screenSize = (camera->fieldOfViewDirection() == FOVDirection::VERTICAL) ? rd->viewport().height() : rd->viewport().width();

                    // Collect terms from the CoC computation that are constant across the screen into a single
                    // constant
                    const float scale = (screenSize * 0.5f / tan(camera->fieldOfViewAngle() * 0.5f)) * camera->depthOfFieldSettings().lensRadius() /
                        (camera->depthOfFieldSettings().focusPlaneZ() * maxCoCRadiusPixels);

                    args.setUniform("focusPlaneZ", camera->depthOfFieldSettings().focusPlaneZ());
                    args.setUniform("scale", scale);

                    // This is a hack to support ChromaBlur experiments. It is not used by the default 
                    // G3D shaders, and is not the intended use of nearSharpPlaneZ.
                    args.setUniform("nearSharpPlaneZ", camera->depthOfFieldSettings().nearSharpPlaneZ());
                }

                args.setMacro("MODEL", camera->depthOfFieldSettings().model().toString());
                args.setMacro("PACK_WITH_COLOR", 1);

                // In case the output is an unsigned format
                args.setUniform("writeScaleBias", Vector2(0.5f, 0.5f));
                args.setMacro("COMPUTE_PERCENT", camera->depthOfFieldSettings().diskFramebuffer() ? 100 : -1);
                LAUNCH_SHADER("DepthOfField_circleOfConfusion.pix", args);

            } rd->pop2D();
        }

        void blurPass
        (RenderDevice* rd,
            const shared_ptr<Texture>& blurInput,
            const shared_ptr<Texture>& nearInput,
            const shared_ptr<Framebuffer>& output,
            bool                            horizontal,
            const shared_ptr<Camera>& camera,
            const Rect2D& fullViewport,
            float                           maxCoCRadiusPixels,
            bool                            diskFramebuffer)
        {

            alwaysAssertM(notNull(blurInput), "Input is nullptr");

            // Dimension along which the blur fraction is measured
            const float dimension =
                float((camera->fieldOfViewDirection() == FOVDirection::HORIZONTAL) ?
                    fullViewport.width() : fullViewport.height());

            // Compute the worst-case near plane blur
            int nearBlurRadiusPixels;
            {
                float n = 0.0f;
                if (camera->depthOfFieldSettings().model() == DepthOfFieldModel::ARTIST) {
                    n = camera->depthOfFieldSettings().nearBlurRadiusFraction() * dimension;
                }
                else {
                    n = -camera->circleOfConfusionRadiusPixels(
                        min(camera->m_closestNearPlaneZForDepthOfField,
                            camera->projection().nearPlaneZ()),
                        fullViewport);
                }

                // Clamp to the maximum permitted radius for this camera
                nearBlurRadiusPixels = iCeil(min(camera->m_viewportFractionMaxCircleOfConfusion * fullViewport.width(), n));

                if (nearBlurRadiusPixels < camera->depthOfFieldSettings().reducedResolutionFactor() - 1) {
                    // Avoid ever showing the downsampled buffer without blur
                    nearBlurRadiusPixels = 0;
                }
            }


            rd->push2D(output); {
                rd->clear();
                Args args;
                args.setUniform("blurSourceBuffer", blurInput, Sampler::buffer());
                args.setUniform("nearSourceBuffer", nearInput, Sampler::buffer(), true);
                args.setUniform("maxCoCRadiusPixels", int(maxCoCRadiusPixels));
                args.setUniform("lowResolutionFactor", (float)camera->depthOfFieldSettings().reducedResolutionFactor());
                args.setUniform("nearBlurRadiusPixels", nearBlurRadiusPixels);
                args.setUniform("invNearBlurRadiusPixels", 1.0f / max(float(nearBlurRadiusPixels), 0.0001f));
                args.setUniform("fieldOfView", (float)camera->fieldOfViewAngle());
                args.setMacro("HORIZONTAL", horizontal ? 1 : 0);
                args.setMacro("COMPUTE_PERCENT", diskFramebuffer ? 100 : -1);
                args.setRect(rd->viewport());
                LAUNCH_SHADER("DepthOfField_blur.*", args);
            } rd->pop2D();
        }

        /** Writes to the currently-bound framebuffer. */
        void composite
        (RenderDevice* rd,
            shared_ptr<Texture>             packedBuffer,
            shared_ptr<Texture>             blurBuffer,
            shared_ptr<Texture>             nearBuffer,
            DebugOption                     debugOption,
            Vector2int16                    outputGuardBandThickness,
            float                           farRadiusRescale,
            bool                            diskFramebuffer)
        {

            debugAssert(farRadiusRescale >= 0.0);
            rd->push2D(); {
                rd->clear(true, false, false);
                rd->setDepthTest(RenderDevice::DEPTH_ALWAYS_PASS);
                rd->setDepthWrite(false);
                Args args;

                args.setUniform("blurBuffer", blurBuffer, Sampler::video());
                args.setUniform("nearBuffer", nearBuffer, Sampler::video());
                args.setUniform("packedBuffer", packedBuffer, Sampler::buffer());
                args.setUniform("packedBufferInvSize", Vector2(1.0f, 1.0f) / packedBuffer->vector2Bounds());
                args.setUniform("farRadiusRescale", farRadiusRescale);
                args.setMacro("COMPUTE_PERCENT", diskFramebuffer ? 100 : -1);
                args.setUniform("debugOption", debugOption);
                args.setRect(Rect2D::xywh(Vector2(outputGuardBandThickness), rd->viewport().wh() - 2.0f * Vector2(outputGuardBandThickness)));

                LAUNCH_SHADER("DepthOfField_composite.*", args);
            } rd->pop2D();
        }

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
            DebugOption debugOption = NONE)
        {

            if (!camera->universalBlurSettings().enabled()) {
                return;
            }

            BEGIN_PROFILER_EVENT("G3D::UniversalBlur::MotionBlur::apply");

            if (isNull(m_randomBuffer)) {
                makeRandomBuffer();
            }

            const int   dimension = (camera->fieldOfViewDirection() == FOVDirection::HORIZONTAL) ? color->width() : color->height();

            const int   maxBlurRadiusPixels = max(4, iCeil(float(dimension) * camera->motionBlurSettings().maxBlurDiameterFraction() / 2.0f));
            const int   numSamplesOdd = nextOdd(camera->motionBlurSettings().numSamples());
            const float exposureTimeFraction = camera->motionBlurSettings().exposureFraction();

            updateBuffers(velocity, maxBlurRadiusPixels, trimBandThickness);

            shared_ptr<Texture> src;

            // Copy the input to another buffer

            if (((notNull(rd->framebuffer()) &&
                (color == rd->framebuffer()->get(Framebuffer::COLOR0)->texture())) ||
                !trimBandThickness.isZero())) {
                // The input color buffer is the current framebuffer's draw target.
                // Make a copy so that we can read from it during the final gatherBlur pass.
                // Note that if we knew that we were performing multiple effects at the same time
                // (e.g., Film, DepthOfField, and MotionBlur), we could avoid this copy by 
                // connecting the output of one to the input of the next.

                if (isNull(m_cachedSrc) || (m_cachedSrc->format() != color->format())) {
                    // Reallocate the underlying texture
                    bool generateMipMaps = false;
                    m_cachedSrc = Texture::createEmpty("G3D::MotionBlur::src", color->width() - trimBandThickness.x * 2, color->height() - trimBandThickness.y * 2, color->format(), Texture::DIM_2D, generateMipMaps);
                }
                else {
                    m_cachedSrc->resize(color->width() - trimBandThickness.x * 2, color->height() - trimBandThickness.y * 2);
                }

                src = m_cachedSrc;

                // Copy and strip the trim band
                Texture::copy(color, src, 0, 0, 1.0f, trimBandThickness, CubeFace::POS_X, CubeFace::POS_X, rd, false);

            }
            else {
                src = color;
            }

            computeTileMinMax(rd, velocity, maxBlurRadiusPixels, trimBandThickness);
            computeNeighborMinMax(rd, m_tileMinMaxFramebuffer->texture(0));
            gatherBlur(rd, src, m_neighborMinMaxFramebuffer->texture(0), velocity, depth, numSamplesOdd, maxBlurRadiusPixels, exposureTimeFraction, trimBandThickness);

            if (m_debugShowTiles) {
                rd->push2D(); {
                    debugDrawTiles(rd, m_neighborMinMaxFramebuffer->texture(0), maxBlurRadiusPixels);
                } rd->pop2D();
            }

            END_PROFILER_EVENT();

            // DoF Part of the function starts here ---------------------------------------------

            if ((camera->depthOfFieldSettings().model() == DepthOfFieldModel::NONE)) {
                const shared_ptr<Framebuffer>& f = rd->framebuffer();
                const shared_ptr<Framebuffer::Attachment>& a = f->get(Framebuffer::COLOR0);

                if (isNull(f) || (a->texture() != color)) {
                    Texture::copy(color, a->texture(), 0, 0, 1.0f, trimBandThickness, CubeFace::POS_X, CubeFace::POS_X, rd, false);
                }

                // Exit abruptly because DoF is disabled
                return;
            }

            alwaysAssertM(notNull(color), "Color buffer may not be nullptr");
            alwaysAssertM(notNull(depth), "Depth buffer may not be nullptr");

            BEGIN_PROFILER_EVENT("G3D::UniversalBlur::DepthOfField::apply");
            resizeBuffers(color, camera->depthOfFieldSettings().reducedResolutionFactor(), trimBandThickness);

            const Rect2D& viewport = color->rect2DBounds();

            // Magic scaling factor for the artist mode depth of field model far-field radius
            float farRadiusRescale = 1.0f;
            const float maxCoCRadiusPixels = ceil(camera->maxCircleOfConfusionRadiusPixels(viewport));
            const bool diskFramebuffer = camera->depthOfFieldSettings().diskFramebuffer();

            debugAssert(maxCoCRadiusPixels >= 0.0f);
            computeCoC(rd, color, depth, camera, trimBandThickness, farRadiusRescale, maxCoCRadiusPixels);
            blurPass(rd, m_packedBuffer, m_packedBuffer, m_horizontalFramebuffer, true, camera, viewport, maxCoCRadiusPixels, diskFramebuffer);
            blurPass(rd, m_tempBlurBuffer, m_tempNearBuffer, m_verticalFramebuffer, false, camera, viewport, maxCoCRadiusPixels, diskFramebuffer);
            composite(rd, m_packedBuffer, m_blurBuffer, m_nearBuffer, debugOption, trimBandThickness, farRadiusRescale, diskFramebuffer);
            END_PROFILER_EVENT();
        }
        
        
    };

} // namespace

#endif // GLG3D_UniversalBlur_h