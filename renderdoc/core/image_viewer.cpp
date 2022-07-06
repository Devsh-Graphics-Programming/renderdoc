/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2019-2022 Baldur Karlsson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include "common/dds_readwrite.h"
#include "common/formatting.h"
#include "core/core.h"
#include "maths/formatpacking.h"
#include "replay/dummy_driver.h"
#include "replay/replay_driver.h"
#include "serialise/rdcfile.h"
#include "stb/stb_image.h"
#include "strings/string_utils.h"
#include "tinyexr/tinyexr.h"

class ImageViewer : public IReplayDriver
{
public:
  ImageViewer(IReplayDriver *proxy, const char *filename)
      : m_Proxy(proxy), m_Filename(filename), m_TextureID()
  {
    m_File = new SDFile;

    // start with props so that m_Props.localRenderer is correct
    m_Props = m_Proxy->GetAPIProperties();
    m_Props.pipelineType = GraphicsAPI::D3D11;
    m_Props.degraded = false;

    m_FrameRecord.frameInfo.fileOffset = 0;
    m_FrameRecord.frameInfo.frameNumber = 1;
    RDCEraseEl(m_FrameRecord.frameInfo.stats);

    m_FrameRecord.actionList.resize(1);
    ActionDescription &action = m_FrameRecord.actionList[0];
    action.actionId = 1;
    action.eventId = 1;
    action.customName = get_basename(filename);
    APIEvent ev;
    ev.eventId = 1;
    action.events.push_back(ev);

    SDChunk *chunk = new SDChunk(action.customName);
    chunk->AddAndOwnChild(makeSDString("path"_lit, filename));

    m_File->chunks.push_back(chunk);

    RefreshFile();

    m_Resources.push_back(ResourceDescription());
    m_Resources[0].resourceId = m_TextureID;
    m_Resources[0].autogeneratedName = false;
    m_Resources[0].name = get_basename(m_Filename);
  }

  virtual ~ImageViewer()
  {
    SAFE_DELETE(m_File);
    if(m_Proxy)
    {
      m_Proxy->Shutdown();
      m_Proxy = NULL;
    }
  }

  bool IsRemoteProxy() { return true; }
  RDResult FatalErrorCheck()
  {
    if(m_Error != ResultCode::Succeeded)
      return m_Error;

    // check for errors on the underlying proxy driver
    return m_Proxy->FatalErrorCheck();
  }
  IReplayDriver *MakeDummyDriver()
  {
    IReplayDriver *ret = new DummyDriver(this, {}, m_File);
    // lose our structured file reference
    m_File = NULL;
    return ret;
  }
  void Shutdown() { delete this; }
  // pass through necessary operations to proxy
  rdcarray<WindowingSystem> GetSupportedWindowSystems()
  {
    return m_Proxy->GetSupportedWindowSystems();
  }
  AMDRGPControl *GetRGPControl() { return NULL; }
  uint64_t MakeOutputWindow(WindowingData window, bool depth)
  {
    return m_Proxy->MakeOutputWindow(window, depth);
  }
  void DestroyOutputWindow(uint64_t id) { m_Proxy->DestroyOutputWindow(id); }
  bool CheckResizeOutputWindow(uint64_t id) { return m_Proxy->CheckResizeOutputWindow(id); }
  void SetOutputWindowDimensions(uint64_t id, int32_t w, int32_t h)
  {
    m_Proxy->SetOutputWindowDimensions(id, w, h);
  }
  void GetOutputWindowDimensions(uint64_t id, int32_t &w, int32_t &h)
  {
    m_Proxy->GetOutputWindowDimensions(id, w, h);
  }
  void GetOutputWindowData(uint64_t id, bytebuf &retData)
  {
    m_Proxy->GetOutputWindowData(id, retData);
  }
  void ClearOutputWindowColor(uint64_t id, FloatVector col)
  {
    m_Proxy->ClearOutputWindowColor(id, col);
  }
  void ClearOutputWindowDepth(uint64_t id, float depth, uint8_t stencil)
  {
    m_Proxy->ClearOutputWindowDepth(id, depth, stencil);
  }
  void BindOutputWindow(uint64_t id, bool depth) { m_Proxy->BindOutputWindow(id, depth); }
  bool IsOutputWindowVisible(uint64_t id) { return m_Proxy->IsOutputWindowVisible(id); }
  void FlipOutputWindow(uint64_t id) { m_Proxy->FlipOutputWindow(id); }
  void RenderCheckerboard(FloatVector dark, FloatVector light)
  {
    m_Proxy->RenderCheckerboard(dark, light);
  }
  void RenderHighlightBox(float w, float h, float scale)
  {
    m_Proxy->RenderHighlightBox(w, h, scale);
  }
  void PickPixel(ResourceId texture, uint32_t x, uint32_t y, const Subresource &sub,
                 CompType typeCast, float pixel[4])
  {
    if(m_Props.localRenderer == GraphicsAPI::OpenGL)
    {
      TextureDescription tex = m_Proxy->GetTexture(texture);
      uint32_t mipHeight = RDCMAX(1U, tex.height >> sub.mip);
      y = (mipHeight - 1) - y;
    }

    m_Proxy->PickPixel(texture, x, y, sub, typeCast, pixel);
  }
  bool GetMinMax(ResourceId texid, const Subresource &sub, CompType typeCast, float *minval,
                 float *maxval)
  {
    return m_Proxy->GetMinMax(m_TextureID, sub, typeCast, minval, maxval);
  }
  bool GetHistogram(ResourceId texid, const Subresource &sub, CompType typeCast, float minval,
                    float maxval, const rdcfixedarray<bool, 4> &channels,
                    rdcarray<uint32_t> &histogram)
  {
    return m_Proxy->GetHistogram(m_TextureID, sub, typeCast, minval, maxval, channels, histogram);
  }
  bool RenderTexture(TextureDisplay cfg)
  {
    if(cfg.resourceId != m_TextureID && cfg.resourceId != m_CustomTexID)
      cfg.resourceId = m_TextureID;

    if(m_Props.localRenderer == GraphicsAPI::OpenGL)
      cfg.flipY = !cfg.flipY;

    return m_Proxy->RenderTexture(cfg);
  }
  uint32_t PickVertex(uint32_t eventId, int32_t width, int32_t height, const MeshDisplay &cfg,
                      uint32_t x, uint32_t y)
  {
    return m_Proxy->PickVertex(eventId, width, height, cfg, x, y);
  }
  rdcarray<ShaderEncoding> GetTargetShaderEncodings()
  {
    return m_Proxy->GetTargetShaderEncodings();
  }
  rdcarray<ShaderEncoding> GetCustomShaderEncodings()
  {
    return m_Proxy->GetCustomShaderEncodings();
  }
  rdcarray<ShaderSourcePrefix> GetCustomShaderSourcePrefixes()
  {
    return m_Proxy->GetCustomShaderSourcePrefixes();
  }
  void SetCustomShaderIncludes(const rdcarray<rdcstr> &directories)
  {
    m_Proxy->SetCustomShaderIncludes(directories);
  }
  void BuildCustomShader(ShaderEncoding sourceEncoding, const bytebuf &source, const rdcstr &entry,
                         const ShaderCompileFlags &compileFlags, ShaderStage type, ResourceId &id,
                         rdcstr &errors)
  {
    m_Proxy->BuildCustomShader(sourceEncoding, source, entry, compileFlags, type, id, errors);
  }
  void FreeCustomShader(ResourceId id) { m_Proxy->FreeTargetResource(id); }
  ResourceId ApplyCustomShader(TextureDisplay &display)
  {
    m_CustomTexID = m_Proxy->ApplyCustomShader(display);
    return m_CustomTexID;
  }
  rdcarray<ResourceDescription> GetResources() { return m_Resources; }
  rdcarray<TextureDescription> GetTextures() { return {m_TexDetails}; }
  TextureDescription GetTexture(ResourceId id) { return m_TexDetails; }
  void GetTextureData(ResourceId tex, const Subresource &sub, const GetTextureDataParams &params,
                      bytebuf &data)
  {
    if(tex != m_TextureID && tex != m_CustomTexID)
      tex = m_TextureID;

    if(tex == m_TextureID && !m_RealTexData.empty() && params.remap == RemapTexture::NoRemap)
    {
      RDCASSERT(sub.sample == 0);
      uint32_t idx = sub.slice * m_TexDetails.mips + sub.mip;
      RDCASSERT(idx < m_RealTexData.size(), idx, m_RealTexData.size(), m_TexDetails.mips, sub.slice,
                sub.mip);
      data = m_RealTexData[idx];
      return;
    }

    m_Proxy->GetTextureData(tex, sub, params, data);
  }

  // handle a couple of operations ourselves to return a simple fake log
  APIProperties GetAPIProperties() { return m_Props; }
  FrameRecord GetFrameRecord() { return m_FrameRecord; }
  void SetPipelineStates(D3D11Pipe::State *d3d11, D3D12Pipe::State *d3d12, GLPipe::State *gl,
                         VKPipe::State *vk)
  {
    d3d11->outputMerger.renderTargets.resize(1);
    d3d11->outputMerger.renderTargets[0].resourceResourceId = m_TextureID;
    d3d11->outputMerger.renderTargets[0].viewFormat = m_TexDetails.format;
  }

  // other operations are dropped/ignored, to avoid confusion
  RDResult ReadLogInitialisation(RDCFile *rdc, bool storeStructuredBuffers)
  {
    return ResultCode::Succeeded;
  }
  SDFile *GetStructuredFile() { return m_File; }
  void RenderMesh(uint32_t eventId, const rdcarray<MeshFormat> &secondaryDraws, const MeshDisplay &cfg)
  {
  }
  rdcarray<BufferDescription> GetBuffers() { return {}; }
  rdcarray<DebugMessage> GetDebugMessages() { return rdcarray<DebugMessage>(); }
  BufferDescription GetBuffer(ResourceId id)
  {
    BufferDescription ret;
    RDCEraseEl(ret);
    return ret;
  }
  void SavePipelineState(uint32_t eventId) {}
  DriverInformation GetDriverInfo()
  {
    DriverInformation ret = {};
    return ret;
  }
  rdcarray<GPUDevice> GetAvailableGPUs() { return {}; }
  void ReplayLog(uint32_t endEventID, ReplayLogType replayType) {}
  rdcarray<uint32_t> GetPassEvents(uint32_t eventId) { return rdcarray<uint32_t>(); }
  rdcarray<EventUsage> GetUsage(ResourceId id) { return rdcarray<EventUsage>(); }
  bool IsRenderOutput(ResourceId id) { return false; }
  ResourceId GetLiveID(ResourceId id) { return id; }
  rdcarray<GPUCounter> EnumerateCounters() { return {}; }
  CounterDescription DescribeCounter(GPUCounter counterID)
  {
    CounterDescription desc = {};
    desc.counter = counterID;
    return desc;
  }
  rdcarray<CounterResult> FetchCounters(const rdcarray<GPUCounter> &counters) { return {}; }
  void FillCBufferVariables(ResourceId pipeline, ResourceId shader, ShaderStage stage,
                            rdcstr entryPoint, uint32_t cbufSlot, rdcarray<ShaderVariable> &outvars,
                            const bytebuf &data)
  {
  }
  void GetBufferData(ResourceId buff, uint64_t offset, uint64_t len, bytebuf &retData) {}
  void InitPostVSBuffers(uint32_t eventId) {}
  void InitPostVSBuffers(const rdcarray<uint32_t> &eventId) {}
  MeshFormat GetPostVSBuffers(uint32_t eventId, uint32_t instID, uint32_t viewID, MeshDataStage stage)
  {
    MeshFormat ret;
    RDCEraseEl(ret);
    return ret;
  }
  ResourceId RenderOverlay(ResourceId texid, FloatVector clearCol, DebugOverlay overlay,
                           uint32_t eventId, const rdcarray<uint32_t> &passEvents)
  {
    return ResourceId();
  }
  rdcarray<ShaderEntryPoint> GetShaderEntryPoints(ResourceId shader) { return {}; }
  ShaderReflection *GetShader(ResourceId pipeline, ResourceId shader, ShaderEntryPoint entry)
  {
    return NULL;
  }
  rdcarray<rdcstr> GetDisassemblyTargets(bool withPipeline) { return {"N/A"}; }
  rdcstr DisassembleShader(ResourceId pipeline, const ShaderReflection *refl, const rdcstr &target)
  {
    return "";
  }
  void FreeTargetResource(ResourceId id) {}
  rdcarray<PixelModification> PixelHistory(rdcarray<EventUsage> events, ResourceId target, uint32_t x,
                                           uint32_t y, const Subresource &sub, CompType typeCast)
  {
    return rdcarray<PixelModification>();
  }
  ShaderDebugTrace *DebugVertex(uint32_t eventId, uint32_t vertid, uint32_t instid, uint32_t idx,
                                uint32_t view)
  {
    return new ShaderDebugTrace();
  }
  ShaderDebugTrace *DebugPixel(uint32_t eventId, uint32_t x, uint32_t y, uint32_t sample,
                               uint32_t primitive)
  {
    return new ShaderDebugTrace();
  }
  ShaderDebugTrace *DebugThread(uint32_t eventId, const rdcfixedarray<uint32_t, 3> &groupid,
                                const rdcfixedarray<uint32_t, 3> &threadid)
  {
    return new ShaderDebugTrace();
  }
  rdcarray<ShaderDebugState> ContinueDebug(ShaderDebugger *debugger) { return {}; }
  void FreeDebugger(ShaderDebugger *debugger) { delete debugger; }
  void BuildTargetShader(ShaderEncoding sourceEncoding, const bytebuf &source, const rdcstr &entry,
                         const ShaderCompileFlags &compileFlags, ShaderStage type, ResourceId &id,
                         rdcstr &errors)
  {
    id = ResourceId();
    errors = "Building target shaders is unsupported";
  }
  void ReplaceResource(ResourceId from, ResourceId to) {}
  void RemoveReplacement(ResourceId id) {}
  // these are proxy functions, and will never be used
  ResourceId CreateProxyTexture(const TextureDescription &templateTex)
  {
    RDCERR("Calling proxy-render functions on an image viewer");
    return ResourceId();
  }

  void SetProxyTextureData(ResourceId texid, const Subresource &sub, byte *data, size_t dataSize)
  {
    RDCERR("Calling proxy-render functions on an image viewer");
  }
  bool IsTextureSupported(const TextureDescription &tex) { return true; }
  bool NeedRemapForFetch(const ResourceFormat &format) { return false; }
  ResourceId CreateProxyBuffer(const BufferDescription &templateBuf)
  {
    RDCERR("Calling proxy-render functions on an image viewer");
    return ResourceId();
  }

  void SetProxyBufferData(ResourceId bufid, byte *data, size_t dataSize)
  {
    RDCERR("Calling proxy-render functions on an image viewer");
  }

  void FileChanged() { RefreshFile(); }
private:
  void RefreshFile();
  void CreateProxyTexture(TextureDescription &texDetails, read_dds_data &read_data);

  APIProperties m_Props;
  FrameRecord m_FrameRecord;
  D3D11Pipe::State m_PipelineState;
  IReplayDriver *m_Proxy;
  rdcstr m_Filename;
  ResourceId m_TextureID, m_CustomTexID;
  rdcarray<ResourceDescription> m_Resources;
  SDFile *m_File;
  TextureDescription m_TexDetails;

  RDResult m_Error;

  // if we remapped the texture for display, this contains the real data to return from
  // GetTextureData()
  rdcarray<bytebuf> m_RealTexData;
};

RDResult IMG_CreateReplayDevice(RDCFile *rdc, IReplayDriver **driver)
{
  if(!rdc)
    return ResultCode::InvalidParameter;

  rdcstr filename;
  FILE *f = rdc->StealImageFileHandle(filename);

  if(!f)
  {
    RETURN_ERROR_RESULT(ResultCode::InvalidParameter,
                        "Trying to load invalid handle as image-capture");
  }

  byte headerBuffer[4];
  const size_t headerSize = FileIO::fread(headerBuffer, 1, 4, f);
  FileIO::fseek64(f, 0, SEEK_SET);

  // make sure the file is a type we recognise before going further
  if(is_exr_file(f))
  {
    FileIO::fseek64(f, 0, SEEK_END);
    uint64_t size = FileIO::ftell64(f);
    FileIO::fseek64(f, 0, SEEK_SET);

    bytebuf buffer;
    buffer.resize((size_t)size);

    FileIO::fread(&buffer[0], 1, buffer.size(), f);

    EXRVersion exrVersion;
    int ret = ParseEXRVersionFromMemory(&exrVersion, buffer.data(), buffer.size());

    if(ret != 0)
    {
      FileIO::fclose(f);
      RETURN_ERROR_RESULT(ResultCode::ImageUnsupported,
                          "EXR file detected, but couldn't load with ParseEXRVersionFromMemory: %d",
                          ret);
    }

    if(exrVersion.multipart)
    {
      FileIO::fclose(f);
      RETURN_ERROR_RESULT(ResultCode::ImageUnsupported,
                          "Unsupported EXR file detected - multipart EXR.");
    }

    if(exrVersion.non_image)
    {
      FileIO::fclose(f);
      RETURN_ERROR_RESULT(ResultCode::ImageUnsupported,
                          "Unsupported EXR file detected - deep image EXR.");
    }

    if(exrVersion.tiled)
    {
      FileIO::fclose(f);
      RETURN_ERROR_RESULT(ResultCode::ImageUnsupported,
                          "Unsupported EXR file detected - tiled EXR.");
    }

    EXRHeader exrHeader;
    InitEXRHeader(&exrHeader);

    rdcstr errString;

    {
      const char *err = NULL;

      ret = ParseEXRHeaderFromMemory(&exrHeader, &exrVersion, buffer.data(), buffer.size(), &err);

      if(err)
      {
        errString = err;
        free((void *)err);
      }
    }

    if(ret != 0)
    {
      FileIO::fclose(f);
      RETURN_ERROR_RESULT(
          ResultCode::ImageUnsupported,
          "EXR file detected, but couldn't load with ParseEXRHeaderFromMemory %d: '%s'", ret,
          errString.c_str());
    }
  }
  else if(stbi_is_hdr_from_file(f))
  {
    FileIO::fseek64(f, 0, SEEK_SET);

    int ignore = 0;
    float *data = stbi_loadf_from_file(f, &ignore, &ignore, &ignore, 4);

    if(!data)
    {
      FileIO::fclose(f);
      RETURN_ERROR_RESULT(ResultCode::ImageUnsupported,
                          "HDR file recognised, but couldn't load with stbi_loadf_from_file");
    }

    free(data);
  }
  else if(is_dds_file(headerBuffer, headerSize))
  {
    FileIO::fseek64(f, 0, SEEK_SET);
    StreamReader reader(f);
    read_dds_data read_data;
    RDResult res = load_dds_from_file(&reader, read_data);
    f = NULL;

    if(res != ResultCode::Succeeded)
      return res;
  }
  else
  {
    FileIO::fseek64(f, 0, SEEK_SET);

    int width = 0, height = 0;
    int ignore = 0;
    int ret = stbi_info_from_file(f, &width, &height, &ignore);

    // just in case (we shouldn't have come in here if this weren't true), make sure
    // the format is supported
    if(ret == 0)
    {
      FileIO::fclose(f);
      RETURN_ERROR_RESULT(ResultCode::ImageUnsupported, "Image can't be identified by stb");
    }

    if(width <= 0 || width >= 65536 || height <= 0 || height >= 65536)
    {
      FileIO::fclose(f);
      RETURN_ERROR_RESULT(ResultCode::ImageUnsupported, "Image dimensions %ux%u are not supported",
                          width, height);
    }

    byte *data = stbi_load_from_file(f, &ignore, &ignore, &ignore, 4);

    if(!data)
    {
      FileIO::fclose(f);
      RETURN_ERROR_RESULT(ResultCode::ImageUnsupported, "File recognised, but couldn't load image");
    }

    free(data);
  }

  if(f != NULL)
    FileIO::fclose(f);

  IReplayDriver *proxy = NULL;
  RDResult result = RenderDoc::Inst().CreateProxyReplayDriver(RDCDriver::Unknown, &proxy);

  if(result != ResultCode::Succeeded || !proxy)
  {
    RDCERR("Couldn't create replay driver to proxy-render images");

    if(proxy)
      proxy->Shutdown();
    return result;
  }

  *driver = new ImageViewer(proxy, filename.c_str());

  result = (*driver)->FatalErrorCheck();

  if(result != ResultCode::Succeeded)
  {
    (*driver)->Shutdown();
    return result;
  }

  return ResultCode::Succeeded;
}

void ImageViewer::RefreshFile()
{
  FILE *f = NULL;

  for(int attempt = 0; attempt < 10 && f == NULL; attempt++)
  {
    f = FileIO::fopen(m_Filename, FileIO::ReadBinary);
    if(f)
      break;
    Threading::Sleep(40);
  }

  if(!f)
  {
    SET_ERROR_RESULT(m_Error, ResultCode::FileIOFailed,
                     "Couldn't open %s! Is the file opened exclusively/locked elsewhere?",
                     m_Filename.c_str());
    return;
  }

  TextureDescription texDetails;

  ResourceFormat rgba8_unorm;
  rgba8_unorm.compByteWidth = 1;
  rgba8_unorm.compCount = 4;
  rgba8_unorm.compType = CompType::UNormSRGB;
  rgba8_unorm.type = ResourceFormatType::Regular;

  ResourceFormat rgba32_float = rgba8_unorm;
  rgba32_float.compByteWidth = 4;
  rgba32_float.compType = CompType::Float;

  texDetails.creationFlags = TextureCategory::ShaderRead | TextureCategory::ColorTarget;
  texDetails.cubemap = false;
  texDetails.resourceId = m_TextureID;
  texDetails.byteSize = 0;
  texDetails.msQual = 0;
  texDetails.msSamp = 1;
  texDetails.format = rgba8_unorm;

  // reasonable defaults
  texDetails.type = TextureType::Texture2D;
  texDetails.dimension = 2;
  texDetails.arraysize = 1;
  texDetails.width = 1;
  texDetails.height = 1;
  texDetails.depth = 1;
  texDetails.mips = 1;

  byte *data = NULL;
  size_t datasize = 0;

  bool dds = false;
  byte headerBuffer[4];
  const size_t headerSize = FileIO::fread(headerBuffer, 1, 4, f);

  FileIO::fseek64(f, 0, SEEK_END);
  uint64_t fileSize = FileIO::ftell64(f);
  FileIO::fseek64(f, 0, SEEK_SET);

  if(is_exr_file(f))
  {
    texDetails.format = rgba32_float;

    FileIO::fseek64(f, 0, SEEK_SET);

    bytebuf buffer;
    buffer.resize((size_t)fileSize);

    FileIO::fread(buffer.data(), 1, buffer.size(), f);

    EXRVersion exrVersion;
    int ret = ParseEXRVersionFromMemory(&exrVersion, buffer.data(), buffer.size());

    if(ret != 0)
    {
      SET_ERROR_RESULT(m_Error, ResultCode::ImageUnsupported,
                       "EXR file detected, but couldn't load with ParseEXRVersionFromMemory: %d",
                       ret);
      FileIO::fclose(f);
      return;
    }

    if(exrVersion.multipart)
    {
      SET_ERROR_RESULT(m_Error, ResultCode::ImageUnsupported,
                       "Unsupported EXR file detected - multipart EXR.");
      FileIO::fclose(f);
      return;
    }

    if(exrVersion.non_image)
    {
      SET_ERROR_RESULT(m_Error, ResultCode::ImageUnsupported,
                       "Unsupported EXR file detected - deep image EXR.");
      FileIO::fclose(f);
      return;
    }

    if(exrVersion.tiled)
    {
      SET_ERROR_RESULT(m_Error, ResultCode::ImageUnsupported,
                       "Unsupported EXR file detected - tiled EXR.");
      FileIO::fclose(f);
      return;
    }

    EXRHeader exrHeader;
    InitEXRHeader(&exrHeader);

    rdcstr errString;

    {
      const char *err = NULL;

      ret = ParseEXRHeaderFromMemory(&exrHeader, &exrVersion, buffer.data(), buffer.size(), &err);

      if(err)
      {
        errString = err;
        free((void *)err);
      }
    }

    if(ret != 0)
    {
      SET_ERROR_RESULT(
          m_Error, ResultCode::ImageUnsupported,
          "EXR file detected, but couldn't load with ParseEXRHeaderFromMemory %d: '%s'", ret,
          errString.c_str());
      FileIO::fclose(f);
      return;
    }

    for(int i = 0; i < exrHeader.num_channels; i++)
      exrHeader.requested_pixel_types[i] = TINYEXR_PIXELTYPE_FLOAT;

    EXRImage exrImage;
    InitEXRImage(&exrImage);

    {
      const char *err = NULL;

      ret = LoadEXRImageFromMemory(&exrImage, &exrHeader, buffer.data(), buffer.size(), &err);

      if(err)
      {
        errString = err;
        free((void *)err);
      }
    }

    if(ret != 0)
    {
      SET_ERROR_RESULT(m_Error, ResultCode::ImageUnsupported,
                       "EXR file detected, but couldn't load with LoadEXRImageFromMemory %d: '%s'",
                       ret, errString.c_str());
      FileIO::fclose(f);
      return;
    }

    texDetails.width = exrImage.width;
    texDetails.height = exrImage.height;

    datasize = texDetails.width * texDetails.height * 4 * sizeof(float);
    data = (byte *)malloc(datasize);

    if(!data)
    {
      SET_ERROR_RESULT(m_Error, ResultCode::ReplayOutOfMemory,
                       "Allocation for %zu bytes failed for EXR data", datasize);
      return;
    }

    int channels[4] = {-1, -1, -1, -1};
    for(int i = 0; i < exrImage.num_channels; i++)
    {
      switch(exrHeader.channels[i].name[0])
      {
        case 'R': channels[0] = i; break;
        case 'G': channels[1] = i; break;
        case 'B': channels[2] = i; break;
        case 'A': channels[3] = i; break;
      }
    }

    float *rgba = (float *)data;
    float **src = (float **)exrImage.images;

    for(uint32_t i = 0; i < texDetails.width * texDetails.height; i++)
    {
      for(int c = 0; c < 4; c++)
      {
        if(channels[c] >= 0)
          rgba[i * 4 + c] = src[channels[c]][i];
        else if(c < 3)    // RGB channels default to 0
          rgba[i * 4 + c] = 0.0f;
        else    // alpha defaults to 1
          rgba[i * 4 + c] = 1.0f;
      }
    }

    ret = FreeEXRImage(&exrImage);

    // shouldn't get here but let's be safe
    if(ret != 0)
    {
      free(data);
      SET_ERROR_RESULT(m_Error, ResultCode::ImageUnsupported,
                       "EXR file detected, but failed during parsing");
      FileIO::fclose(f);
      return;
    }
  }
  else if(stbi_is_hdr_from_file(f))
  {
    texDetails.format = rgba32_float;

    FileIO::fseek64(f, 0, SEEK_SET);

    int ignore = 0;
    data = (byte *)stbi_loadf_from_file(f, (int *)&texDetails.width, (int *)&texDetails.height,
                                        &ignore, 4);
    datasize = texDetails.width * texDetails.height * 4 * sizeof(float);
  }
  else if(is_dds_file(headerBuffer, headerSize))
  {
    dds = true;
  }
  else
  {
    FileIO::fseek64(f, 0, SEEK_SET);

    int ignore = 0;
    int ret = stbi_info_from_file(f, (int *)&texDetails.width, (int *)&texDetails.height, &ignore);

    // just in case (we shouldn't have come in here if this weren't true), make sure
    // the format is supported
    if(ret == 0)
    {
      SET_ERROR_RESULT(m_Error, ResultCode::ImageUnsupported, "Image could not be identified");
      FileIO::fclose(f);
      return;
    }

    if(texDetails.width == 0 || texDetails.width >= 65536 || texDetails.height == 0 ||
       texDetails.height >= 65536)
    {
      SET_ERROR_RESULT(m_Error, ResultCode::ImageUnsupported,
                       "Image dimensions of %ux%u are not supported", texDetails.width,
                       texDetails.height);
      FileIO::fclose(f);
      return;
    }

    texDetails.format = rgba8_unorm;

    data = stbi_load_from_file(f, (int *)&texDetails.width, (int *)&texDetails.height, &ignore, 4);
    datasize = texDetails.width * texDetails.height * 4 * sizeof(byte);
  }

  // if we don't have data at this point (and we're not a dds file) then the
  // file was corrupted and we failed to load it
  if(!dds && data == NULL)
  {
    SET_ERROR_RESULT(m_Error, ResultCode::ImageUnsupported, "Image failed to load");
    FileIO::fclose(f);
    return;
  }

  m_FrameRecord.frameInfo.initDataSize = 0;
  m_FrameRecord.frameInfo.persistentSize = 0;
  m_FrameRecord.frameInfo.uncompressedFileSize = datasize;

  read_dds_data read_data = {};

  if(dds)
  {
    FileIO::fseek64(f, 0, SEEK_SET);
    StreamReader reader(f);
    RDResult res = load_dds_from_file(&reader, read_data);
    f = NULL;

    if(res != ResultCode::Succeeded)
    {
      m_Error = res;
      return;
    }

    texDetails.cubemap = read_data.cubemap;
    texDetails.arraysize = read_data.slices;
    texDetails.width = read_data.width;
    texDetails.height = read_data.height;
    texDetails.depth = read_data.depth;
    texDetails.mips = read_data.mips;
    texDetails.format = read_data.format;
    if(texDetails.depth > 1)
    {
      texDetails.type = TextureType::Texture3D;
      texDetails.dimension = 3;
    }
    else if(texDetails.cubemap)
    {
      texDetails.type =
          texDetails.arraysize > 1 ? TextureType::TextureCubeArray : TextureType::TextureCube;
      texDetails.dimension = 2;
    }
    else if(texDetails.height > 1)
    {
      texDetails.type =
          texDetails.arraysize > 1 ? TextureType::Texture2DArray : TextureType::Texture2D;
      texDetails.dimension = 2;
    }
    else
    {
      texDetails.type =
          texDetails.arraysize > 1 ? TextureType::Texture1DArray : TextureType::Texture1D;
      texDetails.dimension = 1;
    }

    m_FrameRecord.frameInfo.uncompressedFileSize = 0;
    for(uint32_t i = 0; i < texDetails.arraysize * texDetails.mips; i++)
      m_FrameRecord.frameInfo.uncompressedFileSize += read_data.subresources[i].second;
  }

  m_FrameRecord.frameInfo.compressedFileSize = m_FrameRecord.frameInfo.uncompressedFileSize;

  // recreate proxy texture if necessary.
  // we rewrite the texture IDs so that the outside world doesn't need to know about this (we only
  // ever have one texture in the image viewer so we can just set all texture IDs used to that).
  if(m_TextureID != ResourceId())
  {
    if(m_TexDetails.width != texDetails.width || m_TexDetails.height != texDetails.height ||
       m_TexDetails.depth != texDetails.depth || m_TexDetails.cubemap != texDetails.cubemap ||
       m_TexDetails.mips != texDetails.mips || m_TexDetails.arraysize != texDetails.arraysize ||
       m_TexDetails.format != texDetails.format)
    {
      m_TextureID = ResourceId();
    }
  }

  m_TexDetails = texDetails;

  if(m_TextureID == ResourceId())
    CreateProxyTexture(texDetails, read_data);

  if(m_TextureID == ResourceId())
  {
    SET_ERROR_RESULT(m_Error, ResultCode::APIInitFailed,
                     "Couldn't create proxy texture for image file");
  }

  m_TexDetails.resourceId = m_TextureID;
  m_TexDetails.byteSize = fileSize;

  if(!dds)
  {
    m_Proxy->SetProxyTextureData(m_TextureID, Subresource(), data, datasize);
    free(data);
  }
  else
  {
    for(uint32_t i = 0; i < texDetails.arraysize * texDetails.mips; i++)
    {
      m_Proxy->SetProxyTextureData(m_TextureID, {i % texDetails.mips, i / texDetails.mips},
                                   read_data.buffer.data() + read_data.subresources[i].first,
                                   read_data.subresources[i].second);
    }
  }

  if(f != NULL)
    FileIO::fclose(f);
}

void ImageViewer::CreateProxyTexture(TextureDescription &texDetails, read_dds_data &read_data)
{
  if(m_Proxy->IsTextureSupported(texDetails))
  {
    m_TextureID = m_Proxy->CreateProxyTexture(texDetails);
    return;
  }
  else
  {
    // for block compressed 3D textures these may not be supported, try to remap to a 2D array
    if(texDetails.format.BlockFormat() && texDetails.type == TextureType::Texture3D)
    {
      TextureDescription arrayDetails = texDetails;
      arrayDetails.arraysize = arrayDetails.depth;
      arrayDetails.depth = 1;
      arrayDetails.type = TextureType::Texture2DArray;
      arrayDetails.dimension = 2;

      if(m_Proxy->IsTextureSupported(arrayDetails))
      {
        texDetails = arrayDetails;
        m_TextureID = m_Proxy->CreateProxyTexture(arrayDetails);

        rdcarray<rdcpair<size_t, size_t>> oldSubs;
        oldSubs.swap(read_data.subresources);

        // reformat the subresources. The data doesn't change we just add new offsets/sizes
        for(uint32_t i = 0; i < texDetails.arraysize * texDetails.mips; i++)
        {
          const uint32_t mip = i % texDetails.mips;
          const uint32_t slice = i / texDetails.mips;

          // size of each subresource is 1/Nth for an N-sized array
          size_t size = oldSubs[mip].second / texDetails.arraysize;

          // and the offset is slice steps further on
          size_t offset = oldSubs[mip].first + size * slice;

          read_data.subresources.push_back({offset, size});
        }

        return;
      }
    }

    if(read_data.width != 0)
    {
      // see if we can convert this format on the CPU for proxying
      bool convertSupported = false;
      DecodeFormattedComponents(texDetails.format, NULL, &convertSupported);

      if(convertSupported)
      {
        uint32_t srcStride = texDetails.format.ElementSize();

        if(texDetails.format.type == ResourceFormatType::D16S8)
          srcStride = 4;
        else if(texDetails.format.type == ResourceFormatType::D32S8)
          srcStride = 8;

        m_RealTexData.resize(texDetails.arraysize * texDetails.mips);

        bytebuf convertedData;

        for(uint32_t i = 0; i < texDetails.arraysize * texDetails.mips; i++)
        {
          const uint32_t mip = i % texDetails.mips;

          const uint32_t mipwidth = RDCMAX(1U, texDetails.width >> mip);
          const uint32_t mipheight = RDCMAX(1U, texDetails.height >> mip);
          const uint32_t mipdepth = RDCMAX(1U, texDetails.depth >> mip);

          byte *old = read_data.buffer.data() + read_data.subresources[i].first;
          m_RealTexData[i].assign(old, read_data.subresources[i].second);

          read_data.subresources[i].first = convertedData.size();
          read_data.subresources[i].second = sizeof(FloatVector) * mipwidth * mipheight * mipdepth;
          convertedData.resize(convertedData.size() + read_data.subresources[i].second);
          byte *converted = convertedData.data() + read_data.subresources[i].first;

          byte *src = old;
          FloatVector *dst = (FloatVector *)converted;

          for(uint32_t z = 0; z < mipdepth; z++)
          {
            for(uint32_t y = 0; y < mipheight; y++)
            {
              for(uint32_t x = 0; x < mipwidth; x++)
              {
                *dst = DecodeFormattedComponents(texDetails.format, src);
                dst++;
                src += srcStride;
              }
            }
          }
        }

        read_data.buffer.swap(convertedData);

        ResourceFormat rgba32_float;
        rgba32_float.type = ResourceFormatType::Regular;
        rgba32_float.compByteWidth = 4;
        rgba32_float.compCount = 4;
        rgba32_float.compType = CompType::Float;

        texDetails.format = rgba32_float;
        m_TextureID = m_Proxy->CreateProxyTexture(texDetails);
      }
      else
      {
        RDCLOG("Format %s not supported for local display and can't be converted manually.",
               texDetails.format.Name().c_str());
      }
    }
    else
    {
      RDCERR("Standard format %s expected to be supported for local display but can't.",
             texDetails.format.Name().c_str());
    }
  }
}
