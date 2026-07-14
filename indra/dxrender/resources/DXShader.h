#pragma once
#include <d3d11.h>
#include <string>

// Compiles + owns one program's D3D11 vertex/pixel shader pair. Unlike GL,
// there is no separate link step - LLGLSLShader::createShaderDX() hands this
// class one fully concatenated HLSL blob per stage (entry file text + all
// attached utility files' text) and this class compiles+creates the shader
// object directly.
class DXShader
{
public:
    ~DXShader() { reset(); }

    bool compileVertexShader(const std::string& source, const std::string& debugName);
    bool compilePixelShader(const std::string& source, const std::string& debugName);
    void reset();

    ID3D11VertexShader* getVS() const { return mVS; }
    ID3D11PixelShader* getPS() const { return mPS; }

    // Kept alive after compileVertexShader() - Milestone 3's DXVertexLayout
    // needs the exact VS bytecode to build a matching ID3D11InputLayout.
    ID3DBlob* getVSBytecode() const { return mVSBytecode; }

private:
    ID3D11VertexShader* mVS = nullptr;
    ID3D11PixelShader* mPS = nullptr;
    ID3DBlob* mVSBytecode = nullptr;
};
