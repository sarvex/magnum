#ifndef Magnum_Renderbuffer_h
#define Magnum_Renderbuffer_h
/*
    This file is part of Magnum.

    Copyright © 2010, 2011, 2012, 2013 Vladimír Vondruš <mosra@centrum.cz>

    Permission is hereby granted, free of charge, to any person obtaining a
    copy of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom the
    Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
*/

/** @file
 * @brief Class Magnum::Renderbuffer
 */

#include "Magnum.h"
#include "OpenGL.h"
#include "magnumVisibility.h"

namespace Magnum {

/**
@brief %Renderbuffer

Attachable to framebuffer as render target, see Framebuffer documentation
for more information.

@section Renderbuffer-performance-optimization Performance optimizations

The engine tracks currently bound renderbuffer to avoid unnecessary calls to
@fn_gl{BindRenderbuffer} in setStorage().

If extension @extension{EXT,direct_state_access} is available, function
setStorage() uses DSA to avoid unnecessary calls to @fn_gl{BindFramebuffer}.
See its documentation for more information.

@requires_gl30 %Extension @extension{EXT,framebuffer_object}
*/
class MAGNUM_EXPORT Renderbuffer {
    friend class Context;

    Renderbuffer(const Renderbuffer&) = delete;
    Renderbuffer(Renderbuffer&&) = delete;
    Renderbuffer& operator=(const Renderbuffer&) = delete;
    Renderbuffer& operator=(Renderbuffer&&) = delete;

    public:
        /**
         * @brief Constructor
         *
         * Generates new OpenGL renderbuffer.
         * @see @fn_gl{GenRenderbuffers}
         */
        inline explicit Renderbuffer() {
            glGenRenderbuffers(1, &_id);
        }

        /**
         * @brief Destructor
         *
         * Deletes associated OpenGL renderbuffer.
         * @see @fn_gl{DeleteRenderbuffers}
         */
        ~Renderbuffer();

        /** @brief OpenGL internal renderbuffer ID */
        inline GLuint id() const { return _id; }

        /**
         * @brief Set renderbuffer storage
         * @param internalFormat    Internal format
         * @param size              Renderbuffer size
         *
         * If @extension{EXT,direct_state_access} is not available and the
         * framebufferbuffer is not currently bound, it is bound before the
         * operation.
         * @see @fn_gl{BindRenderbuffer}, @fn_gl{RenderbufferStorage} or
         *      @fn_gl_extension{NamedRenderbufferStorage,EXT,direct_state_access}
         */
        inline void setStorage(RenderbufferFormat internalFormat, const Vector2i& size) {
            (this->*storageImplementation)(internalFormat, size);
        }

    private:
        static void MAGNUM_LOCAL initializeContextBasedFunctionality(Context* context);

        typedef void(Renderbuffer::*StorageImplementation)(RenderbufferFormat, const Vector2i&);
        void MAGNUM_LOCAL storageImplementationDefault(RenderbufferFormat internalFormat, const Vector2i& size);
        #ifndef MAGNUM_TARGET_GLES
        void MAGNUM_LOCAL storageImplementationDSA(RenderbufferFormat internalFormat, const Vector2i& size);
        #endif
        static StorageImplementation storageImplementation;

        void MAGNUM_LOCAL bind();

        GLuint _id;
};

}

#endif
