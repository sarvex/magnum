/*
    This file is part of Magnum.

    Copyright © 2010, 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018, 2019,
                2020, 2021, 2022 Vladimír Vondruš <mosra@centrum.cz>
    Copyright © 2022 Vladislav Oleshko <vladislav.oleshko@gmail.com>

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

#include "VectorGL.h"

#include <Corrade/Containers/EnumSet.hpp>
#include <Corrade/Containers/Iterable.h>
#include <Corrade/Utility/Resource.h>

#include "Magnum/GL/Context.h"
#include "Magnum/GL/Extensions.h"
#include "Magnum/GL/Shader.h"
#include "Magnum/GL/Texture.h"
#include "Magnum/Math/Color.h"
#include "Magnum/Math/Matrix3.h"
#include "Magnum/Math/Matrix4.h"

#ifndef MAGNUM_TARGET_GLES2
#include <Corrade/Containers/String.h>
#include <Corrade/Utility/Format.h>

#include "Magnum/GL/Buffer.h"
#endif

#include "Magnum/Shaders/Implementation/CreateCompatibilityShader.h"

namespace Magnum { namespace Shaders {

using namespace Containers::Literals;

namespace {
    enum: Int { TextureUnit = 6 };

    #ifndef MAGNUM_TARGET_GLES2
    enum: Int {
        /* Not using the zero binding to avoid conflicts with
           ProjectionBufferBinding from other shaders which can likely stay
           bound to the same buffer for the whole time */
        TransformationProjectionBufferBinding = 1,
        DrawBufferBinding = 2,
        TextureTransformationBufferBinding = 3,
        MaterialBufferBinding = 4
    };
    #endif
}

template<UnsignedInt dimensions> typename VectorGL<dimensions>::CompileState VectorGL<dimensions>::compile(const Configuration& configuration) {
    #ifndef MAGNUM_TARGET_GLES2
    CORRADE_ASSERT(!(configuration.flags() >= Flag::UniformBuffers) || configuration.materialCount(),
        "Shaders::VectorGL: material count can't be zero", CompileState{NoCreate});
    CORRADE_ASSERT(!(configuration.flags() >= Flag::UniformBuffers) || configuration.drawCount(),
        "Shaders::VectorGL: draw count can't be zero", CompileState{NoCreate});
    #endif

    #ifndef MAGNUM_TARGET_GLES
    if(configuration.flags() >= Flag::UniformBuffers)
        MAGNUM_ASSERT_GL_EXTENSION_SUPPORTED(GL::Extensions::ARB::uniform_buffer_object);
    #endif
    #ifndef MAGNUM_TARGET_GLES2
    if(configuration.flags() >= Flag::MultiDraw) {
        #ifndef MAGNUM_TARGET_GLES
        MAGNUM_ASSERT_GL_EXTENSION_SUPPORTED(GL::Extensions::ARB::shader_draw_parameters);
        #elif !defined(MAGNUM_TARGET_WEBGL)
        MAGNUM_ASSERT_GL_EXTENSION_SUPPORTED(GL::Extensions::ANGLE::multi_draw);
        #else
        MAGNUM_ASSERT_GL_EXTENSION_SUPPORTED(GL::Extensions::WEBGL::multi_draw);
        #endif
    }
    #endif

    #ifdef MAGNUM_BUILD_STATIC
    /* Import resources on static build, if not already */
    if(!Utility::Resource::hasGroup("MagnumShadersGL"_s))
        importShaderResources();
    #endif
    Utility::Resource rs("MagnumShadersGL"_s);

    const GL::Context& context = GL::Context::current();

    #ifndef MAGNUM_TARGET_GLES
    const GL::Version version = context.supportedVersion({GL::Version::GL320, GL::Version::GL310, GL::Version::GL300, GL::Version::GL210});
    #else
    const GL::Version version = context.supportedVersion({GL::Version::GLES300, GL::Version::GLES200});
    #endif

    GL::Shader vert = Implementation::createCompatibilityShader(rs, version, GL::Shader::Type::Vertex);
    vert.addSource(configuration.flags() & Flag::TextureTransformation ? "#define TEXTURE_TRANSFORMATION\n"_s : ""_s)
        .addSource(dimensions == 2 ? "#define TWO_DIMENSIONS\n"_s : "#define THREE_DIMENSIONS\n"_s);
    #ifndef MAGNUM_TARGET_GLES2
    if(configuration.flags() >= Flag::UniformBuffers) {
        vert.addSource(Utility::format(
            "#define UNIFORM_BUFFERS\n"
            "#define DRAW_COUNT {}\n",
            configuration.drawCount()));
        vert.addSource(configuration.flags() >= Flag::MultiDraw ? "#define MULTI_DRAW\n"_s : ""_s);
    }
    #endif
    vert.addSource(rs.getString("generic.glsl"_s))
        .addSource(rs.getString("Vector.vert"_s))
        .submitCompile();

    GL::Shader frag = Implementation::createCompatibilityShader(rs, version, GL::Shader::Type::Fragment);
    #ifndef MAGNUM_TARGET_GLES2
    if(configuration.flags() >= Flag::UniformBuffers) {
        frag.addSource(Utility::format(
            "#define UNIFORM_BUFFERS\n"
            "#define DRAW_COUNT {}\n"
            "#define MATERIAL_COUNT {}\n",
            configuration.drawCount(),
            configuration.materialCount()));
        frag.addSource(configuration.flags() >= Flag::MultiDraw ? "#define MULTI_DRAW\n"_s : ""_s);
    }
    #endif
    frag.addSource(rs.getString("generic.glsl"_s))
        .addSource(rs.getString("Vector.frag"_s))
        .submitCompile();

    VectorGL out{NoInit};
    out._flags = configuration.flags();
    #ifndef MAGNUM_TARGET_GLES2
    out._materialCount = configuration.materialCount();
    out._drawCount = configuration.drawCount();
    #endif

    out.attachShaders({vert,  frag});

    /* ES3 has this done in the shader directly */
    #if !defined(MAGNUM_TARGET_GLES) || defined(MAGNUM_TARGET_GLES2)
    #ifndef MAGNUM_TARGET_GLES
    if(!context.isExtensionSupported<GL::Extensions::ARB::explicit_attrib_location>(version))
    #endif
    {
        out.bindAttributeLocation(Position::Location, "position"_s);
        out.bindAttributeLocation(TextureCoordinates::Location, "textureCoordinates"_s);
    }
    #endif

    out.submitLink();

    return CompileState{std::move(out), std::move(vert), std::move(frag)
        #ifndef MAGNUM_TARGET_GLES
        , version
        #endif
    };
}

template<UnsignedInt dimensions> typename VectorGL<dimensions>::CompileState VectorGL<dimensions>::compile() {
    return compile(Configuration{});
}

#ifdef MAGNUM_BUILD_DEPRECATED
template<UnsignedInt dimensions> typename VectorGL<dimensions>::CompileState VectorGL<dimensions>::compile(const Flags flags) {
    return compile(Configuration{}
        .setFlags(flags));
}

#ifndef MAGNUM_TARGET_GLES2
template<UnsignedInt dimensions> typename VectorGL<dimensions>::CompileState VectorGL<dimensions>::compile(const Flags flags, const UnsignedInt materialCount, const UnsignedInt drawCount) {
    return compile(Configuration{}
        .setFlags(flags)
        .setMaterialCount(materialCount)
        .setDrawCount(drawCount));
}
#endif
#endif

template<UnsignedInt dimensions> VectorGL<dimensions>::VectorGL(CompileState&& state): VectorGL{static_cast<VectorGL&&>(std::move(state))} {
    #ifdef CORRADE_GRACEFUL_ASSERT
    /* When graceful assertions fire from within compile(), we get a NoCreate'd
       CompileState. Exiting makes it possible to test the assert. */
    if(!id()) return;
    #endif

    CORRADE_INTERNAL_ASSERT_OUTPUT(checkLink({GL::Shader(state._vert), GL::Shader(state._frag)}));

    #ifndef MAGNUM_TARGET_GLES
    const GL::Context& context = GL::Context::current();
    if(!context.isExtensionSupported<GL::Extensions::ARB::explicit_uniform_location>(state._version))
    #endif
    {
        #ifndef MAGNUM_TARGET_GLES2
        if(_flags >= Flag::UniformBuffers) {
            if(_drawCount > 1) _drawOffsetUniform = uniformLocation("drawOffset"_s);
        } else
        #endif
        {
            _transformationProjectionMatrixUniform = uniformLocation("transformationProjectionMatrix"_s);
            if(_flags & Flag::TextureTransformation)
                _textureMatrixUniform = uniformLocation("textureMatrix"_s);
            _backgroundColorUniform = uniformLocation("backgroundColor"_s);
            _colorUniform = uniformLocation("color"_s);
        }
    }

    #ifndef MAGNUM_TARGET_GLES
    if(!context.isExtensionSupported<GL::Extensions::ARB::shading_language_420pack>(state._version))
    #endif
    {
        setUniform(uniformLocation("vectorTexture"_s), TextureUnit);
        #ifndef MAGNUM_TARGET_GLES2
        if(_flags >= Flag::UniformBuffers) {
            setUniformBlockBinding(uniformBlockIndex("TransformationProjection"_s), TransformationProjectionBufferBinding);
            setUniformBlockBinding(uniformBlockIndex("Draw"_s), DrawBufferBinding);
            if(_flags & Flag::TextureTransformation)
                setUniformBlockBinding(uniformBlockIndex("TextureTransformation"_s), TextureTransformationBufferBinding);
            setUniformBlockBinding(uniformBlockIndex("Material"_s), MaterialBufferBinding);
        }
        #endif
    }

    /* Set defaults in OpenGL ES (for desktop they are set in shader code itself) */
    #ifdef MAGNUM_TARGET_GLES
    #ifndef MAGNUM_TARGET_GLES2
    if(_flags >= Flag::UniformBuffers) {
        /* Draw offset is zero by default */
    } else
    #endif
    {
        setTransformationProjectionMatrix(MatrixTypeFor<dimensions, Float>{Math::IdentityInit});
        if(_flags & Flag::TextureTransformation)
            setTextureMatrix(Matrix3{Math::IdentityInit});
        /* Background color is zero by default */
        setColor(Color4{1.0f});
    }
    #endif
}

template<UnsignedInt dimensions> VectorGL<dimensions>::VectorGL(const Configuration& configuration): VectorGL{compile(configuration)} {}

template<UnsignedInt dimensions> VectorGL<dimensions>::VectorGL(): VectorGL{Configuration{}} {}

#ifdef MAGNUM_BUILD_DEPRECATED
template<UnsignedInt dimensions> VectorGL<dimensions>::VectorGL(const Flags flags): VectorGL{compile(Configuration{}
    .setFlags(flags))} {}

#ifndef MAGNUM_TARGET_GLES2
template<UnsignedInt dimensions> VectorGL<dimensions>::VectorGL(const Flags flags, const UnsignedInt materialCount, const UnsignedInt drawCount): VectorGL{compile(Configuration{}
    .setFlags(flags)
    .setMaterialCount(materialCount)
    .setDrawCount(drawCount))} {}
#endif
#endif

template<UnsignedInt dimensions> VectorGL<dimensions>::VectorGL(NoInitT) {}

template<UnsignedInt dimensions> VectorGL<dimensions>& VectorGL<dimensions>::setTransformationProjectionMatrix(const MatrixTypeFor<dimensions, Float>& matrix) {
    #ifndef MAGNUM_TARGET_GLES2
    CORRADE_ASSERT(!(_flags >= Flag::UniformBuffers),
        "Shaders::VectorGL::setTransformationProjectionMatrix(): the shader was created with uniform buffers enabled", *this);
    #endif
    setUniform(_transformationProjectionMatrixUniform, matrix);
    return *this;
}

template<UnsignedInt dimensions> VectorGL<dimensions>& VectorGL<dimensions>::setTextureMatrix(const Matrix3& matrix) {
    #ifndef MAGNUM_TARGET_GLES2
    CORRADE_ASSERT(!(_flags >= Flag::UniformBuffers),
        "Shaders::VectorGL::setTextureMatrix(): the shader was created with uniform buffers enabled", *this);
    #endif
    CORRADE_ASSERT(_flags & Flag::TextureTransformation,
        "Shaders::VectorGL::setTextureMatrix(): the shader was not created with texture transformation enabled", *this);
    setUniform(_textureMatrixUniform, matrix);
    return *this;
}

template<UnsignedInt dimensions> VectorGL<dimensions>& VectorGL<dimensions>::setBackgroundColor(const Color4& color) {
    #ifndef MAGNUM_TARGET_GLES2
    CORRADE_ASSERT(!(_flags >= Flag::UniformBuffers),
        "Shaders::VectorGL::setBackgroundColor(): the shader was created with uniform buffers enabled", *this);
    #endif
    setUniform(_backgroundColorUniform, color);
    return *this;
}

template<UnsignedInt dimensions> VectorGL<dimensions>& VectorGL<dimensions>::setColor(const Color4& color) {
    #ifndef MAGNUM_TARGET_GLES2
    CORRADE_ASSERT(!(_flags >= Flag::UniformBuffers),
        "Shaders::VectorGL::setColor(): the shader was created with uniform buffers enabled", *this);
    #endif
    setUniform(_colorUniform, color);
    return *this;
}

#ifndef MAGNUM_TARGET_GLES2
template<UnsignedInt dimensions> VectorGL<dimensions>& VectorGL<dimensions>::setDrawOffset(const UnsignedInt offset) {
    CORRADE_ASSERT(_flags >= Flag::UniformBuffers,
        "Shaders::VectorGL::setDrawOffset(): the shader was not created with uniform buffers enabled", *this);
    CORRADE_ASSERT(offset < _drawCount,
        "Shaders::VectorGL::setDrawOffset(): draw offset" << offset << "is out of bounds for" << _drawCount << "draws", *this);
    if(_drawCount > 1) setUniform(_drawOffsetUniform, offset);
    return *this;
}

template<UnsignedInt dimensions> VectorGL<dimensions>& VectorGL<dimensions>::bindTransformationProjectionBuffer(GL::Buffer& buffer) {
    CORRADE_ASSERT(_flags >= Flag::UniformBuffers,
        "Shaders::VectorGL::bindTransformationProjectionBuffer(): the shader was not created with uniform buffers enabled", *this);
    buffer.bind(GL::Buffer::Target::Uniform, TransformationProjectionBufferBinding);
    return *this;
}

template<UnsignedInt dimensions> VectorGL<dimensions>& VectorGL<dimensions>::bindTransformationProjectionBuffer(GL::Buffer& buffer, const GLintptr offset, const GLsizeiptr size) {
    CORRADE_ASSERT(_flags >= Flag::UniformBuffers,
        "Shaders::VectorGL::bindTransformationProjectionBuffer(): the shader was not created with uniform buffers enabled", *this);
    buffer.bind(GL::Buffer::Target::Uniform, TransformationProjectionBufferBinding, offset, size);
    return *this;
}

template<UnsignedInt dimensions> VectorGL<dimensions>& VectorGL<dimensions>::bindDrawBuffer(GL::Buffer& buffer) {
    CORRADE_ASSERT(_flags >= Flag::UniformBuffers,
        "Shaders::VectorGL::bindDrawBuffer(): the shader was not created with uniform buffers enabled", *this);
    buffer.bind(GL::Buffer::Target::Uniform, DrawBufferBinding);
    return *this;
}

template<UnsignedInt dimensions> VectorGL<dimensions>& VectorGL<dimensions>::bindDrawBuffer(GL::Buffer& buffer, const GLintptr offset, const GLsizeiptr size) {
    CORRADE_ASSERT(_flags >= Flag::UniformBuffers,
        "Shaders::VectorGL::bindDrawBuffer(): the shader was not created with uniform buffers enabled", *this);
    buffer.bind(GL::Buffer::Target::Uniform, DrawBufferBinding, offset, size);
    return *this;
}

template<UnsignedInt dimensions> VectorGL<dimensions>& VectorGL<dimensions>::bindTextureTransformationBuffer(GL::Buffer& buffer) {
    CORRADE_ASSERT(_flags >= Flag::UniformBuffers,
        "Shaders::VectorGL::bindTextureTransformationBuffer(): the shader was not created with uniform buffers enabled", *this);
    CORRADE_ASSERT(_flags & Flag::TextureTransformation,
        "Shaders::VectorGL::bindTextureTransformationBuffer(): the shader was not created with texture transformation enabled", *this);
    buffer.bind(GL::Buffer::Target::Uniform, TextureTransformationBufferBinding);
    return *this;
}

template<UnsignedInt dimensions> VectorGL<dimensions>& VectorGL<dimensions>::bindTextureTransformationBuffer(GL::Buffer& buffer, const GLintptr offset, const GLsizeiptr size) {
    CORRADE_ASSERT(_flags >= Flag::UniformBuffers,
        "Shaders::VectorGL::bindTextureTransformationBuffer(): the shader was not created with uniform buffers enabled", *this);
    CORRADE_ASSERT(_flags & Flag::TextureTransformation,
        "Shaders::VectorGL::bindTextureTransformationBuffer(): the shader was not created with texture transformation enabled", *this);
    buffer.bind(GL::Buffer::Target::Uniform, TextureTransformationBufferBinding, offset, size);
    return *this;
}

template<UnsignedInt dimensions> VectorGL<dimensions>& VectorGL<dimensions>::bindMaterialBuffer(GL::Buffer& buffer) {
    CORRADE_ASSERT(_flags >= Flag::UniformBuffers,
        "Shaders::VectorGL::bindMaterialBuffer(): the shader was not created with uniform buffers enabled", *this);
    buffer.bind(GL::Buffer::Target::Uniform, MaterialBufferBinding);
    return *this;
}

template<UnsignedInt dimensions> VectorGL<dimensions>& VectorGL<dimensions>::bindMaterialBuffer(GL::Buffer& buffer, const GLintptr offset, const GLsizeiptr size) {
    CORRADE_ASSERT(_flags >= Flag::UniformBuffers,
        "Shaders::VectorGL::bindMaterialBuffer(): the shader was not created with uniform buffers enabled", *this);
    buffer.bind(GL::Buffer::Target::Uniform, MaterialBufferBinding, offset, size);
    return *this;
}
#endif

template<UnsignedInt dimensions> VectorGL<dimensions>& VectorGL<dimensions>::bindVectorTexture(GL::Texture2D& texture) {
    texture.bind(TextureUnit);
    return *this;
}

template class MAGNUM_SHADERS_EXPORT VectorGL<2>;
template class MAGNUM_SHADERS_EXPORT VectorGL<3>;

namespace Implementation {

Debug& operator<<(Debug& debug, const VectorGLFlag value) {
    debug << "Shaders::VectorGL::Flag" << Debug::nospace;

    switch(value) {
        /* LCOV_EXCL_START */
        #define _c(v) case VectorGLFlag::v: return debug << "::" #v;
        _c(TextureTransformation)
        #ifndef MAGNUM_TARGET_GLES2
        _c(UniformBuffers)
        _c(MultiDraw)
        #endif
        #undef _c
        /* LCOV_EXCL_STOP */
    }

    return debug << "(" << Debug::nospace << reinterpret_cast<void*>(UnsignedByte(value)) << Debug::nospace << ")";
}

Debug& operator<<(Debug& debug, const VectorGLFlags value) {
    return Containers::enumSetDebugOutput(debug, value, "Shaders::VectorGL::Flags{}", {
        VectorGLFlag::TextureTransformation,
        #ifndef MAGNUM_TARGET_GLES2
        VectorGLFlag::MultiDraw, /* Superset of UniformBuffers */
        VectorGLFlag::UniformBuffers
        #endif
    });
}

}

}}
