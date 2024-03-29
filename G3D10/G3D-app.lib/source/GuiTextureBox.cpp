/**
  \file G3D-app.lib/source/GuiTextureBox.cpp

  G3D Innovation Engine http://casual-effects.com/g3d
  Copyright 2000-2021, Morgan McGuire
  All rights reserved
  Available under the BSD License
*/
#include "G3D-base/fileutils.h"
#include "G3D-base/FileSystem.h"
#include "G3D-gfx/RenderDevice.h"
#include "G3D-gfx/GLCaps.h"
#include "G3D-gfx/Shader.h"
#include "G3D-app/ScreenCapture.h"
#include "G3D-app/GuiTextureBox.h"
#include "G3D-app/GuiButton.h"
#include "G3D-app/GuiPane.h"
#include "G3D-app/Draw.h"
#include "G3D-app/FileDialog.h"
#include "G3D-app/GApp.h"
#include "G3D-app/VideoOutput.h"
#include "G3D-gfx/GLFWWindow.h"

namespace G3D {


static Vector3 uvToXYZ(float u, float v) {
    float theta = v * pif();
    float phi = u * 2 * pif();
    float sinTheta = sin(theta);
    return Vector3(cos(phi) * sinTheta, cos(theta), sin(phi) * sinTheta);
}

GuiTextureBox::GuiTextureBox
(GuiContainer*       parent,
 const GuiText&      caption,
 GApp*               app,
 const shared_ptr<Texture>& t,
 bool                embeddedMode,
 bool                drawInverted) : 
    GuiContainer(parent, caption),
    m_showInfo(embeddedMode), 
    m_showCubemapEdges(false),
    m_drawInverted(drawInverted),
    m_lastFormat(nullptr),
    m_dragging(false), 
    m_readbackXY(-1, -1),
    m_embeddedMode(embeddedMode),
    m_app(app) {

    setTexture(t);
    setCaptionHeight(0.0f);
    float aspect = 1440.0f / 900.0f;
    setSize(Vector2(240 * aspect, 240));

    zoomToFit();
}


GuiTextureBox::~GuiTextureBox() {
    if (m_videoOutput) {
        m_videoOutput->abort();
    }
}


class GuiTextureBoxInspector : public GuiWindow {
protected:

    /** Settings of the original GuiTextureBox */
    Texture::Visualization&     m_settings;

    /** The internal texture box */
    GuiTextureBox*              m_textureBox;

    GApp*                       m_app;

    shared_ptr<GuiWindow>       m_parentWindow;

    GuiDropDownList*            m_modeDropDownList;
    
    GuiDropDownList*            m_layerDropDownList;
    GuiDropDownList*            m_mipLevelDropDownList;

    GuiPane*                    m_drawerPane;

    mutable GuiLabel*           m_xyLabel;
    mutable GuiLabel*           m_uvLabel;
    mutable GuiLabel*           m_xyzLabel;
    mutable GuiLabel*           m_rgbaLabel;
    mutable GuiLabel*           m_ARGBLabel;
 
    /** Adds two labels to create a two-column display and returns a pointer to the second label. */
    static GuiLabel* addPair(GuiPane* p, const GuiText& key, const GuiText& val, int captionWidth = 130, GuiLabel* nextTo = nullptr, int moveDown = 0) {
        GuiLabel* keyLabel = p->addLabel(key);
        if (nextTo) {
            keyLabel->moveRightOf(nextTo);
        }
        if (moveDown != 0) {
            keyLabel->moveBy(0, (float)moveDown);
        }
        keyLabel->setWidth((float)captionWidth);
        GuiLabel* valLabel = p->addLabel(val);
        valLabel->moveRightOf(keyLabel);
        valLabel->setWidth(200);
        valLabel->setXAlign(GFont::XALIGN_LEFT);
        return valLabel;
    }


    static String valToText(const Color4& val) {
        if (val.isFinite()) {
            return format("(%6.3f, %6.3f, %6.3f, %6.3f)", val.r, val.g, val.b, val.a);
        } else {
            return "Unknown";
        }
    }

public:

    /** \param parentWindow Hold a pointer to the window containing the original 
        GuiTextureBox so that it is not collected while we have its Settings&. */
    GuiTextureBoxInspector(const String& windowCaption, const shared_ptr<Texture>& texture, Texture::Visualization& settings, const shared_ptr<GuiWindow>& parentWindow, GApp* app) :
        GuiWindow(windowCaption,
            parentWindow->theme(), 
            Rect2D::xywh(0,0, 100, 100),
            GuiTheme::NORMAL_WINDOW_STYLE, 
            REMOVE_ON_CLOSE),
        m_settings(settings),
        m_app(app),
        m_parentWindow(parentWindow) {

        const float pixelScale = GLFWWindow::defaultGuiPixelScale();
        const Vector2 screenBounds((float)parentWindow->window()->width() / pixelScale, (float)parentWindow->window()->height() / pixelScale);

        GuiPane* leftPane = pane();

        m_textureBox = leftPane->addTextureBox(m_app, "", texture, true);
        m_textureBox->setSize(screenBounds - Vector2(375, 200));
        m_textureBox->zoomToFit();

        leftPane->pack();

        //////////////////////////////////////////////////////////////////////
        // Place the preset list in the empty space next to the drawer, over the TextureBox control
        Array<String> presetList;

        // This list must be kept in sync with onEvent
        presetList.append("<Click to load>", "sRGB Image", "Radiance", "Reflectivity");
        presetList.append( "8-bit Normal/Dir", "Float Normal/Dir");
        presetList.append("Depth Buffer", "Bump Map (in Alpha)", "Texture Coordinates");
        presetList.append("2D Motion Vectors");
        m_modeDropDownList = leftPane->addDropDownList("Vis. Preset", presetList);
        m_modeDropDownList->setWidth(238);
        m_modeDropDownList->setCaptionWidth(99);
        m_modeDropDownList->moveBy(5, 0);
        GuiPane* visPane = leftPane->addPane("", GuiTheme::NO_PANE_STYLE);
        
        Array<String> channelList;
        // this order must match the order of the Channels enum
        channelList.append("RGB", "R", "G", "B");
        channelList.append("R as Luminance", "G as Luminance", "B as Luminance", "A as Luminance");
        channelList.append("RGB/3 as Luminance", "True Luminance");
        GuiDropDownList* channelDropDown = visPane->addDropDownList("Channels", channelList, (int*)&m_settings.channels);
        channelDropDown->setCaptionWidth(99);
        channelDropDown->setWidth(238);

        GuiLabel* documentCaption = visPane->addLabel("Document");
        documentCaption->setWidth(65.0f);
        documentCaption->moveBy(-4.0f, 0.0f);
        GuiNumberBox<float>* gammaBox = visPane->addNumberBox(GuiText("g", GFont::fromFile(System::findDataFile("greek.fnt"))), &m_settings.documentGamma, "", GuiTheme::LINEAR_SLIDER, 0.1f, 15.0f);
        gammaBox->setCaptionWidth(37.0f);
        gammaBox->setUnitsSize(5.0f);
        gammaBox->setWidth(170.0f);
        gammaBox->moveRightOf(documentCaption);

        GuiNumberBox<float>* minBox = visPane->addNumberBox("Range", &m_settings.min);
        minBox->setCaptionWidth(102);
        minBox->setUnitsSize(0.0f);
        minBox->setWidth(161.0f);
        
        GuiNumberBox<float>* maxBox = visPane->addNumberBox("-", &m_settings.max);
        maxBox->setCaptionWidth(12.0f);
        maxBox->setWidth(85.0f);
        maxBox->moveRightOf(minBox);
        maxBox->moveBy(8, 0);

        visPane->addCheckBox("Show Motion Vectors", &m_settings.showMotionVectors)->moveBy(-2, 0);
        GuiNumberBox<float>* vectorSpacingBox = visPane->addNumberBox("Spacing", &m_settings.motionVectorSpacing, "px", GuiTheme::LINEAR_SLIDER, 4.0f, 256.0f);
        vectorSpacingBox->setCaptionWidth(80.0f);
        vectorSpacingBox->setUnitsSize(18.0f);
        vectorSpacingBox->moveBy(21, -5);

        GuiNumberBox<float>* vectorScaleBox = visPane->addNumberBox("Scale", &m_settings.motionVectorScale, "x", GuiTheme::LOG_SLIDER, 0.02f, 10.0f);
        vectorScaleBox->setCaptionWidth(80.0f);
        vectorScaleBox->setUnitsSize(18.0f);
        vectorScaleBox->moveBy(21, 0);

        visPane->pack();
        visPane->setWidth(230);

        //////////////////////////////////////////////////////////////////////
        // Height of caption and button bar
        const float cs = 20;

        // Height of the drawer
        const float h = cs - 1;
        const shared_ptr<GFont>& iconFont = GFont::fromFile(System::findDataFile("icon.fnt"));

        m_drawerPane = pane();

        // Contents of the tools drawer:
        {
            // static const char* infoIcon = "i";
            static const char* zoomIcon = "L";
            static const char* diskIcon = "\xcd";
            static const char* movieIcon = "\xb8";
            //static const char* inspectorIcon = "\xa0";

            debugAssert(! m_clientRect.isEmpty());
            GuiButton* saveButton = m_drawerPane->addButton(GuiText(diskIcon, iconFont, h), 
                                                            GuiControl::Callback(m_textureBox, &GuiTextureBox::save),
                                                            GuiTheme::TOOL_BUTTON_STYLE);
            saveButton->setSize(h, h);

            GuiButton* rawSaveButton = m_drawerPane->addButton("raw",//GuiText("R", iconFont, h), 
                                                            GuiControl::Callback(m_textureBox, &GuiTextureBox::rawSave),
                                                            GuiTheme::TOOL_BUTTON_STYLE);
            rawSaveButton->setSize(h, h);

            
            GuiCheckBox* movieSaveButton = m_drawerPane->addCheckBox(GuiText(movieIcon, iconFont, h),
                Pointer<bool>(m_textureBox, &GuiTextureBox::movieRecording, &GuiTextureBox::setMovieRecording),
                GuiTheme::TOOL_CHECK_BOX_STYLE);
            movieSaveButton->setSize(h, h);

            GuiButton* zoomInButton = m_drawerPane->addButton(GuiText(zoomIcon, iconFont, h), 
                                                              GuiControl::Callback(m_textureBox, &GuiTextureBox::zoomIn), 
                                                              GuiTheme::TOOL_BUTTON_STYLE);
            zoomInButton->setSize(h, h);
            zoomInButton->moveBy(h/3, 0);

            GuiButton* fitToWindowButton = m_drawerPane->addButton(GuiText("fit", shared_ptr<GFont>(), h - 7), 
                                                                   GuiControl::Callback(m_textureBox, &GuiTextureBox::zoomToFit), GuiTheme::TOOL_BUTTON_STYLE);
            fitToWindowButton->setSize(h, h);

            GuiButton* zoom100Button = m_drawerPane->addButton(GuiText("1:1", shared_ptr<GFont>(), h - 8), 
                                                               GuiControl::Callback(m_textureBox, &GuiTextureBox::zoomTo1), GuiTheme::TOOL_BUTTON_STYLE);
            zoom100Button->setSize(h, h);

            GuiButton* zoomOutButton = m_drawerPane->addButton(GuiText(zoomIcon, iconFont, h/2), 
                                                               GuiControl::Callback(m_textureBox, &GuiTextureBox::zoomOut),
                                                               GuiTheme::TOOL_BUTTON_STYLE);
            zoomOutButton->setSize(h, h);

        }
        //m_drawerPane->moveRightOf(dataPane);
        m_drawerPane->pack();
        ///////////////////////////////////////////////////////////////////////////////
        GuiPane* dataPane = leftPane->addPane("", GuiTheme::NO_PANE_STYLE);

        int captionWidth = 55;
        m_xyLabel = addPair(dataPane, "xy =", "", 30);
        m_xyLabel->setWidth(70);
        if (texture->dimension() == Texture::DIM_CUBE_MAP || 
            texture->dimension() == Texture::DIM_CUBE_MAP_ARRAY){
            m_xyzLabel = addPair(dataPane, "xyz =", "", 30, m_xyLabel);
            m_xyzLabel->setWidth(160);
        } else {
            m_uvLabel = addPair(dataPane, "uv =", "", 30, m_xyLabel);
            m_uvLabel->setWidth(120);
        }

        m_rgbaLabel = addPair(dataPane, "rgba =", "", captionWidth);
        m_rgbaLabel->moveBy(-13,0);
        m_ARGBLabel = addPair(dataPane, "ARGB =", "", captionWidth);
        dataPane->addLabel(GuiText("Before gamma correction", shared_ptr<GFont>(), 8))->moveBy(Vector2(5, -5));
        if ((texture->dimension() == Texture::DIM_CUBE_MAP) ||
            (texture->dimension() == Texture::DIM_CUBE_MAP_ARRAY)) {
            dataPane->addCheckBox("Show Cube Edges", &m_textureBox->m_showCubemapEdges);
        }
        dataPane->pack();  
        dataPane->moveRightOf(visPane);
        dataPane->moveBy(-20,-10);
        leftPane->pack();
        //////////////////////////////////////////////////////////////////////
        GuiScrollPane* scrollPane = leftPane->addScrollPane(true, false, GuiTheme::BORDERLESS_SCROLL_PANE_STYLE);
        GuiPane* infoPane = scrollPane->viewPane();

        addPair(infoPane, "Format:", texture->format()->name());
        if (texture->depth() > 1) {
            addPair(infoPane, "Size:"  , format("%dx%dx%d", texture->width(), texture->height(), texture->depth()));
        } else {
            addPair(infoPane, "Size:"  , format("%dx%d", texture->width(), texture->height()));
        }        
        
        String dim;
        switch (texture->dimension()) {
        case Texture::DIM_2D: dim = "DIM_2D"; break;
        case Texture::DIM_3D: dim = "DIM_3D"; break;
        case Texture::DIM_2D_RECT: dim = "DIM_2D_RECT"; break;
        case Texture::DIM_CUBE_MAP: dim = "DIM_CUBE_MAP"; break;
        case Texture::DIM_2D_ARRAY: dim = "DIM_2D_ARRAY"; break;
        case Texture::DIM_CUBE_MAP_ARRAY: dim = "DIM_CUBE_MAP_ARRAY"; break;
        }
        addPair(infoPane, "Dimension:", dim);
        if (texture->depth() > 1) {
            Array<String> indexList;
            for (int i = 0; i < texture->depth(); ++i) {
                indexList.append(format("Layer %d", i));
            }
            m_layerDropDownList = infoPane->addDropDownList("", indexList, &(settings.layer) );
        }
                
        if (texture->hasMipMaps()) {
            addPair(infoPane, "MipMaps levels: ", GuiText(G3D::String(std::to_string(texture->numMipMapLevels()))));
            infoPane->addNumberBox(GuiText("MipMap : "), &(settings.mipLevel), "", GuiTheme::LINEAR_SLIDER, 0, texture->numMipMapLevels() - 1);
        } else {
            infoPane->addLabel("No MipMaps");
        }


        //addPair(infoPane, "Min Value:",  valToText(texture->min()), 80, nullptr, 20);
        addPair(infoPane, "Min Value:", valToText(texture->min()), 80);
        addPair(infoPane, "Mean Value:", valToText(texture->mean()), 80);
        addPair(infoPane, "Max Value:",  valToText(texture->max()), 80);
        addPair(infoPane, "ReadMultiplyFirst:", valToText(texture->encoding().readMultiplyFirst), 120);
        addPair(infoPane, "ReadAddSecond:", valToText(texture->encoding().readAddSecond), 120);
        infoPane->pack();
        scrollPane->pack();
        scrollPane->moveRightOf(dataPane);
        scrollPane->moveBy(0, -20);
        scrollPane->setHeight(160);
        scrollPane->setWidth(295.0f);
        /////////////////////////////////////////////////////////
        pack();
        moveTo(screenBounds / 2.0f - rect().center());
        setVisible(true);
    }


    virtual void render(RenderDevice* rd) const {
        GuiWindow::render(rd);

        // Keep our display in sync with the original one when a GUI control changes
        m_textureBox->setSettings(m_settings);

        // Update the xy/uv/rgba labels
        const shared_ptr<Texture>& tex = m_textureBox->texture();
        float w = 1, h = 1;
        if (tex) {
            w = float(tex->width());
            h = float(tex->height());
        }

        // Render child controls so that they slide under the canvas
        m_xyLabel->setCaption(format("(%d, %d)", m_textureBox->m_readbackXY.x, m_textureBox->m_readbackXY.y));
        const float u = m_textureBox->m_readbackXY.x / w;
        const float v = m_textureBox->m_readbackXY.y / h;

        if ((tex->dimension() == Texture::DIM_CUBE_MAP) ||
            (tex->dimension() == Texture::DIM_CUBE_MAP_ARRAY)) {
            Vector3 xyz = uvToXYZ(u, v);
            m_xyzLabel->setCaption(format("(%6.4f, %6.4f, %6.4f)", xyz.x, xyz.y, xyz.z));
        } else {
            m_uvLabel->setCaption(format("(%6.4f, %6.4f)", u, v));
        }
        m_rgbaLabel->setCaption(format("(%6.4f, %6.4f, %6.4f, %6.4f)", m_textureBox->m_texel.r, 
                        m_textureBox->m_texel.g, m_textureBox->m_texel.b, m_textureBox->m_texel.a));
        const Color4unorm8 c(m_textureBox->m_texel);
        m_ARGBLabel->setCaption(format("0x%02x%02x%02x%02x", c.a.bits(), c.r.bits(), c.g.bits(), c.b.bits()));
    }


    virtual bool onEvent(const GEvent& event) {
        if (GuiWindow::onEvent(event)) {
            return true;
        }
        
        switch (event.type) {
        case GEventType::KEY_DOWN:
            if (event.key.keysym.sym == GKey::ESCAPE) {
                // Cancel this window
                manager()->remove(dynamic_pointer_cast<GuiTextureBoxInspector>(shared_from_this()));
                return true;
            }
            break;

        case GEventType::GUI_ACTION:
            if ((event.gui.control == m_modeDropDownList) && (m_modeDropDownList->selectedIndex() > 0)) {
                String preset = m_modeDropDownList->selectedValue().text();
                if (preset == "sRGB Image") {
                    m_settings = Texture::Visualization::sRGB();
                } else if (preset == "Radiance") {
                    // Choose the maximum value
                    m_settings = Texture::Visualization::defaults();
                    shared_ptr<Texture> tex = m_textureBox->texture();
                    if (tex) {
                        Color4 max = tex->max();
                        if (max.isFinite()) {
                            m_settings.max = G3D::max(max.r, max.g, max.b);
                        }
                    }
                } else if (preset == "Reflectivity") {
                    m_settings = Texture::Visualization::defaults();
                } else if (preset == "8-bit Vector") {
                    m_settings = Texture::Visualization::packedUnitVector();
                } else if (preset == "Float Vector") {
                    m_settings = Texture::Visualization::unitVector();
                } else if (preset == "Depth Buffer") {
                    m_settings = Texture::Visualization::depthBuffer();
                } else if (preset == "Bump Map (in Alpha)") {
                    m_settings = Texture::Visualization::bumpInAlpha();
                } else if (preset == "Texture Coordinates") {
                    m_settings = Texture::Visualization::textureCoordinates();
                } else if (preset == "2D Motion Vectors") {
                    m_settings = Texture::Visualization::motionVectors();
                }

                // Switch back to <click to load>
                m_modeDropDownList->setSelectedIndex(0);
                return true;
            } 
            break;

        default:;
        }

        return false;
    }
};


shared_ptr<Texture> GuiTextureBox::applyProcessing() const {
    // save code
    const bool generateMipMaps = false;
    Vector2int32 scale(1,1);
    if ((m_texture->dimension() == Texture::DIM_CUBE_MAP) ||
        (m_texture->dimension() == Texture::DIM_CUBE_MAP_ARRAY)) {
        // Stretch cubemaps appropriately
        scale = Vector2int32(4, 2);
    }

    const shared_ptr<Framebuffer>& fb = Framebuffer::create(Texture::createEmpty("GuiTextureBox: save", m_texture->width() * scale.x, m_texture->height() * scale.y, ImageFormat::RGB8(), Texture::DIM_2D, generateMipMaps));
    
    // Create the preview image
    RenderDevice* rd = RenderDevice::current;
    rd->push2D(fb); {
        rd->setColorClearValue(Color3::white());
        rd->clear();
        drawTexture(rd, rd->viewport());
    }  rd->pop2D();

    return fb->texture(0);
}


void GuiTextureBox::save() {
    if (notNull(m_app)) {
        const String& savePath = FilePath::canonicalize(m_app->screenCapture()->getNextFilenameBase() + "_" + FilePath::makeLegalFilename(m_texture->caption()) + ".png");
        const shared_ptr<Texture>& output = applyProcessing();

        // Save to a temporary file. ScreenCapture will delete or rename it.
        // This allows unifying the video and image saving routines.

        output->toImage()->save(savePath);
        m_app->screenCapture()->saveCaptureAs(savePath, "Save Texture Visualization", output, false, m_texture->caption());
    }
}


void GuiTextureBox::rawSave() {
    if (notNull(m_app)) {
        const String& savePath = FilePath::canonicalize(m_app->screenCapture()->getNextFilenameBase() + "_" + FilePath::makeLegalFilename(m_texture->name()) + ((m_texture->format()->redBits == 8) ? ".png" : ".exr"));
        const ImageFormat* exportFormat = m_texture->format();
        if ((exportFormat != ImageFormat::RGBA8()) && (exportFormat != ImageFormat::RGB8())) {
            switch (exportFormat->numComponents) {
            case 4: exportFormat = ImageFormat::RGBA32F(); break;
            case 3: case 2: exportFormat = ImageFormat::RGB32F(); break;
            case 1: exportFormat = ImageFormat::R32F(); break;
            }
        }
        m_texture->toImage(exportFormat)->save(savePath);
        m_app->screenCapture()->saveCaptureAs(savePath, "Save Raw Texture Data", m_texture, false, m_texture->name());
    }
}


void GuiTextureBox::setSizeFromInterior(const Vector2& dims) {
    // Find out how big the canvas inset is
    const Rect2D& big = Rect2D::xywh(0, 0, 100, 100);

    // Get the canvas bounds
    const Rect2D& small = theme()->canvasToClientBounds(canvasRect(big), m_captionHeight);
    
    // Offset is now big - small
    setSize(dims + big.wh() - small.wh() + Vector2(BORDER, BORDER) * 2.0f);
}


bool GuiTextureBox::onEvent(const GEvent& event) {
    if (! m_visible || ! m_enabled) {
        return false;
    } else if (GuiContainer::onEvent(event)) {
        // Event was handled by base class
        return true;
    } else if ((event.type == GEventType::MOUSE_BUTTON_DOWN) && 
        m_clipBounds.contains(Vector2(event.button.x, event.button.y))) {
        if (m_embeddedMode) {
            m_dragStart = Vector2(event.button.x, event.button.y);
            m_dragging = true;
            m_offsetAtDragStart = m_offset;
        } else {
            showInspector();
        }
        return true;

    } else if (event.type == GEventType::MOUSE_BUTTON_UP) {

        // Stop drag
        m_dragging = false;
        return (m_clipBounds.contains(Vector2(event.button.x, event.button.y)));

    } else if (event.type == GEventType::MOUSE_MOTION) {
        if (m_dragging) {
            Vector2 mouse(event.mousePosition());
            
            // Move point, clamping adjacents        
            Vector2 delta = mouse - m_dragStart;

            // Hide weird mouse event delivery
            if (delta.squaredLength() < square(rect().width() + rect().height())) {
                m_offset = m_offsetAtDragStart + delta / m_zoom;
                return true;
            }
        } 
    }

    return false;
}


void GuiTextureBox::setMovieRecording(bool start) {
    alwaysAssertM(notNull(m_app), "App is null");

    const shared_ptr<Texture>& frame = applyProcessing();
    if (start) {
        debugAssert(isNull(m_videoOutput));
        VideoOutput::Settings settings;
        settings.width = frame->width();
        settings.height = frame->height();
        settings.fps = 30;
        settings.setBitrateQuality(1.0f);
        const String& savePath = m_app->screenCapture()->getNextFilenameBase() + "_" + FilePath::makeLegalFilename(m_texture->name()) + settings.encoder.extension;
        m_videoOutput = VideoOutput::create(savePath, settings);
        alwaysAssertM(notNull(m_videoOutput), "Failed to create VideoOutput");
        m_videoOutput->append(frame);

        // Tell the app that we need regular callback events to capture each frame
        m_recordWidget = CallbackWidget::create();
        m_recordWidget->beforeGraphicsCallback = [&]() {
            if (m_videoOutput) {
                m_videoOutput->append(applyProcessing());
            }
        };
        m_app->addWidget(m_recordWidget);
    } else {
        debugAssert(notNull(m_videoOutput));
        m_app->removeWidget(m_recordWidget);
        m_videoOutput->commit();
        m_app->screenCapture()->saveCaptureAs(m_videoOutput->filename(), "Save Video", frame, false, m_texture->name());
        m_videoOutput = nullptr;
        m_recordWidget = nullptr;
    }
}


void GuiTextureBox::setRect(const Rect2D& rect) {
    debugAssert(! rect.isEmpty());
    GuiContainer::setRect(rect);
    debugAssert(! m_clientRect.isEmpty());

    m_clipBounds = theme()->canvasToClientBounds(canvasRect(), m_captionHeight);
    m_clickRect = m_clipBounds;
}

Rect2D GuiTextureBox::canvasRect(const Rect2D& rect) const {
    return rect;
}


Rect2D GuiTextureBox::canvasRect() const {
    return canvasRect(m_rect);
}


void GuiTextureBox::showInspector() {
    shared_ptr<GuiWindow>     myWindow = dynamic_pointer_cast<GuiWindow>(window()->shared_from_this());
    shared_ptr<WidgetManager> manager  = dynamic_pointer_cast<WidgetManager>(myWindow->manager()->shared_from_this());

    shared_ptr<GuiTextureBoxInspector> ins(m_inspector.lock());
    if (isNull(ins)) {
        computeSizeString();
        ins.reset(new GuiTextureBoxInspector(m_texture->name() + " (" + m_lastSizeCaption.text() + ")", m_texture, m_settings, myWindow, m_app));
        m_inspector = ins;

        manager->add(ins);
    }

    manager->setFocusedWidget(ins);
}


void GuiTextureBox::setShaderArgs(UniformTable& args) {
    bool isCubemap = m_texture->dimension() == Texture::Dimension::DIM_CUBE_MAP ||
        m_texture->dimension() == Texture::Dimension::DIM_CUBE_MAP_ARRAY;

    args.setMacro("IS_GL_TEXTURE_RECTANGLE", m_texture->dimension() == Texture::DIM_2D_RECT ? 1 : 0);
    args.setMacro("IS_ARRAY", m_texture->dimension() == Texture::DIM_2D_ARRAY || m_texture->dimension() == Texture::DIM_CUBE_MAP_ARRAY);
    args.setMacro("IS_3D", m_texture->dimension() == Texture::DIM_3D);

    args.setMacro("DRAW_INVERTED", m_drawInverted);
    m_texture->setShaderArgs(args, "tex_", m_texture->hasMipMaps() ? Sampler::visualization() : Sampler::buffer());
    
    // Generate the correct gsamplerXX string for this texture type
    String samplerType = "sampler";
    switch (m_texture->dimension()) {
    case Texture::Dimension::DIM_2D:
    case Texture::Dimension::DIM_2D_RECT:
        samplerType += "2D";
        break;
    case Texture::Dimension::DIM_2D_ARRAY:
        samplerType += "2DArray";
        break;
    case Texture::Dimension::DIM_3D:
        samplerType += "3D";
        break;
    case Texture::Dimension::DIM_CUBE_MAP:
        samplerType += "Cube";
        break;
    case Texture::Dimension::DIM_CUBE_MAP_ARRAY:
        samplerType += "CubeArray";
        break;
    }
    Texture::TexelType tType = m_texture->texelType();
    if (tType == Texture::TexelType::INTEGER) {
        samplerType = "i" + samplerType;
    } else if (tType == Texture::TexelType::UNSIGNED_INTEGER) {
        samplerType = "u" + samplerType;
    }

    args.setMacro("SAMPLER_TYPE", samplerType);
    
    m_settings.setShaderArgs(args);

    if(isCubemap){
        if(m_showCubemapEdges){
            // TODO: come up with a principled way to make this 1 pixel wide
            float thresholdValue = 2.0f - 10.0f/m_texture->width();
            args.setUniform("edgeThreshold", thresholdValue);
        } else {
            args.setUniform("edgeThreshold", 3.0f); // Anything over 2.0 turns off edge rendering
        }
            
    }
    
}


void GuiTextureBox::drawTexture(RenderDevice* rd, const Rect2D& r) const {
    rd->setBlendFunc(RenderDevice::BLEND_ONE, RenderDevice::BLEND_ZERO);

    // If this is a depth texture, make sure we flip it into normal read mode.
 //   Texture::DepthReadMode oldReadMode = m_texture->settings().depthReadMode;
//    m_texture->setDepthReadMode(Texture::DEPTH_NORMAL);

    // The GuiTextureBox inspector can directly manipulate this value,
    // so it might not reflect the value we had at the last m_settings
    // call.
    const_cast<GuiTextureBox*>(this)->setSettings(m_settings);
    
    // Draw texture
    Args args;
    const_cast<GuiTextureBox*>(this)->setShaderArgs(args);
    args.setRect(r);
       
    if (m_texture->dimension() == Texture::DIM_CUBE_MAP ||
        m_texture->dimension() == Texture::DIM_CUBE_MAP_ARRAY) {
        LAUNCH_SHADER_WITH_HINT("GuiTextureBox_Cubemap.pix", args, m_texture->name());
    } else {
        LAUNCH_SHADER_WITH_HINT("GuiTextureBox_2D.pix", args, m_texture->name());
    }        
}


static void directionToCubemapFaceAndCoordinate(const Vector3& dir, CubeFace& face, Vector2& uv) {
    const Vector3& a = abs(dir);
    if (a.x >= a.y && a.x >= a.z) {
        face = (dir.x > 0) ? CubeFace::POS_X : CubeFace::NEG_X;
        uv = ((dir.zy() / dir.x) * 0.5f) + Vector2(0.5f, 0.5f);
        uv.x = 1.0f - uv.x;
        if (dir.x > 0) {
            uv.y = 1.0f - uv.y;
        }
    } else if (a.y >= a.x && a.y >= a.z) {
        // POS_Y Good
        face = (dir.y > 0) ? CubeFace::POS_Y : CubeFace::NEG_Y;
        uv = ((dir.xz() / dir.y) * 0.5f) + Vector2(0.5f, 0.5f);
        if (dir.y < 0) {
            uv.x = 1.0f - uv.x;
        }
    } else if (a.z >= a.x && a.z >= a.y) {
        // Good
        face = (dir.z > 0) ? CubeFace::POS_Z : CubeFace::NEG_Z;
        uv = ((dir.xy() / dir.z) * 0.5f) + Vector2(0.5f, 0.5f);
        if (dir.z > 0) {
            uv.y = 1.0f - uv.y;
        }
    } else {
        alwaysAssertM(false, "directionToCubemapFaceAndCoordinate() failed!");
    }
}

void GuiTextureBox::render(RenderDevice* rd, const shared_ptr<GuiTheme>& theme, bool ancestorsEnabled) const {
    if (! m_visible) {
        return;
    }  

    int w = 0;
    int h = 0;
    // Find the mouse position

    if (m_embeddedMode) {
        theme->renderCanvas(canvasRect(), m_enabled && ancestorsEnabled, focused(), m_caption, m_captionHeight);
    } else {
        //theme->renderButtonBorder(canvasRect(), mouseOver(), GuiTheme::TOOL_BUTTON_STYLE);
    }
    const CoordinateFrame& matrix = rd->objectToWorldMatrix();
    const float pixelScale = GLFWWindow::defaultGuiPixelScale();

    if (m_texture) {
        // Shrink by the border size to save space for the border,
        // and then draw the largest rect that we can fit inside.
        Rect2D r = m_texture->rect2DBounds();
        if (m_texture->dimension() == Texture::DIM_CUBE_MAP ||
            m_texture->dimension() == Texture::DIM_CUBE_MAP_ARRAY) {
            r = r * Vector2(2.0f, 1.0f);
        }
        r = r + (m_offset - r.center());
        r = r * m_zoom;
        r = r + m_clipBounds.center();
        
        theme->pauseRendering();
        {
            // Merge with existing clipping region!
            Rect2D oldClip = rd->clip2D();
            // Scissor region ignores transformation matrix
            Rect2D newClip = m_clipBounds * pixelScale + matrix.translation.xy();

            rd->setClip2D(oldClip.intersect(newClip));

            drawTexture(rd, r);

            if (m_texture) {
                w = m_texture->width();
                h = m_texture->height();
                GuiTheme::TextStyle style = theme->defaultStyle();
                const Color3& front = Color3::white();
                const Color3& back  = Color3::black();
                if (min(m_clipBounds.width(), m_clipBounds.height()) <= 128) {
                    style.size = 12;
                } else {
                    style.size = 14;
                }
                
                // Display coords and value when requested
                // (note that the manager may be null while we are waiting to be added)
                if (m_showInfo && (window()->manager() != nullptr) && (window()->window()->mouseHideCount() < 1)) {
                    // Find the mouse position
                    Vector2 mousePos;
                    uint8 ignore;
                    static const float scale = GLFWWindow::defaultGuiPixelScale();
                    window()->window()->getRelativeMouseState(mousePos, ignore);
                    mousePos /= scale;
                    // Make relative to the control
                    mousePos -= matrix.translation.xy();
                    if (m_clipBounds.contains(mousePos) && r.contains(mousePos)) {
                        mousePos -= r.x0y0();
                        // Convert to texture coordinates
                        mousePos *= Vector2((float)w, (float)h) / r.wh();
                        mousePos *= (1.0f / powf(2.0f, float(m_settings.mipLevel)));
                        //screenPrintf("w=%d h=%d", w, h);
                                
                        if (m_texture->dimension() == Texture::DIM_CUBE_MAP ||
                            m_texture->dimension() == Texture::DIM_CUBE_MAP_ARRAY) {
                            int ix = iFloor(mousePos.x);
                            int iy = iFloor(mousePos.y);
                            if (ix >= 0 && ix < w && iy >= 0 && iy < h) {
                                const Vector2 mipSize = m_texture->vector2Bounds() / powf(2.0f, float(m_settings.mipLevel));
                                const Vector2 uv = Vector2(ix + 0.5f, iy + 0.5f) / mipSize;
                                const Vector3& xyz = uvToXYZ(uv.x, uv.y);
                                CubeFace face;
                                Vector2 faceUV;
                                directionToCubemapFaceAndCoordinate(xyz, face, faceUV);
                                const int cubeIx = iFloor(faceUV.x * mipSize.x);
                                const int cubeIy = iFloor(faceUV.y * mipSize.y);
                                if (m_readbackXY.x != cubeIx || m_readbackXY.y != cubeIy) {
                                    m_readbackXY.x = cubeIx;
                                    m_readbackXY.y = cubeIy;
                                    m_texel = m_texture->readTexel(cubeIx, cubeIy, rd, m_settings.mipLevel, m_settings.layer, face);
                                }
                            }
                        } else {
                            int ix = iFloor(mousePos.x);
                            int iy = iFloor(mousePos.y);
                            if (ix >= 0 && ix < w && iy >= 0 && iy < h) {
                                if (m_readbackXY.x != ix || m_readbackXY.y != iy) {
                                    m_readbackXY.x = ix;
                                    m_readbackXY.y = iy;
                                    m_texel = m_texture->readTexel(ix, iy, rd, m_settings.mipLevel, m_settings.layer);
                                }
                            }
                        }
                           
                    }
                } 

                // Render the label
                computeSizeString();

                if (! m_embeddedMode) {
                    const float sizeHeight = theme->bounds(m_lastSizeCaption).y;
                    style.font->draw2D(rd, m_caption        , m_clipBounds.x0y1() - Vector2(-5, sizeHeight + theme->bounds(m_caption).y), style.size, front, back);
                    style.font->draw2D(rd, m_lastSizeCaption, m_clipBounds.x0y1() - Vector2(-5, sizeHeight), style.size, front, back);
                }

                //Draw::rect2DBorder(r, rd, Color3::black(), 0, BORDER);      

            }
        }
        theme->resumeRendering();      
    }
}


void GuiTextureBox::computeSizeString() const {
    const int w = m_texture->width();
    const int h = m_texture->height();
    const ImageFormat* fmt = m_texture->format();

    if ((m_lastSize.x != w) || (m_lastSize.y != h) || (fmt != m_lastFormat)) {
        m_lastSize.x = w;
        m_lastSize.y = h;
        m_lastFormat = fmt;

        // Avoid computing this every frame
        String s;
        if (w == h) {
            // Use ASCII squared character
            s = format("%d\xB2", w);
        } else {
            s = format("%dx%d", w, h);
        }

        s += " " + fmt->name();
        m_lastSizeCaption = GuiText(s, shared_ptr<GFont>(), 14, Color3::white(), Color3::black());
    }
}

#define ZOOM_FACTOR (sqrt(2.0f))

void GuiTextureBox::zoomIn() {
    m_zoom *= ZOOM_FACTOR;
}


void GuiTextureBox::zoomOut() {
    m_zoom /= ZOOM_FACTOR;
}


void GuiTextureBox::setViewZoom(float z) {
    m_zoom = z;
}


void GuiTextureBox::setViewOffset(const Vector2& x) {
    m_offset = x;
}


void GuiTextureBox::zoomToFit() {
    if (m_texture) {
        Vector2 w = m_texture->vector2Bounds();
        Rect2D r = m_clipBounds.expand(-BORDER).largestCenteredSubRect(w.x, w.y);
        m_zoom = r.width() / w.x;
        m_offset = Vector2::zero();
    } else {
        zoomTo1();
    }
}


void GuiTextureBox::zoomTo1() {
    m_zoom = 1.0;
    m_offset = Vector2::zero();
}
    

void GuiTextureBox::findControlUnderMouse(Vector2 mouse, GuiControl*& control) {
    if (! m_enabled || ! m_rect.contains(mouse) || ! m_visible) {
        return;
    }

    control = this;
    mouse -= m_clientRect.x0y0();

}

void GuiTextureBox::setTexture(const shared_ptr<Texture>& t, bool drawInverted) {
    m_drawInverted = drawInverted;
    setTexture(t);
}
    
void GuiTextureBox::setTexture(const shared_ptr<Texture>& t) {
    if (m_texture == t) {
        // Setting back to the same texture
        return;
    }

    const bool firstTime = isNull(m_texture);

    m_texture = t;
    shared_ptr<GuiTextureBoxInspector> ins = m_inspector.lock();
    if (ins) {
        // The inspector now has the wrong texture in it and it would require a 
        // lot of GUI changes to update it, so we simply close that window.
        window()->manager()->remove(ins);
    }
    if (t) {
        setSettings(t->visualization);

        if (firstTime) {
            zoomToFit();
        }
    }
}

void GuiTextureBox::setCaption(const GuiText& text) {
    m_caption = text;
    setCaptionWidth(0);
    setCaptionHeight(0);
}

void GuiTextureBox::setSettings(const Texture::Visualization& s) {
    // Check the settings for this computer
    m_settings = s;
}



} // G3D

