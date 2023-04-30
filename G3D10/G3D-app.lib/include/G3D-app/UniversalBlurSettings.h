/*
    Added by Hugo Palisson
*/

#ifndef GLG3D_UniversalBlurSettings_h
#define GLG3D_UniversalBlurSettings_h

#include "G3D-base/platform.h"
#include "G3D-app/GBuffer.h"
#include "G3D-app/DepthOfFieldSettings.h"
#include "G3D-app/MotionBlurSettings.h"

namespace G3D {

    //class Any;

    /** \see Camera, MotionBlur, DepthOfFieldSettings */
    class UniversalBlurSettings {
    private:
        bool                        m_enabled;
        bool                        m_MbAlgorithm;

    public:

        //UniversalBlurSettings();

        //UniversalBlurSettings(const Any&);

        //Any toAny() const;

        UniversalBlurSettings() :
            m_enabled(false), 
            m_MbAlgorithm(true)
        {}

        bool enabled() const {
            return m_enabled;
        }

        void setEnabled(bool e) {
            m_enabled = e;
        }

        bool MbAlgorithm() const {
            return m_MbAlgorithm;
        }

        void setMbAlgorithm(bool e) {
            m_MbAlgorithm = e;
        }

    };

}

#endif // GLG3D_UniversalBlurSettings_h
