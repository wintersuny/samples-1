/******************************************************************************\
Copyright (c) 2005-2016, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

This sample was distributed or derived from the Intel's Media Samples package.
The original version of this sample may be obtained from https://software.intel.com/en-us/intel-media-server-studio
or https://software.intel.com/en-us/media-client-solutions-support.
\**********************************************************************************/

#include "mfx_samples_config.h"

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

#include "pipeline_transcode.h"
#include "transcode_utils.h"
#include "sample_utils.h"
#include "mfx_vpp_plugin.h"

#include "plugin_loader.h"

using namespace TranscodingSample;

mfxU32 MFX_STDCALL TranscodingSample::ThranscodeRoutine(void   *pObj)
{
    mfxU64 start = TranscodingSample::GetTick();
    ThreadTranscodeContext *pContext = (ThreadTranscodeContext*)pObj;
    pContext->transcodingSts = MFX_ERR_NONE;
    for(;;)
    {
        while (MFX_ERR_NONE == pContext->transcodingSts)
        {
            pContext->transcodingSts = pContext->pPipeline->Run();
        }
        if (MFX_ERR_MORE_DATA == pContext->transcodingSts)
        {
            // get next coded data
            mfxStatus bs_sts = pContext->pBSProcessor->PrepareBitstream();
            // we can continue transcoding if input bistream presents
            if (MFX_ERR_NONE == bs_sts)
            {
                MSDK_IGNORE_MFX_STS(pContext->transcodingSts, MFX_ERR_MORE_DATA);
                continue;
            }
            // no need more data, need to get last transcoded frames
            else if (MFX_ERR_MORE_DATA == bs_sts)
            {
                pContext->transcodingSts = pContext->pPipeline->FlushLastFrames();
            }
        }

        break; // exit loop
    }

    MSDK_IGNORE_MFX_STS(pContext->transcodingSts, MFX_WRN_VALUE_NOT_CHANGED);

    pContext->working_time = TranscodingSample::GetTime(start);
    pContext->numTransFrames = pContext->pPipeline->GetProcessFrames();

    return 0;
} // mfxU32 __stdcall ThranscodeRoutine(void   *pObj)

// set structure to define values
sInputParams::sInputParams()
{
    Reset();
} // sInputParams::sInputParams()

void sInputParams::Reset()
{
    memset(static_cast<__sInputParams*>(this), 0, sizeof(__sInputParams));

    priority = MFX_PRIORITY_NORMAL;
    libType = MFX_IMPL_SOFTWARE;
    MaxFrameNumber = 0xFFFFFFFF;
    pVppCompDstRects = NULL;
    m_hwdev = NULL;
    DenoiseLevel=-1;
    DetailLevel=-1;
}

CTranscodingPipeline::CTranscodingPipeline():
    m_pmfxBS(NULL),
    m_pMFXAllocator(NULL),
    m_hdl(NULL),
    m_EncSurfaceType(0),
    m_DecSurfaceType(0),
    m_bIsVpp(false),
    m_bIsPlugin(false),
    m_nTimeout(0),
    m_bOwnMVCSeqDescMemory(true),
    m_AsyncDepth(0),
    m_nProcessedFramesNum(0),
    m_nOutputFramesNum(0),
    m_bIsJoinSession(false),
    m_bDecodeEnable(true),
    m_bEncodeEnable(true),
    m_nVPPCompEnable(0),
    m_hwdev4Rendering(NULL),
    m_bUseOpaqueMemory(false),
    m_pBuffer(NULL),
    m_pParentPipeline(NULL),
    m_bIsInit(false),
    m_FrameNumberPreference(0xFFFFFFFF),
    m_MaxFramesForTranscode(0xFFFFFFFF),
    m_pBSProcessor(NULL),
    m_nReqFrameTime(0),
    m_LastDecSyncPoint(0),
    m_NumFramesForReset(0)
{
    MSDK_ZERO_MEMORY(m_mfxDecParams);
    MSDK_ZERO_MEMORY(m_mfxVppParams);
    MSDK_ZERO_MEMORY(m_mfxEncParams);
    MSDK_ZERO_MEMORY(m_mfxPluginParams);
    MSDK_ZERO_MEMORY(m_RotateParam);
    MSDK_ZERO_MEMORY(m_mfxPreEncParams);

    MSDK_ZERO_MEMORY(m_mfxDecResponse);
    MSDK_ZERO_MEMORY(m_mfxEncResponse);

    MSDK_ZERO_MEMORY(m_Request);

    MSDK_ZERO_MEMORY(m_VppDoNotUse);
    MSDK_ZERO_MEMORY(m_MVCSeqDesc);
    MSDK_ZERO_MEMORY(m_EncOpaqueAlloc);
    MSDK_ZERO_MEMORY(m_VppOpaqueAlloc);
    MSDK_ZERO_MEMORY(m_DecOpaqueAlloc);
    MSDK_ZERO_MEMORY(m_PluginOpaqueAlloc);
    MSDK_ZERO_MEMORY(m_PreEncOpaqueAlloc);
    MSDK_ZERO_MEMORY(m_ExtLAControl);
    MSDK_ZERO_MEMORY(m_CodingOption2);
    MSDK_ZERO_MEMORY(m_CodingOption3);

    m_MVCSeqDesc.Header.BufferId = MFX_EXTBUFF_MVC_SEQ_DESC;
    m_MVCSeqDesc.Header.BufferSz = sizeof(mfxExtMVCSeqDesc);

    m_VppDoNotUse.Header.BufferId = MFX_EXTBUFF_VPP_DONOTUSE;
    m_VppDoNotUse.Header.BufferSz = sizeof(mfxExtVPPDoNotUse);

    m_ExtHEVCParam.Header.BufferId = MFX_EXTBUFF_HEVC_PARAM;
    m_ExtHEVCParam.Header.BufferSz = sizeof(mfxExtHEVCParam);

    m_EncOpaqueAlloc.Header.BufferId = m_VppOpaqueAlloc.Header.BufferId =
        m_DecOpaqueAlloc.Header.BufferId = m_PluginOpaqueAlloc.Header.BufferId =
        m_PreEncOpaqueAlloc.Header.BufferId =
        MFX_EXTBUFF_OPAQUE_SURFACE_ALLOCATION;
    m_EncOpaqueAlloc.Header.BufferSz = m_VppOpaqueAlloc.Header.BufferSz =
        m_DecOpaqueAlloc.Header.BufferSz = m_PluginOpaqueAlloc.Header.BufferSz =
        m_PreEncOpaqueAlloc.Header.BufferSz =
        sizeof(mfxExtOpaqueSurfaceAlloc);

    m_VppCompParams.InputStream = NULL;
    m_CodingOption2.Header.BufferId = MFX_EXTBUFF_CODING_OPTION2;
    m_CodingOption2.Header.BufferSz = sizeof(m_CodingOption2);

    m_CodingOption3.Header.BufferId = MFX_EXTBUFF_CODING_OPTION3;
    m_CodingOption3.Header.BufferSz = sizeof(m_CodingOption3);

    m_VppCompParams.InputStream = NULL;
} //CTranscodingPipeline::CTranscodingPipeline()

CTranscodingPipeline::~CTranscodingPipeline()
{
    Close();
} //CTranscodingPipeline::CTranscodingPipeline()

mfxStatus CTranscodingPipeline::CheckRequiredAPIVersion(mfxVersion& version, sInputParams *pParams)
{
    MSDK_CHECK_POINTER(pParams, MFX_ERR_NULL_PTR);

    if (pParams->bIsMVC && !CheckVersion(&version, MSDK_FEATURE_MVC)) {
        msdk_printf(MSDK_STRING("error: MVC is not supported in the %d.%d API version\n"),
            version.Major, version.Minor);
        return MFX_ERR_UNSUPPORTED;

    }
    if ((pParams->DecodeId == MFX_CODEC_JPEG) && !CheckVersion(&version, MSDK_FEATURE_JPEG_DECODE)) {
        msdk_printf(MSDK_STRING("error: Jpeg decoder is not supported in the %d.%d API version\n"),
            version.Major, version.Minor);
        return MFX_ERR_UNSUPPORTED;
    }
    if ((pParams->EncodeId == MFX_CODEC_JPEG) && !CheckVersion(&version, MSDK_FEATURE_JPEG_ENCODE)) {
        msdk_printf(MSDK_STRING("error: Jpeg encoder is not supported in the %d.%d API version\n"),
            version.Major, version.Minor);
        return MFX_ERR_UNSUPPORTED;
    }

    if ((pParams->bLABRC || pParams->nLADepth) && !CheckVersion(&version, MSDK_FEATURE_LOOK_AHEAD)) {
        msdk_printf(MSDK_STRING("error: Look Ahead is not supported in the %d.%d API version\n"),
            version.Major, version.Minor);
        return MFX_ERR_UNSUPPORTED;
    }

    return MFX_ERR_NONE;
}

// initialize decode part
mfxStatus CTranscodingPipeline::DecodePreInit(sInputParams *pParams)
{
    // initialize decode pert
    mfxStatus sts = MFX_ERR_NONE;

    if (m_bDecodeEnable)
    {
        if (CheckVersion(&m_Version, MSDK_FEATURE_PLUGIN_API))
        {
            /* Here we actually define the following codec initialization scheme:
            *  1. If plugin path or guid is specified: we load user-defined plugin (example: VP8 sample decoder plugin)
            *  2. If plugin path not specified:
            *    2.a) we check if codec is distributed as a mediasdk plugin and load it if yes
            *    2.b) if codec is not in the list of mediasdk plugins, we assume, that it is supported inside mediasdk library
            */
            if (pParams->decoderPluginParams.type == MFX_PLUGINLOAD_TYPE_FILE && strlen(pParams->decoderPluginParams.strPluginPath))
            {
                m_pUserDecoderModule.reset(new MFXVideoUSER(*m_pmfxSession.get()));
                m_pUserDecoderPlugin.reset(LoadPlugin(MFX_PLUGINTYPE_VIDEO_DECODE, *m_pmfxSession.get(), pParams->decoderPluginParams.pluginGuid, 1, pParams->decoderPluginParams.strPluginPath, (mfxU32)strlen(pParams->decoderPluginParams.strPluginPath)));
                if (m_pUserDecoderPlugin.get() == NULL) sts = MFX_ERR_UNSUPPORTED;
            }
            else
            {
                if (AreGuidsEqual(pParams->decoderPluginParams.pluginGuid, MSDK_PLUGINGUID_NULL))
                {
                    pParams->decoderPluginParams.pluginGuid = msdkGetPluginUID(pParams->libType, MSDK_VDECODE, pParams->DecodeId);
                }
                if (!AreGuidsEqual(pParams->decoderPluginParams.pluginGuid, MSDK_PLUGINGUID_NULL))
                {
                    m_pUserDecoderPlugin.reset(LoadPlugin(MFX_PLUGINTYPE_VIDEO_DECODE, *m_pmfxSession.get(), pParams->decoderPluginParams.pluginGuid, 1));
                    if (m_pUserDecoderPlugin.get() == NULL) sts = MFX_ERR_UNSUPPORTED;
                }
                if(sts==MFX_ERR_UNSUPPORTED)
                {
                    msdk_printf(MSDK_STRING("Default plugin cannot be loaded (possibly you have to define plugin explicitly)\n"));
                }
            }
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
        }

        // create decoder
        m_pmfxDEC.reset(new MFXVideoDECODE(*m_pmfxSession.get()));

        // set video type in parameters
        m_mfxDecParams.mfx.CodecId = pParams->DecodeId;

        // configure specific decoder parameters
        sts = InitDecMfxParams(pParams);
        if (MFX_ERR_MORE_DATA == sts)
        {
            m_pmfxDEC.reset(NULL);
            return sts;
        }
        else
        {
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
        }
    }
    else
    {
        m_mfxDecParams = m_pParentPipeline->GetDecodeParam();
        m_MVCSeqDesc = m_pParentPipeline->GetDecMVCSeqDesc();
        m_bOwnMVCSeqDescMemory = false;
    }

    if (pParams->nFPS)
    {
        this->m_nReqFrameTime = 1000000 / pParams->nFPS;
    }

    return sts;

} //mfxStatus CTranscodingPipeline::Init(sInputParams *pParams)

mfxStatus CTranscodingPipeline::VPPPreInit(sInputParams *pParams)
{
    mfxStatus sts = MFX_ERR_NONE;
    bool bVppCompInitRequire = false;

    if (((pParams->eModeExt == VppComp) || (pParams->eModeExt == VppCompOnly)) &&
        (pParams->eMode == Source))
        bVppCompInitRequire = true;

    // Obtaining decoder output FourCC - in case of inter-session, just take it from params, in intra-session case, take it from parent session
    // In inter-session case, we'll enable chroma-changing VPP only in encoding session, and only if decoderFourCC!=encoderFourCC
    mfxU32 decoderFourCC = m_bDecodeEnable ? m_mfxDecParams.mfx.FrameInfo.FourCC : m_pParentPipeline->GetDecodeParam().mfx.FrameInfo.FourCC;

    if (m_bEncodeEnable || m_bDecodeEnable)
    {
        if ( (m_mfxDecParams.mfx.FrameInfo.CropW != pParams->nDstWidth && pParams->nDstWidth) ||
             (m_mfxDecParams.mfx.FrameInfo.CropH != pParams->nDstHeight && pParams->nDstHeight) ||
             (pParams->bEnableDeinterlacing) || (pParams->DenoiseLevel!=-1) || (pParams->DetailLevel!=-1) || (pParams->FRCAlgorithm) ||
             (bVppCompInitRequire) || (pParams->fieldProcessingMode) ||
             (pParams->EncoderFourCC && decoderFourCC && pParams->EncoderFourCC != decoderFourCC && m_bEncodeEnable))
        {
            m_bIsVpp = true;
            sts = InitVppMfxParams(pParams);
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
        }

        if (pParams->nRotationAngle) // plugin was requested
        {
            m_bIsPlugin = true;
            sts = InitPluginMfxParams(pParams);
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

            std::auto_ptr<MFXVideoVPPPlugin> pVPPPlugin(new MFXVideoVPPPlugin(*m_pmfxSession.get()));
            MSDK_CHECK_POINTER(pVPPPlugin.get(), MFX_ERR_NULL_PTR);

            sts = pVPPPlugin->LoadDLL(pParams->strVPPPluginDLLPath);
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

            m_RotateParam.Angle = pParams->nRotationAngle;
            sts = pVPPPlugin->SetAuxParam(&m_RotateParam, sizeof(m_RotateParam));
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

            if(!m_bUseOpaqueMemory)
            {
                sts = pVPPPlugin->SetFrameAllocator(m_pMFXAllocator);
                MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
            }

            m_pmfxVPP.reset(pVPPPlugin.release());
        }

        if (!m_bIsPlugin && m_bIsVpp) // only VPP was requested
        {
            m_pmfxVPP.reset(new MFXVideoMultiVPP(*m_pmfxSession.get()));
        }
    }

    return sts;

} //mfxStatus CTranscodingPipeline::VPPInit(sInputParams *pParams)

mfxStatus CTranscodingPipeline::EncodePreInit(sInputParams *pParams)
{

    mfxStatus sts = MFX_ERR_NONE;

    if (m_bEncodeEnable)
    {
        if(pParams->EncodeId != MFX_FOURCC_DUMP)
        {
            if (CheckVersion(&m_Version, MSDK_FEATURE_PLUGIN_API) && (m_pUserEncPlugin.get() == NULL))
            {
                /* Here we actually define the following codec initialization scheme:
                *  1. If plugin path or guid is specified: we load user-defined plugin (example: HEVC encoder plugin)
                *  2. If plugin path not specified:
                *    2.a) we check if codec is distributed as a mediasdk plugin and load it if yes
                *    2.b) if codec is not in the list of mediasdk plugins, we assume, that it is supported inside mediasdk library
                */
                if (pParams->encoderPluginParams.type == MFX_PLUGINLOAD_TYPE_FILE && strlen(pParams->encoderPluginParams.strPluginPath))
                {
                    m_pUserEncoderModule.reset(new MFXVideoUSER(*m_pmfxSession.get()));
                    m_pUserEncoderPlugin.reset(LoadPlugin(MFX_PLUGINTYPE_VIDEO_ENCODE, *m_pmfxSession.get(), pParams->encoderPluginParams.pluginGuid, 1, pParams->encoderPluginParams.strPluginPath, (mfxU32)strlen(pParams->encoderPluginParams.strPluginPath)));
                    if (m_pUserEncoderPlugin.get() == NULL) sts = MFX_ERR_UNSUPPORTED;
                }
                else
                {
                    if (AreGuidsEqual(pParams->encoderPluginParams.pluginGuid, MSDK_PLUGINGUID_NULL))
                    {
                        pParams->encoderPluginParams.pluginGuid = msdkGetPluginUID(pParams->libType, MSDK_VENCODE, pParams->EncodeId);
                    }
                    if (!AreGuidsEqual(pParams->encoderPluginParams.pluginGuid, MSDK_PLUGINGUID_NULL))
                    {
                        m_pUserEncoderPlugin.reset(LoadPlugin(MFX_PLUGINTYPE_VIDEO_ENCODE, *m_pmfxSession.get(), pParams->encoderPluginParams.pluginGuid, 1));
                        if (m_pUserEncoderPlugin.get() == NULL) sts = MFX_ERR_UNSUPPORTED;
                    }
                    if(sts==MFX_ERR_UNSUPPORTED)
                    {
                        msdk_printf(MSDK_STRING("Default plugin cannot be loaded (possibly you have to define plugin explicitly)\n"));
                    }
                }
                MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
            }

            // create encoder
            m_pmfxENC.reset(new MFXVideoENCODE(*m_pmfxSession.get()));

            sts = InitEncMfxParams(pParams);
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

            // Querying parameters
            mfxU16 ioPattern = m_mfxEncParams.IOPattern;
            sts = m_pmfxENC->Query(&m_mfxEncParams, &m_mfxEncParams);
            m_mfxEncParams.IOPattern=ioPattern; // Workaround for a problem: Query changes IOPattern incorrectly

            MSDK_IGNORE_MFX_STS(sts, MFX_WRN_INCOMPATIBLE_VIDEO_PARAM);
            MSDK_CHECK_RESULT_SAFE(sts, MFX_ERR_NONE, sts, msdk_printf(MSDK_STRING("Encoder parameters query failed.\n")));
        }
        else
        {
            //--- This one is required for YUV output
            m_mfxEncParams.mfx.CodecId = pParams->EncodeId;
        }

    }
    return sts;

} // mfxStatus CTranscodingPipeline::EncodeInit(sInputParams *pParams)

mfxStatus CTranscodingPipeline::PreEncPreInit(sInputParams *pParams)
{
     mfxStatus sts = MFX_ERR_NONE;
     // PreInit is allowed in decoder session only
     if (pParams->bEnableExtLA && m_bDecodeEnable)
     {
        /* Here we actually define the following codec initialization scheme:
         *    a) we check if codec is distributed as a user plugin and load it if yes
         *    b) we check if codec is distributed as a mediasdk plugin and load it if yes
         *    c) if codec is not in the list of user plugins or mediasdk plugins, we assume, that it is supported inside mediasdk library
         */

        m_pUserEncPlugin.reset(LoadPlugin(MFX_PLUGINTYPE_VIDEO_ENCODE, *m_pmfxSession.get(), MFX_PLUGINID_H264LA_HW, 1));
        if (m_pUserEncPlugin.get() == NULL) sts = MFX_ERR_UNSUPPORTED;

         // create encoder
        m_pmfxPreENC.reset(new MFXVideoENC(*m_pmfxSession.get()));

        sts = InitPreEncMfxParams(pParams);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    }
    return sts;

}

mfxVideoParam CTranscodingPipeline::GetDecodeParam() {
    if (m_bIsVpp)
     {
         mfxVideoParam tmp = m_mfxDecParams;
         tmp.mfx.FrameInfo = m_mfxVppParams.vpp.Out;
         return tmp;
     }
    else if (m_bIsPlugin)
    {
        mfxVideoParam tmp = m_mfxDecParams;
        tmp.mfx.FrameInfo = m_mfxPluginParams.mfx.FrameInfo;
        return tmp;
    }

     return m_mfxDecParams;
 };
// 1 ms provides better result in range [0..5] ms
enum
{
    TIME_TO_SLEEP = 1
};

mfxStatus CTranscodingPipeline::DecodeOneFrame(ExtendedSurface *pExtSurface)
{
    MSDK_CHECK_POINTER(pExtSurface,  MFX_ERR_NULL_PTR);

    mfxStatus sts = MFX_ERR_MORE_SURFACE;
    mfxFrameSurface1    *pmfxSurface = NULL;
    pExtSurface->pSurface = NULL;
    mfxU32 i = 0;

    //--- Time measurements
    if (statisticsWindowSize)
    {
        inputStatistics.StopTimeMeasurementWithCheck();
        inputStatistics.StartTimeMeasurement();
    }

    while (MFX_ERR_MORE_DATA == sts || MFX_ERR_MORE_SURFACE == sts || MFX_ERR_NONE < sts)
    {
        if (MFX_WRN_DEVICE_BUSY == sts)
        {
            WaitForDeviceToBecomeFree(*m_pmfxSession,m_LastDecSyncPoint,sts);
        }
        else if (MFX_ERR_MORE_DATA == sts)
        {
            sts = m_pBSProcessor->GetInputBitstream(&m_pmfxBS); // read more data to input bit stream
            MSDK_BREAK_ON_ERROR(sts);
        }
        else if (MFX_ERR_MORE_SURFACE == sts)
        {
            // find new working surface
            for (i = 0; i < MSDK_SURFACE_WAIT_INTERVAL; i += TIME_TO_SLEEP)
            {
                pmfxSurface = GetFreeSurface(true);
                if (pmfxSurface)
                {
                    break;
                }
                else
                {
                    MSDK_SLEEP(TIME_TO_SLEEP);
                }
            }

            MSDK_CHECK_POINTER_SAFE(pmfxSurface, MFX_ERR_MEMORY_ALLOC, msdk_printf(MSDK_STRING("ERROR: No free surfaces in decoder pool (during long period)\n"))); // return an error if a free surface wasn't found
        }

        sts = m_pmfxDEC->DecodeFrameAsync(m_pmfxBS, pmfxSurface, &pExtSurface->pSurface, &pExtSurface->Syncp);

        if (sts==MFX_ERR_NONE)
        {
            m_LastDecSyncPoint = pExtSurface->Syncp;
        }
        // ignore warnings if output is available,
        if (MFX_ERR_NONE < sts && pExtSurface->Syncp)
        {
            sts = MFX_ERR_NONE;
        }

    } //while processing

    return sts;

} // mfxStatus CTranscodingPipeline::DecodeOneFrame(ExtendedSurface *pExtSurface)
mfxStatus CTranscodingPipeline::DecodeLastFrame(ExtendedSurface *pExtSurface)
{
    mfxFrameSurface1    *pmfxSurface = NULL;
    mfxStatus sts = MFX_ERR_MORE_SURFACE;
    mfxU32 i = 0;

    //--- Time measurements
    if (statisticsWindowSize)
    {
        inputStatistics.StopTimeMeasurementWithCheck();
        inputStatistics.StartTimeMeasurement();
    }

    // retrieve the buffered decoded frames
    while (MFX_ERR_MORE_SURFACE == sts || MFX_WRN_DEVICE_BUSY == sts)
    {
        if (MFX_WRN_DEVICE_BUSY == sts)
        {
            WaitForDeviceToBecomeFree(*m_pmfxSession,m_LastDecSyncPoint,sts);
        }

        // find new working surface
        for (i = 0; i < MSDK_SURFACE_WAIT_INTERVAL; i += TIME_TO_SLEEP)
        {
            pmfxSurface = GetFreeSurface(true);
            if (pmfxSurface)
            {
                break;
            }
            else
            {
                MSDK_SLEEP(TIME_TO_SLEEP);
            }
        }

        MSDK_CHECK_POINTER_SAFE(pmfxSurface, MFX_ERR_MEMORY_ALLOC, msdk_printf(MSDK_STRING("ERROR: No free surfaces in decoder pool (during long period)\n"))); // return an error if a free surface wasn't found
        sts = m_pmfxDEC->DecodeFrameAsync(NULL, pmfxSurface, &pExtSurface->pSurface, &pExtSurface->Syncp);
    }
    return sts;
}

mfxStatus CTranscodingPipeline::VPPOneFrame(ExtendedSurface *pSurfaceIn, ExtendedSurface *pExtSurface)
{
    MSDK_CHECK_POINTER(pExtSurface,  MFX_ERR_NULL_PTR);
    mfxFrameSurface1 *pmfxSurface = NULL;
    // find/wait for a free working surface
    for (mfxU32 i = 0; i < MSDK_SURFACE_WAIT_INTERVAL; i += TIME_TO_SLEEP)
    {
        pmfxSurface= GetFreeSurface(false);

        if (pmfxSurface)
        {
            break;
        }
        else
        {
            MSDK_SLEEP(TIME_TO_SLEEP);
        }
    }

    MSDK_CHECK_POINTER_SAFE(pmfxSurface, MFX_ERR_MEMORY_ALLOC, msdk_printf(MSDK_STRING("ERROR: No free surfaces for VPP in encoder pool (during long period)\n"))); // return an error if a free surface wasn't found

    // make sure picture structure has the initial value
    // surfaces are reused and VPP may change this parameter in certain configurations
    pmfxSurface->Info.PicStruct = m_mfxVppParams.vpp.Out.PicStruct ? m_mfxVppParams.vpp.Out.PicStruct : (m_bEncodeEnable ? m_mfxEncParams : m_mfxDecParams).mfx.FrameInfo.PicStruct;

    pExtSurface->pSurface = pmfxSurface;
    mfxStatus sts = MFX_ERR_NONE;
    for(;;)
    {
        sts = m_pmfxVPP->RunFrameVPPAsync(pSurfaceIn->pSurface, pmfxSurface, NULL, &pExtSurface->Syncp);

        if (MFX_ERR_NONE < sts && !pExtSurface->Syncp) // repeat the call if warning and no output
        {
            if (MFX_WRN_DEVICE_BUSY == sts)
                MSDK_SLEEP(1); // wait if device is busy
        }
        else if (MFX_ERR_NONE < sts && pExtSurface->Syncp)
        {
            sts = MFX_ERR_NONE; // ignore warnings if output is available
            break;
        }
        else
        {
            break;
        }
    }
    return sts;

} // mfxStatus CTranscodingPipeline::DecodeOneFrame(ExtendedSurface *pExtSurface)

mfxStatus CTranscodingPipeline::EncodeOneFrame(ExtendedSurface *pExtSurface, mfxBitstream *pBS)
{
    mfxStatus sts = MFX_ERR_NONE;
    mfxEncodeCtrl *pCtrl = (pExtSurface->pCtrl) ? &pExtSurface->pCtrl->encCtrl : NULL;

    for (;;)
    {
        // at this point surface for encoder contains either a frame from file or a frame processed by vpp
        sts = m_pmfxENC->EncodeFrameAsync(pCtrl, pExtSurface->pSurface, pBS, &pExtSurface->Syncp);

        if (MFX_ERR_NONE < sts && !pExtSurface->Syncp) // repeat the call if warning and no output
        {
            if (MFX_WRN_DEVICE_BUSY == sts)
                MSDK_SLEEP(TIME_TO_SLEEP); // wait if device is busy
        }
        else if (MFX_ERR_NONE < sts && pExtSurface->Syncp)
        {
            sts = MFX_ERR_NONE; // ignore warnings if output is available
            break;
        }
        else if (MFX_ERR_NOT_ENOUGH_BUFFER == sts)
        {
            sts = AllocateSufficientBuffer(pBS);
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
        }
        else
        {
            break;
        }
    }
    return sts;

} //CTranscodingPipeline::EncodeOneFrame(ExtendedSurface *pExtSurface)

mfxStatus CTranscodingPipeline::PreEncOneFrame(ExtendedSurface *pInSurface, ExtendedSurface *pOutSurface)
{
    mfxStatus sts = MFX_ERR_NONE;
    PreEncAuxBuffer* pAux = NULL;

    for (mfxU32 i = 0; i < MSDK_WAIT_INTERVAL; i += TIME_TO_SLEEP)
    {
        pAux = GetFreePreEncAuxBuffer();
        if (pAux)
        {
            break;
        }
        else
        {
            MSDK_SLEEP(TIME_TO_SLEEP);
        }
    }
    MSDK_CHECK_POINTER(pAux,  MFX_ERR_MEMORY_ALLOC);
    for (;;)
    {
        pAux->encInput.InSurface = pInSurface->pSurface;
        // at this point surface for encoder contains either a frame from file or a frame processed by vpp
        sts = m_pmfxPreENC->ProcessFrameAsync(&pAux->encInput, &pAux->encOutput, &pOutSurface->Syncp );

        if (MFX_ERR_NONE < sts && !pOutSurface->Syncp) // repeat the call if warning and no output
        {
            if (MFX_WRN_DEVICE_BUSY == sts)
                MSDK_SLEEP(TIME_TO_SLEEP); // wait if device is busy
        }
        else if (MFX_ERR_NONE <= sts && pOutSurface->Syncp)
        {
            LockPreEncAuxBuffer(pAux);
            pOutSurface->pCtrl = pAux;
            MSDK_CHECK_POINTER(pAux->encOutput.ExtParam, MFX_ERR_NULL_PTR);
            MSDK_CHECK_NOT_EQUAL(pAux->encOutput.NumExtParam, 1, MFX_ERR_UNSUPPORTED);
            pOutSurface->pSurface =  ((mfxExtLAFrameStatistics *) pAux->encOutput.ExtParam[0])->OutSurface;
            sts = MFX_ERR_NONE; // ignore warnings if output is available
            break;
        }
        else
        {
            break;
        }
    }
    return sts;
}

// signal that there are no more frames
void CTranscodingPipeline::NoMoreFramesSignal(ExtendedSurface &DecExtSurface)
{
    SafetySurfaceBuffer   *pNextBuffer = m_pBuffer;
    DecExtSurface.pSurface = NULL;
    pNextBuffer->AddSurface(DecExtSurface);
    /*if 1_to_N mode */
    if (0 == m_nVPPCompEnable)
    {
        while (pNextBuffer->m_pNext)
        {
            pNextBuffer = pNextBuffer->m_pNext;
            pNextBuffer->AddSurface(DecExtSurface);
        }
    }
}

mfxStatus CTranscodingPipeline::Decode()
{
    mfxStatus sts = MFX_ERR_NONE;

    ExtendedSurface DecExtSurface    = {0};
    ExtendedSurface VppExtSurface    = {0};
    ExtendedSurface PreEncExtSurface = {0};
    bool shouldReadNextFrame=true;

    SafetySurfaceBuffer   *pNextBuffer = m_pBuffer;
    bool bEndOfFile = false;
    bool bLastCycle = false;
    time_t start = time(0);
    while (MFX_ERR_NONE == sts)
    {
        pNextBuffer = m_pBuffer;

        if (time(0) - start >= m_nTimeout)
            bLastCycle = true;

        msdk_tick nBeginTime = msdk_time_get_tick(); // microseconds.

        if(shouldReadNextFrame)
        {
            if (m_MaxFramesForTranscode != m_nProcessedFramesNum)
            {
                if (!bEndOfFile)
                {
                    sts = DecodeOneFrame(&DecExtSurface);
                    if (MFX_ERR_MORE_DATA == sts)
                    {
                        sts = DecodeLastFrame(&DecExtSurface);
                        bEndOfFile = bLastCycle ? true : false;
                    }
                }
                else
                {
                    sts = DecodeLastFrame(&DecExtSurface);
                }

                if (sts == MFX_ERR_NONE)
                {
                    m_nProcessedFramesNum++;
                    if (statisticsWindowSize && m_nProcessedFramesNum && 0 == m_nProcessedFramesNum % statisticsWindowSize)
                    {
                        inputStatistics.PrintStatistics(MSDK_STRING("(I):"));
                        inputStatistics.ResetStatistics();
                        fflush(stdout);
                    }
                }
                if (sts == MFX_ERR_MORE_DATA && (m_pmfxVPP.get() || m_pmfxPreENC.get()))
                {
                    DecExtSurface.pSurface = NULL;  // to get buffered VPP or ENC frames
                    sts = MFX_ERR_NONE;
                }
                if (!bLastCycle && (DecExtSurface.pSurface == NULL) )
                {
                    static_cast<FileBitstreamProcessor_WithReset*>(m_pBSProcessor)->ResetInput();

                    if (!GetNumFramesForReset())
                        SetNumFramesForReset(m_nProcessedFramesNum);
                    sts = MFX_ERR_NONE;
                    continue;
                }
                MSDK_BREAK_ON_ERROR(sts);
            }
            else if ( m_pmfxVPP.get() || m_pmfxPreENC.get())
            {
                DecExtSurface.pSurface = NULL;  // to get buffered VPP or ENC frames
                bEndOfFile = true;
                sts = MFX_ERR_NONE;
            }
            else
            {
                break;
            }
        }

        if (m_pmfxVPP.get())
        {
            sts = VPPOneFrame(&DecExtSurface, &VppExtSurface);
        }
        else // no VPP - just copy pointers
        {
            VppExtSurface.pSurface = DecExtSurface.pSurface;
            VppExtSurface.Syncp = DecExtSurface.Syncp;
        }

        //--- Sometimes VPP may return 2 surfaces on output, for the first one it'll return status MFX_ERR_MORE_SURFACE - we have to call VPPOneFrame again in this case
        if(MFX_ERR_MORE_SURFACE == sts)
        {
            shouldReadNextFrame=false;
            sts=MFX_ERR_NONE;
        }
        else
        {
            shouldReadNextFrame=true;
        }


        if (sts == MFX_ERR_MORE_DATA || !VppExtSurface.pSurface)
        {
            if (!bEndOfFile )
            {
                sts = MFX_ERR_NONE;
                continue; // go get next frame from Decode
            }
        }
        if (sts == MFX_ERR_MORE_DATA && m_pmfxPreENC.get())
        {
           VppExtSurface.pSurface = NULL;  // to get buffered VPP or ENC frames
           sts = MFX_ERR_NONE;
        }

        MSDK_BREAK_ON_ERROR(sts);

        if (m_pmfxPreENC.get())
        {
            sts = PreEncOneFrame(&VppExtSurface, &PreEncExtSurface);
        }
        else // no VPP - just copy pointers
        {
            PreEncExtSurface.pSurface = VppExtSurface.pSurface;
            PreEncExtSurface.Syncp = VppExtSurface.Syncp;
        }

        if (sts == MFX_ERR_MORE_DATA || !PreEncExtSurface.pSurface)
        {
            if (!bEndOfFile )
            {
                sts = MFX_ERR_NONE;
                continue; // go get next frame from Decode
            }
        }
        if (!bLastCycle)
        {
            sts = MFX_ERR_NONE;
        }
        MSDK_BREAK_ON_ERROR(sts);

        // if session is not joined and it is not parent - synchronize
        if (!m_bIsJoinSession && m_pParentPipeline)
        {
            sts = m_pmfxSession->SyncOperation(PreEncExtSurface.Syncp, MSDK_WAIT_INTERVAL);
            PreEncExtSurface.Syncp = NULL;
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
        }

        // add surfaces in queue for all sinks
        pNextBuffer->AddSurface(PreEncExtSurface);
        /* one of key parts for N_to_1 mode:
        * decoded frame should be in one buffer only as we have only 1 (one!) sink
        * */
        if (0 == m_nVPPCompEnable)
        {
            while (pNextBuffer->m_pNext)
            {
                pNextBuffer = pNextBuffer->m_pNext;
                pNextBuffer->AddSurface(PreEncExtSurface);
            }
        }

        if (!statisticsWindowSize && 0 == (m_nProcessedFramesNum - 1) % 100)
        {
            msdk_printf(MSDK_STRING("."));
        }

        if (bEndOfFile && m_nTimeout)
        {
            break;
        }

        msdk_tick nFrameTime = msdk_time_get_tick() - nBeginTime;
        if (nFrameTime < m_nReqFrameTime)
        {
            MSDK_USLEEP((mfxU32)(m_nReqFrameTime - nFrameTime));
        }
    }

    MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);

    NoMoreFramesSignal(PreEncExtSurface);

    if (MFX_ERR_NONE == sts)
        sts = MFX_WRN_VALUE_NOT_CHANGED;

    return sts;
} // mfxStatus CTranscodingPipeline::Decode()

mfxStatus CTranscodingPipeline::Encode()
{
    mfxStatus sts = MFX_ERR_NONE;
    ExtendedSurface DecExtSurface = {};
    ExtendedSurface VppExtSurface = {};
    ExtendedBS      *pBS = NULL;
    bool isQuit = false;
    bool bInsertIDR = false;
    int nFramesAlreadyPut = 0;
    SafetySurfaceBuffer   *curBuffer = m_pBuffer;

    PreEncAuxBuffer encAuxCtrl;
    MSDK_ZERO_MEMORY(encAuxCtrl);
    encAuxCtrl.encCtrl.FrameType = MFX_FRAMETYPE_I | MFX_FRAMETYPE_IDR | MFX_FRAMETYPE_REF;

    bool shouldReadNextFrame=true;
    while (MFX_ERR_NONE == sts ||  MFX_ERR_MORE_DATA == sts)
    {
        msdk_tick nBeginTime = msdk_time_get_tick(); // microseconds.

        if(shouldReadNextFrame)
        {
            while (MFX_ERR_MORE_SURFACE == curBuffer->GetSurface(DecExtSurface) && !isQuit)
                MSDK_SLEEP(TIME_TO_SLEEP);

            // if session is not joined and it is not parent - synchronize
            if (!m_bIsJoinSession && m_pParentPipeline)
            {
                // if it is not already synchronized
                if (DecExtSurface.Syncp)
                {
                    sts = m_pParentPipeline->m_pmfxSession->SyncOperation(DecExtSurface.Syncp, MSDK_WAIT_INTERVAL);
                    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
                }
            }

            mfxU32 NumFramesForReset = m_pParentPipeline ? m_pParentPipeline->GetNumFramesForReset() : 0;
            if (NumFramesForReset && !(nFramesAlreadyPut % NumFramesForReset) )
            {
                bInsertIDR = true;
            }

            if (NULL == DecExtSurface.pSurface)
            {
                isQuit = true;
            }
        }

        if (m_pmfxVPP.get())
        {
            sts = VPPOneFrame(&DecExtSurface, &VppExtSurface);
            VppExtSurface.pCtrl = DecExtSurface.pCtrl;
        }
        else // no VPP - just copy pointers
        {
            VppExtSurface.pSurface = DecExtSurface.pSurface;
            VppExtSurface.pCtrl = DecExtSurface.pCtrl;
            VppExtSurface.Syncp = DecExtSurface.Syncp;
        }

        if(MFX_ERR_MORE_SURFACE == sts)
        {
            shouldReadNextFrame=false;
            sts=MFX_ERR_NONE;
        }
        else
        {
            shouldReadNextFrame=true;
        }

        if (MFX_ERR_MORE_DATA == sts)
        {
            if (isQuit)
            {
                // to get buffered VPP or ENC frames
                VppExtSurface.pSurface = NULL;
                sts = MFX_ERR_NONE;
            }
            else
            {
                curBuffer->ReleaseSurface(DecExtSurface.pSurface);

                //--- We should switch to another buffer ONLY in case of Composition
                if (curBuffer->m_pNext != NULL && m_nVPPCompEnable > 0)
                {
                    curBuffer = curBuffer->m_pNext;
                    continue;
                }
                else
                {
                    curBuffer = m_pBuffer;
                    continue; /* No more buffer from decoders */
                }
            }
        }

        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

        if (m_nVPPCompEnable > 0)
            curBuffer->ReleaseSurface(DecExtSurface.pSurface);

        curBuffer = m_pBuffer;

        pBS = m_pBSStore->GetNext();
        if (!pBS)
            return MFX_ERR_NOT_FOUND;

        m_BSPool.push_back(pBS);

        mfxU32 NumFramesForReset = m_pParentPipeline ? m_pParentPipeline->GetNumFramesForReset() : 0;
        if (NumFramesForReset && !(m_nProcessedFramesNum % NumFramesForReset))
        {
            static_cast<FileBitstreamProcessor_WithReset*>(m_pBSProcessor)->ResetOutput();
        }

        SetSurfaceAuxIDR(VppExtSurface, &encAuxCtrl, bInsertIDR);
        bInsertIDR = false;

        if (m_nVPPCompEnable != VppCompOnly)
        {
            if(m_mfxEncParams.mfx.CodecId != MFX_FOURCC_DUMP)
            {
                sts = EncodeOneFrame(&VppExtSurface, &m_BSPool.back()->Bitstream);
                if (!sts)
                    nFramesAlreadyPut++;
            }
            else
            {
                sts = Surface2BS(&VppExtSurface, &m_BSPool.back()->Bitstream,m_mfxVppParams.vpp.Out.FourCC);
            }
        }

        if(shouldReadNextFrame) // Release current decoded surface only if we're going to read next one during next iteration
        {
            m_pBuffer->ReleaseSurface(DecExtSurface.pSurface);
        }

        // check if we need one more frame from decode
        if (MFX_ERR_MORE_DATA == sts)
        {
            // the task in not in Encode queue
            m_BSPool.pop_back();
            m_pBSStore->Release(pBS);

            if (NULL == VppExtSurface.pSurface ) // there are no more buffered frames in encoder
            {
                break;
            }
            else
            {
                // get next frame from Decode
                sts = MFX_ERR_NONE;
                continue;
            }
        }

        // check encoding result
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

        m_nProcessedFramesNum++;
        if (statisticsWindowSize && m_nOutputFramesNum && 0 == m_nOutputFramesNum % statisticsWindowSize)
        {
            outputStatistics.PrintStatistics(MSDK_STRING("(O):"));
            outputStatistics.ResetStatistics();
            fflush(stdout);
        }

        m_BSPool.back()->Syncp = VppExtSurface.Syncp;
        m_BSPool.back()->pCtrl =  VppExtSurface.pCtrl;

        /* Actually rendering... if enabled
         * SYNC have not done by driver !!! */
        if (m_nVPPCompEnable == VppCompOnly)
        {
            if(m_BSPool.size())
            {
                ExtendedBS *pBitstreamEx_temp  = m_BSPool.front();

                // get result coded stream
                if(VppExtSurface.pSurface)
                {
                    sts = m_pmfxSession->SyncOperation(VppExtSurface.Syncp, MSDK_WAIT_INTERVAL);
                    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
#if defined(_WIN32) || defined(_WIN64)
                    sts = m_hwdev4Rendering->RenderFrame(VppExtSurface.pSurface, m_pMFXAllocator);
#else
                    sts = m_hwdev4Rendering->RenderFrame(VppExtSurface.pSurface, NULL);
#endif
                    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
                }

                UnPreEncAuxBuffer(pBitstreamEx_temp->pCtrl);

                pBitstreamEx_temp->Bitstream.DataLength = 0;
                pBitstreamEx_temp->Bitstream.DataOffset = 0;

                m_BSPool.pop_front();
                m_pBSStore->Release(pBitstreamEx_temp);
            }

            //--- If there's no data coming out from VPP and there's no data coming from decoders (isQuit==true),
            // then we should quit, otherwise we may stuck here forever (cause there's no new data coming)
            if(!VppExtSurface.pSurface && isQuit)
            {
                break;
            }
        }

        if (m_nVPPCompEnable != VppCompOnly)
        {
            if (m_BSPool.size() == m_AsyncDepth)
            {
                sts = PutBS();
                MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
            }
            else
            {
                continue;
            }
        } // if (m_nVPPCompEnable != VppCompOnly)

        /* Exit condition */
        if (m_nProcessedFramesNum == m_MaxFramesForTranscode)
        {
            break;
        }

        msdk_tick nFrameTime = msdk_time_get_tick() - nBeginTime;
        if (nFrameTime < m_nReqFrameTime)
        {
            MSDK_USLEEP((mfxU32)(m_nReqFrameTime - nFrameTime));
        }
    }
    MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);

    if (m_nVPPCompEnable != VppCompOnly)
    {
        // need to get buffered bitstream
        if (MFX_ERR_NONE == sts)
        {
            while (m_BSPool.size())
            {
                sts = PutBS();
                MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
            }
        }
    }
    if (MFX_ERR_NONE == sts)
        sts = MFX_WRN_VALUE_NOT_CHANGED;
    return sts;

} // mfxStatus CTranscodingPipeline::Encode()

void CTranscodingPipeline::SetSurfaceAuxIDR(ExtendedSurface& extSurface, PreEncAuxBuffer* encAuxCtrl, bool bInsertIDR)
{
    if (bInsertIDR)
    {
        if (extSurface.pCtrl)
            extSurface.pCtrl->encCtrl.FrameType = MFX_FRAMETYPE_I | MFX_FRAMETYPE_IDR | MFX_FRAMETYPE_REF;
        else
            extSurface.pCtrl = encAuxCtrl;
    }
    else
    {
        if (extSurface.pCtrl)
        {
            if (extSurface.pCtrl != encAuxCtrl)
                extSurface.pCtrl->encCtrl.FrameType = 0;
            else
                extSurface.pCtrl = NULL;
        }
    }
}

mfxStatus CTranscodingPipeline::Transcode()
{
    mfxStatus sts = MFX_ERR_NONE;
    ExtendedSurface DecExtSurface = {0};
    ExtendedSurface VppExtSurface = {0};
    ExtendedBS *pBS = NULL;
    bool bNeedDecodedFrames = true; // indicates if we need to decode frames
    bool bEndOfFile = false;
    bool bLastCycle = false;
    bool bInsertIDR = false;
    bool shouldReadNextFrame=true;
    PreEncAuxBuffer encAuxCtrl;

    MSDK_ZERO_MEMORY(encAuxCtrl);
    encAuxCtrl.encCtrl.FrameType = MFX_FRAMETYPE_I | MFX_FRAMETYPE_IDR | MFX_FRAMETYPE_REF;

    time_t start = time(0);
    while (MFX_ERR_NONE == sts )
    {
        msdk_tick nBeginTime = msdk_time_get_tick(); // microseconds.

        if (time(0) - start >= m_nTimeout)
            bLastCycle = true;
        if (m_MaxFramesForTranscode == m_nProcessedFramesNum)
        {
            DecExtSurface.pSurface = NULL;  // to get buffered VPP or ENC frames
            bNeedDecodedFrames = false; // no more decoded frames needed
        }

        // if need more decoded frames
        // decode a frame
        if (bNeedDecodedFrames && shouldReadNextFrame)
        {
            if (!bEndOfFile)
            {
                sts = DecodeOneFrame(&DecExtSurface);
                if (MFX_ERR_MORE_DATA == sts)
                {
                    if (!bLastCycle)
                    {
                        bInsertIDR = true;

                        static_cast<FileBitstreamProcessor_WithReset*>(m_pBSProcessor)->ResetInput();
                        static_cast<FileBitstreamProcessor_WithReset*>(m_pBSProcessor)->ResetOutput();
                        bNeedDecodedFrames = true;

                        bEndOfFile = false;
                        sts = MFX_ERR_NONE;
                        continue;
                    }
                    else
                    {
                        bEndOfFile = true;
                    }
                }
            }

            if (bEndOfFile)
            {
                sts = DecodeLastFrame(&DecExtSurface);
            }

            if (sts == MFX_ERR_MORE_DATA)
            {
                DecExtSurface.pSurface = NULL;  // to get buffered VPP or ENC frames
                sts = MFX_ERR_NONE;
            }

            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
        }

        // pre-process a frame
        if (m_pmfxVPP.get())
        {
            sts = VPPOneFrame(&DecExtSurface, &VppExtSurface);
        }
        else // no VPP - just copy pointers
        {
            VppExtSurface.pSurface = DecExtSurface.pSurface;
            VppExtSurface.pCtrl = DecExtSurface.pCtrl;
            VppExtSurface.Syncp = DecExtSurface.Syncp;
        }

        if(MFX_ERR_MORE_SURFACE == sts)
        {
            shouldReadNextFrame=false;
            sts=MFX_ERR_NONE;
        }
        else
        {
            shouldReadNextFrame=true;
        }

        if (sts == MFX_ERR_MORE_DATA)
        {
            sts = MFX_ERR_NONE;
            if (NULL == DecExtSurface.pSurface) // there are no more buffered frames in VPP
            {
                VppExtSurface.pSurface = NULL; // to get buffered ENC frames
            }
            else
            {
                continue; // go get next frame from Decode
            }
        }

        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

        // encode frame
        pBS = m_pBSStore->GetNext();
        if (!pBS)
            return MFX_ERR_NOT_FOUND;

        m_BSPool.push_back(pBS);

        // encode frame only if it wasn't encoded enough
        SetSurfaceAuxIDR(VppExtSurface, &encAuxCtrl, bInsertIDR);
        bInsertIDR = false;

        if(bNeedDecodedFrames)
        {
            if(m_mfxEncParams.mfx.CodecId != MFX_FOURCC_DUMP)
            {
                sts = EncodeOneFrame(&VppExtSurface, &m_BSPool.back()->Bitstream);
            }
            else
            {
                sts = Surface2BS(&VppExtSurface, &m_BSPool.back()->Bitstream,m_mfxVppParams.vpp.Out.FourCC);
            }
        }
        else
        {
            sts = MFX_ERR_MORE_DATA;
        }

        // check if we need one more frame from decode
        if (MFX_ERR_MORE_DATA == sts)
        {
            // the task in not in Encode queue
            m_BSPool.pop_back();
            m_pBSStore->Release(pBS);

            if (NULL == VppExtSurface.pSurface) // there are no more buffered frames in encoder
            {
                break;
            }
            sts = MFX_ERR_NONE;
            continue;
        }

        // check encoding result
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

        m_nProcessedFramesNum++;
        if(statisticsWindowSize)
        {
            if (m_nOutputFramesNum && 0 == m_nOutputFramesNum % statisticsWindowSize)
            {
                inputStatistics.PrintStatistics(MSDK_STRING("(I): "));
                outputStatistics.PrintStatistics(MSDK_STRING("(O): "));
                inputStatistics.ResetStatistics();
                outputStatistics.ResetStatistics();
                fflush(stdout);
            }
        }
        else if (0 == (m_nProcessedFramesNum - 1) % 100)
        {
            msdk_printf(MSDK_STRING("."));
        }

        m_BSPool.back()->Syncp = VppExtSurface.Syncp;

        if (m_BSPool.size() == m_AsyncDepth)
        {
            sts = PutBS();
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
        }

        msdk_tick nFrameTime = msdk_time_get_tick() - nBeginTime;
        if (nFrameTime < m_nReqFrameTime)
        {
            MSDK_USLEEP((mfxU32)(m_nReqFrameTime - nFrameTime));
        }
    }
    MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);

    // need to get buffered bitstream
    if (MFX_ERR_NONE == sts)
    {
        while(m_BSPool.size())
        {
            sts = PutBS();
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
        }
    }

    if (MFX_ERR_NONE == sts)
        sts = MFX_WRN_VALUE_NOT_CHANGED;

    return sts;
} // mfxStatus CTranscodingPipeline::Transcode()

mfxStatus CTranscodingPipeline::PutBS()
{
    mfxStatus       sts = MFX_ERR_NONE;
    ExtendedBS *pBitstreamEx  = m_BSPool.front();
    // get result coded stream, synchronize only if we still have sync point
    if(pBitstreamEx->Syncp)
    {
        sts = m_pmfxSession->SyncOperation(pBitstreamEx->Syncp, MSDK_WAIT_INTERVAL);
    }
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    m_nOutputFramesNum++;

    //--- Time measurements
    if (statisticsWindowSize)
    {
        outputStatistics.StopTimeMeasurementWithCheck();
        outputStatistics.StartTimeMeasurement();
    }

    sts = m_pBSProcessor->ProcessOutputBitstream(&pBitstreamEx->Bitstream);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    UnPreEncAuxBuffer(pBitstreamEx->pCtrl);

    pBitstreamEx->Bitstream.DataLength = 0;
    pBitstreamEx->Bitstream.DataOffset = 0;

    m_BSPool.pop_front();
    m_pBSStore->Release(pBitstreamEx);

    return sts;
} //mfxStatus CTranscodingPipeline::PutBS()

mfxStatus CTranscodingPipeline::Surface2BS(ExtendedSurface* pSurf,mfxBitstream* pBS, mfxU32 fourCC)
{
    mfxStatus       sts = MFX_ERR_MORE_DATA;
    // get result coded stream
    if(pSurf->Syncp)
    {
        sts = m_pmfxSession->SyncOperation(pSurf->Syncp, MSDK_WAIT_INTERVAL);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
        pSurf->Syncp=0;

        //--- Copying data from surface to bitstream
        sts = m_pMFXAllocator->Lock(m_pMFXAllocator->pthis,pSurf->pSurface->Data.MemId,&pSurf->pSurface->Data);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

        switch(fourCC)
        {
        case 0: // Default value is NV12
        case MFX_FOURCC_NV12:
            sts=NV12toBS(pSurf->pSurface,pBS);
            break;
        case MFX_FOURCC_RGB4:
            sts=RGB4toBS(pSurf->pSurface,pBS);
            break;
        case MFX_FOURCC_YUY2:
            sts=YUY2toBS(pSurf->pSurface,pBS);
            break;
        }
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

        sts = m_pMFXAllocator->Unlock(m_pMFXAllocator->pthis,pSurf->pSurface->Data.MemId,&pSurf->pSurface->Data);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    }

    return sts;
}

mfxStatus CTranscodingPipeline::NV12toBS(mfxFrameSurface1* pSurface,mfxBitstream* pBS)
{
    mfxFrameInfo& info = pSurface->Info;
    mfxFrameData& data = pSurface->Data;
    if((int)pBS->MaxLength-(int)pBS->DataLength < (int)(info.CropH*info.CropW*3/2))
    {
        mfxStatus sts = ExtendMfxBitstream(pBS, pBS->DataLength+(int)(info.CropH*info.CropW*3/2));
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    }

    for (mfxU16 i = 0; i < info.CropH; i++)
    {
        MSDK_MEMCPY(pBS->Data+pBS->DataLength, data.Y + (info.CropY * data.Pitch + info.CropX)+ i * data.Pitch, info.CropW);
        pBS->DataLength += info.CropW;
    }

    mfxU16 h = info.CropH / 2;
    mfxU16 w = info.CropW;

    for(mfxU16 offset = 0; offset<2;offset++)
    {
        for (mfxU16 i = 0; i < h; i++)
        {
            for (mfxU16 j = offset; j < w; j += 2)
            {
                pBS->Data[pBS->DataLength]=*(data.UV + (info.CropY * data.Pitch / 2 + info.CropX) + i * data.Pitch + j);
                pBS->DataLength++;
            }
        }
    }

    return MFX_ERR_NONE;
}

mfxStatus CTranscodingPipeline::RGB4toBS(mfxFrameSurface1* pSurface,mfxBitstream* pBS)
{
    mfxFrameInfo& info = pSurface->Info;
    mfxFrameData& data = pSurface->Data;
    if((int)pBS->MaxLength-(int)pBS->DataLength < (int)(info.CropH*info.CropW*4))
    {
        mfxStatus sts = ExtendMfxBitstream(pBS, pBS->DataLength+(int)(info.CropH*info.CropW*4));
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    }

    for (mfxU16 i = 0; i < info.CropH; i++)
    {
        MSDK_MEMCPY(pBS->Data+pBS->DataLength, data.B + (info.CropY * data.Pitch + info.CropX*4)+ i * data.Pitch, info.CropW*4);
        pBS->DataLength += info.CropW*4;
    }

    return MFX_ERR_NONE;
}

mfxStatus CTranscodingPipeline::YUY2toBS(mfxFrameSurface1* pSurface,mfxBitstream* pBS)
{
    mfxFrameInfo& info = pSurface->Info;
    mfxFrameData& data = pSurface->Data;
    if((int)pBS->MaxLength-(int)pBS->DataLength < (int)(info.CropH*info.CropW*4))
    {
        mfxStatus sts = ExtendMfxBitstream(pBS, pBS->DataLength+(int)(info.CropH*info.CropW*4));
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    }

    for (mfxU16 i = 0; i < info.CropH; i++)
    {
        MSDK_MEMCPY(pBS->Data+pBS->DataLength, data.Y + (info.CropY * data.Pitch + info.CropX/2*4)+ i * data.Pitch, info.CropW*2);
        pBS->DataLength += info.CropW*2;
    }

    return MFX_ERR_NONE;
}


mfxStatus CTranscodingPipeline::AllocMVCSeqDesc()
{
    mfxU32 i;

    m_MVCSeqDesc.View = new mfxMVCViewDependency[m_MVCSeqDesc.NumView];
    MSDK_CHECK_POINTER(m_MVCSeqDesc.View, MFX_ERR_MEMORY_ALLOC);
    for (i = 0; i < m_MVCSeqDesc.NumView; ++i)
    {
        MSDK_ZERO_MEMORY(m_MVCSeqDesc.View[i]);
    }
    m_MVCSeqDesc.NumViewAlloc = m_MVCSeqDesc.NumView;

    m_MVCSeqDesc.ViewId = new mfxU16[m_MVCSeqDesc.NumViewId];
    MSDK_CHECK_POINTER(m_MVCSeqDesc.ViewId, MFX_ERR_MEMORY_ALLOC);
    for (i = 0; i < m_MVCSeqDesc.NumViewId; ++i)
    {
        MSDK_ZERO_MEMORY(m_MVCSeqDesc.ViewId[i]);
    }
    m_MVCSeqDesc.NumViewIdAlloc = m_MVCSeqDesc.NumViewId;

    m_MVCSeqDesc.OP = new mfxMVCOperationPoint[m_MVCSeqDesc.NumOP];
    MSDK_CHECK_POINTER(m_MVCSeqDesc.OP, MFX_ERR_MEMORY_ALLOC);
    for (i = 0; i < m_MVCSeqDesc.NumOP; ++i)
    {
        MSDK_ZERO_MEMORY(m_MVCSeqDesc.OP[i]);
    }
    m_MVCSeqDesc.NumOPAlloc = m_MVCSeqDesc.NumOP;

    return MFX_ERR_NONE;
}

void CTranscodingPipeline::FreeMVCSeqDesc()
{
    if (m_bOwnMVCSeqDescMemory)
    {
        MSDK_SAFE_DELETE_ARRAY(m_MVCSeqDesc.View);
        MSDK_SAFE_DELETE_ARRAY(m_MVCSeqDesc.ViewId);
        MSDK_SAFE_DELETE_ARRAY(m_MVCSeqDesc.OP);
    }
}

mfxStatus CTranscodingPipeline::InitDecMfxParams(sInputParams *pInParams)
{
    mfxStatus sts = MFX_ERR_NONE;
    MSDK_CHECK_POINTER(pInParams, MFX_ERR_NULL_PTR);

    m_mfxDecParams.AsyncDepth = m_AsyncDepth;

    // configure and attach external parameters
    if (m_bUseOpaqueMemory)
        m_DecExtParams.push_back((mfxExtBuffer *)&m_DecOpaqueAlloc);

    if (pInParams->bIsMVC)
        m_DecExtParams.push_back((mfxExtBuffer *)&m_MVCSeqDesc);

    if (!m_DecExtParams.empty())
    {
        m_mfxDecParams.ExtParam = &m_DecExtParams[0]; // vector is stored linearly in memory
        m_mfxDecParams.NumExtParam = (mfxU16)m_DecExtParams.size();
    }

    // read a portion of data for DecodeHeader function
    sts = m_pBSProcessor->GetInputBitstream(&m_pmfxBS);
    if (MFX_ERR_MORE_DATA == sts)
        return sts;
    else
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // try to find a sequence header in the stream
    // if header is not found this function exits with error (e.g. if device was lost and there's no header in the remaining stream)
    for(;;)
    {
        // trying to find PicStruct information in AVI headers
        if ( pInParams->DecodeId == MFX_CODEC_JPEG )
            MJPEG_AVI_ParsePicStruct(m_pmfxBS);

        // parse bit stream and fill mfx params
        sts = m_pmfxDEC->DecodeHeader(m_pmfxBS, &m_mfxDecParams);

        if (MFX_ERR_MORE_DATA == sts)
        {
            if (m_pmfxBS->MaxLength == m_pmfxBS->DataLength)
            {
                sts = ExtendMfxBitstream(m_pmfxBS, m_pmfxBS->MaxLength * 2);
                MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
            }

            // read a portion of data for DecodeHeader function
            sts = m_pBSProcessor->GetInputBitstream(&m_pmfxBS);
            if (MFX_ERR_MORE_DATA == sts)
                return sts;
            else
                MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);


            continue;
        }
        else if (MFX_ERR_NOT_ENOUGH_BUFFER == sts && pInParams->bIsMVC)
        {
            sts = AllocMVCSeqDesc();
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

            continue;
        }
        else
            break;
    }

    // to enable decorative flags, has effect with 1.3 API libraries only
    // (in case of JPEG decoder - it is not valid to use this field)
    if (m_mfxDecParams.mfx.CodecId != MFX_CODEC_JPEG)
        m_mfxDecParams.mfx.ExtendedPicStruct = 1;

    // check DecodeHeader status
    if (MFX_WRN_PARTIAL_ACCELERATION == sts)
    {
        msdk_printf(MSDK_STRING("WARNING: partial acceleration\n"));
        MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    }
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // set memory pattern
    if (m_bUseOpaqueMemory)
        m_mfxDecParams.IOPattern = MFX_IOPATTERN_OUT_OPAQUE_MEMORY;
    else if (pInParams->bForceSysMem || (MFX_IMPL_SOFTWARE == pInParams->libType))
        m_mfxDecParams.IOPattern = MFX_IOPATTERN_OUT_SYSTEM_MEMORY;
    else
        m_mfxDecParams.IOPattern = MFX_IOPATTERN_OUT_VIDEO_MEMORY;

    // if input is interlaced JPEG stream
    if (((pInParams->DecodeId == MFX_CODEC_JPEG) && (m_pmfxBS->PicStruct == MFX_PICSTRUCT_FIELD_TFF))
        || (m_pmfxBS->PicStruct == MFX_PICSTRUCT_FIELD_BFF))
    {
        m_mfxDecParams.mfx.FrameInfo.CropH *= 2;
        m_mfxDecParams.mfx.FrameInfo.Height = MSDK_ALIGN16(m_mfxDecParams.mfx.FrameInfo.CropH);
        m_mfxDecParams.mfx.FrameInfo.PicStruct = m_pmfxBS->PicStruct;
    }

    // if frame rate specified by user set it for decoder and the whole pipeline
    if (pInParams->dFrameRate)
    {
        ConvertFrameRate(pInParams->dFrameRate, &m_mfxDecParams.mfx.FrameInfo.FrameRateExtN, &m_mfxDecParams.mfx.FrameInfo.FrameRateExtD);
    }
    // if frame rate not specified and input stream header doesn't contain valid values use default (30.0)
    else if (!(m_mfxDecParams.mfx.FrameInfo.FrameRateExtN * m_mfxDecParams.mfx.FrameInfo.FrameRateExtD))
    {
        m_mfxDecParams.mfx.FrameInfo.FrameRateExtN = 30;
        m_mfxDecParams.mfx.FrameInfo.FrameRateExtD = 1;
    }
    else
    {
        // use the value from input stream header
    }

    //--- Force setting fourcc type if required
    if(pInParams->DecoderFourCC)
    {
        m_mfxDecParams.mfx.FrameInfo.FourCC=pInParams->DecoderFourCC;
        m_mfxDecParams.mfx.FrameInfo.ChromaFormat=FourCCToChroma(pInParams->DecoderFourCC);
    }
    return MFX_ERR_NONE;
}// mfxStatus CTranscodingPipeline::InitDecMfxParams()

mfxStatus CTranscodingPipeline::InitEncMfxParams(sInputParams *pInParams)
{
    MSDK_CHECK_POINTER(pInParams,  MFX_ERR_NULL_PTR);
    m_mfxEncParams.mfx.CodecId                 = pInParams->EncodeId;
    m_mfxEncParams.mfx.TargetUsage             = pInParams->nTargetUsage; // trade-off between quality and speed
    m_mfxEncParams.AsyncDepth                  = m_AsyncDepth;

    if (m_pParentPipeline && m_pParentPipeline->m_pmfxPreENC.get())
    {
        m_mfxEncParams.mfx.RateControlMethod       = MFX_RATECONTROL_LA_EXT;
        m_mfxEncParams.mfx.EncodedOrder            = 1; // binary flag, 0 signals encoder to take frames in display order
        m_mfxEncParams.AsyncDepth = m_mfxEncParams.AsyncDepth == 0 ? 2: m_mfxEncParams.AsyncDepth;
    }
    else
    {
        m_mfxEncParams.mfx.RateControlMethod   = pInParams->nRateControlMethod;
    }
    m_mfxEncParams.mfx.NumSlice                = pInParams->nSlices;

    if (pInParams->nRateControlMethod == MFX_RATECONTROL_CQP)
    {
        m_mfxEncParams.mfx.QPI = pInParams->nQPI;
        m_mfxEncParams.mfx.QPP = pInParams->nQPP;
        m_mfxEncParams.mfx.QPB = pInParams->nQPB;
    }

    if (m_bIsVpp)
    {
        MSDK_MEMCPY_VAR(m_mfxEncParams.mfx.FrameInfo, &m_mfxVppParams.vpp.Out, sizeof(mfxFrameInfo));
    }
    else if (m_bIsPlugin)
    {
        MSDK_MEMCPY_VAR(m_mfxEncParams.mfx.FrameInfo, &m_mfxPluginParams.vpp.Out, sizeof(mfxFrameInfo));
    }
    else
    {
        MSDK_MEMCPY_VAR(m_mfxEncParams.mfx.FrameInfo, &m_mfxDecParams.mfx.FrameInfo, sizeof(mfxFrameInfo));
    }

    // leave PAR unset to avoid MPEG2 encoder rejecting streams with unsupported DAR
    m_mfxEncParams.mfx.FrameInfo.AspectRatioW = m_mfxEncParams.mfx.FrameInfo.AspectRatioH = 0;

    // calculate default bitrate based on resolution and framerate

    // set framerate if specified
    if (pInParams->dEncoderFrameRate)
    {
        ConvertFrameRate(pInParams->dEncoderFrameRate, &m_mfxEncParams.mfx.FrameInfo.FrameRateExtN, &m_mfxEncParams.mfx.FrameInfo.FrameRateExtD);
    }

    MSDK_CHECK_ERROR(m_mfxEncParams.mfx.FrameInfo.FrameRateExtN * m_mfxEncParams.mfx.FrameInfo.FrameRateExtD,
        0, MFX_ERR_INVALID_VIDEO_PARAM);

    if (pInParams->nRateControlMethod != MFX_RATECONTROL_CQP)
    {
        if (pInParams->nBitRate == 0)
        {
            pInParams->nBitRate = CalculateDefaultBitrate(pInParams->EncodeId,
                pInParams->nTargetUsage, m_mfxEncParams.mfx.FrameInfo.Width, m_mfxEncParams.mfx.FrameInfo.Height,
                1.0 * m_mfxEncParams.mfx.FrameInfo.FrameRateExtN / m_mfxEncParams.mfx.FrameInfo.FrameRateExtD);
        }
        m_mfxEncParams.mfx.TargetKbps = (mfxU16)(pInParams->nBitRate); // in Kbps
    }

    // In case of HEVC when height and/or width divided with 8 but not divided with 16
    // add extended parameter to increase performance
    if ( ( !((m_mfxEncParams.mfx.FrameInfo.CropW & 15 ) ^ 8 ) ||
           !((m_mfxEncParams.mfx.FrameInfo.CropH & 15 ) ^ 8 ) ) &&
             (m_mfxEncParams.mfx.CodecId == MFX_CODEC_HEVC) )
    {
        m_ExtHEVCParam.PicWidthInLumaSamples = m_mfxEncParams.mfx.FrameInfo.CropW;
        m_ExtHEVCParam.PicHeightInLumaSamples = m_mfxEncParams.mfx.FrameInfo.CropH;
        m_EncExtParams.push_back((mfxExtBuffer*)&m_ExtHEVCParam);
    }

    m_mfxEncParams.mfx.FrameInfo.CropX = 0;
    m_mfxEncParams.mfx.FrameInfo.CropY = 0;

    mfxU16 InPatternFromParent = (mfxU16) ((MFX_IOPATTERN_OUT_VIDEO_MEMORY == m_mfxDecParams.IOPattern) ?
MFX_IOPATTERN_IN_VIDEO_MEMORY : MFX_IOPATTERN_IN_SYSTEM_MEMORY);

    // set memory pattern
    if (m_bUseOpaqueMemory)
        m_mfxEncParams.IOPattern = MFX_IOPATTERN_IN_OPAQUE_MEMORY;
    else
        m_mfxEncParams.IOPattern = InPatternFromParent;

    // we don't specify profile and level and let the encoder choose those basing on parameters
    // we must specify profile only for MVC codec
    if (pInParams->bIsMVC)
    {
        m_mfxEncParams.mfx.CodecProfile = m_mfxDecParams.mfx.CodecProfile;
    }

    // JPEG encoder settings overlap with other encoders settings in mfxInfoMFX structure
    if (MFX_CODEC_JPEG == pInParams->EncodeId)
    {
        m_mfxEncParams.mfx.Interleaved = 1;
        m_mfxEncParams.mfx.Quality = pInParams->nQuality;
        m_mfxEncParams.mfx.RestartInterval = 0;
        MSDK_ZERO_MEMORY(m_mfxEncParams.mfx.reserved5);
    }

    // configure and attach external parameters
    if (pInParams->bLABRC || pInParams->nMaxSliceSize)
    {
        m_CodingOption2.LookAheadDepth = pInParams->nLADepth;
        m_CodingOption2.MaxSliceSize = pInParams->nMaxSliceSize;
        m_EncExtParams.push_back((mfxExtBuffer *)&m_CodingOption2);
    }

    if (pInParams->WinBRCMaxAvgKbps || pInParams->WinBRCSize)
    {
        m_CodingOption3.WinBRCMaxAvgKbps = pInParams->WinBRCMaxAvgKbps;
        m_CodingOption3.WinBRCSize = pInParams->WinBRCSize;
        m_EncExtParams.push_back((mfxExtBuffer *)&m_CodingOption3);
    }

    if (m_bUseOpaqueMemory)
        m_EncExtParams.push_back((mfxExtBuffer *)&m_EncOpaqueAlloc);

    if (pInParams->bIsMVC)
        m_EncExtParams.push_back((mfxExtBuffer *)&m_MVCSeqDesc);

    if (!m_EncExtParams.empty())
    {
        m_mfxEncParams.ExtParam = &m_EncExtParams[0]; // vector is stored linearly in memory
        m_mfxEncParams.NumExtParam = (mfxU16)m_EncExtParams.size();
    }
    if (m_pParentPipeline)
    {
        m_pParentPipeline->AddLaStreams(m_mfxEncParams.mfx.FrameInfo.Width,m_mfxEncParams.mfx.FrameInfo.Height);
    }

    //--- Settings HRD buffer size
    m_mfxEncParams.mfx.BufferSizeInKB = pInParams->BufferSizeInKB ? pInParams->BufferSizeInKB
        : (mfxU16)(m_mfxEncParams.mfx.TargetKbps*4L/8); // buffer for 4 seconds

    //--- Force setting fourcc type if required
    if (pInParams->EncoderFourCC)
    {
        m_mfxEncParams.mfx.FrameInfo.FourCC=pInParams->EncoderFourCC;
        m_mfxEncParams.mfx.FrameInfo.ChromaFormat=FourCCToChroma(pInParams->EncoderFourCC);
    }

    if (pInParams->GopPicSize)
    {
        m_mfxEncParams.mfx.GopPicSize = pInParams->GopPicSize;
    }

    if (pInParams->GopRefDist)
    {
        m_mfxEncParams.mfx.GopRefDist = pInParams->GopRefDist;
    }

    return MFX_ERR_NONE;
}// mfxStatus CTranscodingPipeline::InitEncMfxParams(sInputParams *pInParams)

mfxStatus CTranscodingPipeline::CorrectPreEncAuxPool(mfxU32 num_frames_in_pool)
{
    if (!m_pmfxPreENC.get()) return MFX_ERR_NONE;

    if (m_pPreEncAuxPool.size() < num_frames_in_pool)
    {
        m_pPreEncAuxPool.resize(num_frames_in_pool);
    }
    return MFX_ERR_NONE;
}

mfxStatus CTranscodingPipeline::AllocPreEncAuxPool()
{
    if (!m_pmfxPreENC.get()) return MFX_ERR_NONE;

    mfxU16 num_resolutions = m_ExtLAControl.NumOutStream;
    int buff_size = sizeof(mfxExtLAFrameStatistics) +
        sizeof(mfxLAFrameInfo)*num_resolutions*m_ExtLAControl.LookAheadDepth;

    for (size_t i = 0; i < m_pPreEncAuxPool.size(); i++)
    {
        memset(&m_pPreEncAuxPool[i],0,sizeof(m_pPreEncAuxPool[i]));

        m_pPreEncAuxPool[i].encCtrl.NumExtParam = 1;
        m_pPreEncAuxPool[i].encCtrl.ExtParam = new mfxExtBuffer*[1];

        char *pBuff = new char[buff_size];
        memset(pBuff,0, buff_size);

        m_pPreEncAuxPool[i].encCtrl.ExtParam[0] = (mfxExtBuffer *)pBuff;
        mfxExtLAFrameStatistics *pExtBuffer = (mfxExtLAFrameStatistics *)pBuff;

        pExtBuffer = (mfxExtLAFrameStatistics*) pBuff;
        pExtBuffer->Header.BufferId = MFX_EXTBUFF_LOOKAHEAD_STAT;
        pExtBuffer->Header.BufferSz = buff_size;
        pExtBuffer->NumAlloc = num_resolutions*m_ExtLAControl.LookAheadDepth;
        pExtBuffer->FrameStat = (mfxLAFrameInfo *)(pBuff + sizeof(mfxExtLAFrameStatistics));

        m_pPreEncAuxPool[i].encOutput.NumExtParam = 1;
        m_pPreEncAuxPool[i].encOutput.ExtParam = m_pPreEncAuxPool[i].encCtrl.ExtParam;
    }
    return MFX_ERR_NONE;
}

void CTranscodingPipeline::FreePreEncAuxPool()
{
     for (size_t i = 0; i < m_pPreEncAuxPool.size(); i++)
     {
         if(m_pPreEncAuxPool[i].encCtrl.ExtParam)
         {
             delete [] m_pPreEncAuxPool[i].encCtrl.ExtParam[0];
             delete m_pPreEncAuxPool[i].encCtrl.ExtParam;
         }
     }
     m_pPreEncAuxPool.resize(0);
}

mfxStatus CTranscodingPipeline::InitPreEncMfxParams(sInputParams *pInParams)
{
    MSDK_CHECK_ERROR(pInParams->bEnableExtLA, false, MFX_ERR_INVALID_VIDEO_PARAM);
    MSDK_CHECK_POINTER(pInParams,  MFX_ERR_NULL_PTR);

    mfxVideoParam & param = m_mfxPreEncParams;

    param.AsyncDepth = m_AsyncDepth;

    MSDK_ZERO_MEMORY(param.mfx);
    param.mfx.CodecId= MFX_CODEC_AVC;
    param.mfx.TargetUsage= pInParams->nTargetUsage;

    if (m_bIsVpp)
    {
        MSDK_MEMCPY_VAR(param.mfx.FrameInfo, &m_mfxVppParams.vpp.Out, sizeof(mfxFrameInfo));
    }
    else if (m_bIsPlugin)
    {
        MSDK_MEMCPY_VAR(param.mfx.FrameInfo, &m_mfxPluginParams.vpp.Out, sizeof(mfxFrameInfo));
    }
    else
    {
        MSDK_MEMCPY_VAR(param.mfx.FrameInfo, &m_mfxDecParams.mfx.FrameInfo, sizeof(mfxFrameInfo));
    }

    mfxU16 InPatternFromParent = (mfxU16) ((MFX_IOPATTERN_OUT_VIDEO_MEMORY == m_mfxDecParams.IOPattern) ?
                                                        MFX_IOPATTERN_IN_VIDEO_MEMORY : MFX_IOPATTERN_IN_SYSTEM_MEMORY);

    // set memory pattern
    if (m_bUseOpaqueMemory)
        param.IOPattern = MFX_IOPATTERN_IN_OPAQUE_MEMORY;
    else
        param.IOPattern = InPatternFromParent;

    // configure and attach external parameters
    if (m_bUseOpaqueMemory)
        m_PreEncExtParams.push_back((mfxExtBuffer *)&m_PreEncOpaqueAlloc);

    MSDK_ZERO_MEMORY(m_ExtLAControl);
    m_ExtLAControl.Header.BufferId = MFX_EXTBUFF_LOOKAHEAD_CTRL;
    m_ExtLAControl.Header.BufferSz = sizeof(m_ExtLAControl);
    m_ExtLAControl.LookAheadDepth = pInParams->nLADepth? pInParams->nLADepth : 40;
    m_ExtLAControl.NumOutStream = 0;
    m_ExtLAControl.BPyramid = (mfxU16)(pInParams->bEnableBPyramid ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF);

    m_mfxPreEncParams.mfx.GopPicSize = pInParams->GopPicSize ? pInParams->GopPicSize : 1500;

    if (pInParams->GopRefDist)
    {
        m_mfxPreEncParams.mfx.GopRefDist = pInParams->GopRefDist;
    }

    if (pInParams->nTargetUsage)
    {
        m_mfxPreEncParams.mfx.TargetUsage = pInParams->nTargetUsage;
    }

    m_PreEncExtParams.push_back((mfxExtBuffer *)&m_ExtLAControl);

    if (!m_PreEncExtParams.empty())
    {
        m_mfxPreEncParams.ExtParam = &m_PreEncExtParams[0]; // vector is stored linearly in memory
        m_mfxPreEncParams.NumExtParam = (mfxU16)m_PreEncExtParams.size();
    }
    return MFX_ERR_NONE;
}

mfxStatus CTranscodingPipeline::AddLaStreams(mfxU16 width, mfxU16 height)
{
    if (m_pmfxPreENC.get() > 0)
    {
        mfxU32 num = m_ExtLAControl.NumOutStream;
        m_numEncoders ++;
        for (mfxU32 i = 0; i < num; i++)
        {
            if (m_ExtLAControl.OutStream[i].Width  == width && m_ExtLAControl.OutStream[i].Height == height)
                return MFX_ERR_NONE;
        }
        MSDK_CHECK_RESULT((sizeof(m_ExtLAControl.OutStream) / sizeof(m_ExtLAControl.OutStream[0])),num + 1 , MFX_ERR_UNSUPPORTED);
        m_ExtLAControl.OutStream[num].Width  = width;
        m_ExtLAControl.OutStream[num].Height = height;
        m_ExtLAControl.NumOutStream = (mfxU16)num + 1;
    }
    return MFX_ERR_NONE;
}

 mfxStatus CTranscodingPipeline::InitVppMfxParams(sInputParams *pInParams)
{
    MSDK_CHECK_POINTER(pInParams,  MFX_ERR_NULL_PTR);
    m_mfxVppParams.AsyncDepth = m_AsyncDepth;

    mfxU16 InPatternFromParent = (mfxU16)((MFX_IOPATTERN_OUT_VIDEO_MEMORY == m_mfxDecParams.IOPattern) ?
        MFX_IOPATTERN_IN_VIDEO_MEMORY : MFX_IOPATTERN_IN_SYSTEM_MEMORY);

    // set memory pattern
    if (m_bUseOpaqueMemory)
    {
        m_mfxVppParams.IOPattern = MFX_IOPATTERN_IN_OPAQUE_MEMORY|MFX_IOPATTERN_OUT_OPAQUE_MEMORY;
    }
    else if (pInParams->bForceSysMem || (MFX_IMPL_SOFTWARE == pInParams->libType))
    {
        m_mfxVppParams.IOPattern = (mfxU16)(InPatternFromParent|MFX_IOPATTERN_OUT_SYSTEM_MEMORY);
    }
    else
    {
        m_mfxVppParams.IOPattern = (mfxU16)(InPatternFromParent|MFX_IOPATTERN_OUT_VIDEO_MEMORY);
    }

    // input frame info
    MSDK_MEMCPY_VAR(m_mfxVppParams.vpp.In, &m_mfxDecParams.mfx.FrameInfo, sizeof(mfxFrameInfo));

    // fill output frame info
    MSDK_MEMCPY_VAR(m_mfxVppParams.vpp.Out, &m_mfxVppParams.vpp.In, sizeof(mfxFrameInfo));


    if (pInParams->bEnableDeinterlacing)
        m_mfxVppParams.vpp.Out.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;


    // Resizing
    if (pInParams->nDstWidth)
    {
        m_mfxVppParams.vpp.Out.CropW = pInParams->nDstWidth;
        // WA for HEVCe HW. It requires 32 bit alignment so far.
        // With others codecs MSDK_ALIGN16 macro can be used.
        m_mfxVppParams.vpp.Out.Width     = MSDK_ALIGN32(pInParams->nDstWidth);
    }

    // Framerate conversion
    if(pInParams->dEncoderFrameRate)
    {
        ConvertFrameRate(pInParams->dEncoderFrameRate, &m_mfxVppParams.vpp.Out.FrameRateExtN, &m_mfxVppParams.vpp.Out.FrameRateExtD);
    }

    if (pInParams->nDstHeight)
    {
        m_mfxVppParams.vpp.Out.CropH = pInParams->nDstHeight;
        m_mfxVppParams.vpp.Out.Height    = (MFX_PICSTRUCT_PROGRESSIVE == m_mfxVppParams.vpp.Out.PicStruct)?
            MSDK_ALIGN16(pInParams->nDstHeight) : MSDK_ALIGN32(pInParams->nDstHeight);
    }


    if (pInParams->bEnableDeinterlacing)
    {
        // If stream were interlaced before then 32 bit alignment were applied.
        // Discard 32 bit alignment as progressive doesn't require it.
        m_mfxVppParams.vpp.Out.Height = MSDK_ALIGN16(m_mfxVppParams.vpp.Out.CropH);
        m_mfxVppParams.vpp.Out.Width  = MSDK_ALIGN16(m_mfxVppParams.vpp.Out.CropW);
    }


    // configure and attach external parameters
    mfxStatus sts = AllocAndInitVppDoNotUse(pInParams);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    if(m_VppDoNotUse.NumAlg)
    {
        m_VppExtParamsStorage.ExtBuffers.push_back((mfxExtBuffer *)&m_VppDoNotUse);
    }

    //--- Setting output FourCC type (input type is taken from m_mfxDecParams)
    if (pInParams->EncoderFourCC)
    {
        m_mfxVppParams.vpp.Out.FourCC = pInParams->EncoderFourCC;
        m_mfxVppParams.vpp.Out.ChromaFormat = FourCCToChroma(pInParams->EncoderFourCC);
    }

    /* VPP Comp Init */
    if (((pInParams->eModeExt == VppComp) || (pInParams->eModeExt == VppCompOnly)) &&
        (pInParams->numSurf4Comp != 0))
    {
        m_nVPPCompEnable = pInParams->eModeExt;
        m_VppCompParams.Header.BufferId = MFX_EXTBUFF_VPP_COMPOSITE;
        m_VppCompParams.Header.BufferSz = sizeof(mfxExtVPPComposite);
        m_VppCompParams.NumInputStream = (mfxU16)pInParams->numSurf4Comp;
        m_VppCompParams.InputStream = (mfxVPPCompInputStream *)malloc(sizeof(mfxVPPCompInputStream)*
            m_VppCompParams.NumInputStream);
        MSDK_CHECK_POINTER(m_VppCompParams.InputStream,MFX_ERR_NULL_PTR);

        // stream params
        /* if input streams in NV12 format background color should be in YUV format too
        * The same for RGB4 input, background color should be in ARGB format
        * */
        /* back color in YUV */
        m_VppCompParams.Y = 0x10;
        m_VppCompParams.U = 0x80;
        m_VppCompParams.V = 0x80;

        MSDK_CHECK_POINTER(pInParams->pVppCompDstRects,MFX_ERR_NULL_PTR);
        for (mfxU32 i = 0; i < pInParams->numSurf4Comp; i++)
        {
            m_VppCompParams.InputStream[i].DstX = pInParams->pVppCompDstRects[i].DstX;
            m_VppCompParams.InputStream[i].DstY = pInParams->pVppCompDstRects[i].DstY;
            m_VppCompParams.InputStream[i].DstW = pInParams->pVppCompDstRects[i].DstW;
            m_VppCompParams.InputStream[i].DstH = pInParams->pVppCompDstRects[i].DstH;
            m_VppCompParams.InputStream[i].GlobalAlpha = 0;
            m_VppCompParams.InputStream[i].GlobalAlphaEnable = 0;
            m_VppCompParams.InputStream[i].PixelAlphaEnable = 0;

            m_VppCompParams.InputStream[i].LumaKeyEnable = 0;
            m_VppCompParams.InputStream[i].LumaKeyMin = 0;
            m_VppCompParams.InputStream[i].LumaKeyMax = 0;
        }

        m_VppExtParamsStorage.ExtBuffers.push_back((mfxExtBuffer *)&m_VppCompParams);
    } // if ( ((pInParams->eModeExt == VppComp) || (pInParams->eModeExt == VppCompOnly)) &&

    if (m_bUseOpaqueMemory)
        m_VppExtParamsStorage.ExtBuffers.push_back((mfxExtBuffer *)&m_VppOpaqueAlloc);
    if (pInParams->bIsMVC)
        m_VppExtParamsStorage.ExtBuffers.push_back((mfxExtBuffer *)&m_MVCSeqDesc);

    // Initializing m_VppExtParamsStorage here, to put all extra filters (created in Init function) to the end of buffer
    m_VppExtParamsStorage.Init(pInParams);

    // Adding

    m_mfxVppParams.ExtParam = &m_VppExtParamsStorage.ExtBuffers[0]; // vector is stored linearly in memory
    m_mfxVppParams.NumExtParam = (mfxU16)m_VppExtParamsStorage.ExtBuffers.size();

    return MFX_ERR_NONE;

} //mfxStatus CTranscodingPipeline::InitMfxVppParams(sInputParams *pInParams)

mfxStatus CTranscodingPipeline::InitPluginMfxParams(sInputParams *pInParams)
{
    MSDK_CHECK_POINTER(pInParams,  MFX_ERR_NULL_PTR);

    mfxU16 parentPattern = m_bIsVpp ? m_mfxVppParams.IOPattern : m_mfxDecParams.IOPattern;
    mfxU16 InPatternFromParent = (mfxU16)((MFX_IOPATTERN_OUT_VIDEO_MEMORY && parentPattern) ?
        MFX_IOPATTERN_IN_VIDEO_MEMORY : MFX_IOPATTERN_IN_SYSTEM_MEMORY);

    // set memory pattern
    if (m_bUseOpaqueMemory)
        m_mfxPluginParams.IOPattern = MFX_IOPATTERN_IN_OPAQUE_MEMORY|MFX_IOPATTERN_OUT_OPAQUE_MEMORY;
    else if (pInParams->bForceSysMem || (MFX_IMPL_SOFTWARE == pInParams->libType))
        m_mfxPluginParams.IOPattern = (mfxU16)(InPatternFromParent|MFX_IOPATTERN_OUT_SYSTEM_MEMORY);
    else
        m_mfxPluginParams.IOPattern = (mfxU16)(InPatternFromParent|MFX_IOPATTERN_OUT_VIDEO_MEMORY);

    m_mfxPluginParams.AsyncDepth = m_AsyncDepth;

    // input frame info
    if (m_bIsVpp)
    {
        MSDK_MEMCPY_VAR(m_mfxPluginParams.vpp.In, &m_mfxVppParams.vpp.Out, sizeof(mfxFrameInfo));
    }
    else
    {
        MSDK_MEMCPY_VAR(m_mfxPluginParams.vpp.In, &m_mfxDecParams.mfx.FrameInfo, sizeof(mfxFrameInfo));
    }

    // fill output frame info
    // in case of rotation plugin sample output frameinfo is same as input
    MSDK_MEMCPY_VAR(m_mfxPluginParams.vpp.Out, &m_mfxPluginParams.vpp.In, sizeof(mfxFrameInfo));

    // configure and attach external parameters
    if (m_bUseOpaqueMemory)
        m_PluginExtParams.push_back((mfxExtBuffer *)&m_PluginOpaqueAlloc);

    if (!m_PluginExtParams.empty())
    {
        m_mfxPluginParams.ExtParam = &m_PluginExtParams[0]; // vector is stored linearly in memory
        m_mfxPluginParams.NumExtParam = (mfxU16)m_PluginExtParams.size();
    }

    return MFX_ERR_NONE;

} //mfxStatus CTranscodingPipeline::InitMfxVppParams(sInputParams *pInParams)

mfxStatus CTranscodingPipeline::AllocFrames(mfxFrameAllocRequest *pRequest, bool isDecAlloc)
{
    mfxStatus sts = MFX_ERR_NONE;

    mfxU16 nSurfNum = 0; // number of surfaces
    mfxU16 i;

    nSurfNum = pRequest->NumFrameMin = pRequest->NumFrameSuggested;
    msdk_printf(MSDK_STRING("Pipeline surfaces number: %d\n"),nSurfNum);

    mfxFrameAllocResponse *pResponse = isDecAlloc ? &m_mfxDecResponse : &m_mfxEncResponse;

    // no actual memory is allocated if opaque memory type is used
    if (!m_bUseOpaqueMemory)
    {
        sts = m_pMFXAllocator->Alloc(m_pMFXAllocator->pthis, pRequest, pResponse);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    }

    for (i = 0; i < nSurfNum; i++)
    {
        mfxFrameSurface1 *surface = new mfxFrameSurface1;
        MSDK_CHECK_POINTER(surface, MFX_ERR_MEMORY_ALLOC);

        MSDK_ZERO_MEMORY(*surface);
        MSDK_MEMCPY_VAR(surface->Info, &(pRequest->Info), sizeof(mfxFrameInfo));

        // no actual memory is allocated if opaque memory type is used (surface pointers and MemId field remain 0)
        if (!m_bUseOpaqueMemory)
        {
            surface->Data.MemId = pResponse->mids[i];
        }

        (isDecAlloc) ? m_pSurfaceDecPool.push_back(surface):m_pSurfaceEncPool.push_back(surface);
    }

    (isDecAlloc) ? m_DecSurfaceType = pRequest->Type : m_EncSurfaceType = pRequest->Type;

    return MFX_ERR_NONE;

} // mfxStatus CTranscodingPipeline::AllocFrames(Component* pComp, mfxFrameAllocResponse* pMfxResponse, mfxVideoParam* pMfxVideoParam)

//return true if correct
static bool CheckAsyncDepth(mfxFrameAllocRequest &curReq, mfxU16 asyncDepth)
{
    return (curReq.NumFrameSuggested >= asyncDepth);
}

static mfxStatus CorrectAsyncDepth(mfxFrameAllocRequest &curReq, mfxU16 asyncDepth)
{
    mfxStatus sts = MFX_ERR_NONE;
    if (!CheckAsyncDepth(curReq, asyncDepth))
    {
        sts = MFX_ERR_MEMORY_ALLOC;
    }
    else
    {
        // If surfaces are shared by 2 components, c1 and c2. NumSurf = c1_out + c2_in - AsyncDepth + 1
        curReq.NumFrameSuggested = 2*curReq.NumFrameSuggested - asyncDepth + 1;
        curReq.NumFrameMin = curReq.NumFrameSuggested;
    }

    return sts;
}

static void SumAllocRequest(mfxFrameAllocRequest  &curReq, mfxFrameAllocRequest  &newReq)
{
    curReq.NumFrameSuggested = curReq.NumFrameSuggested + newReq.NumFrameSuggested;
    curReq.NumFrameMin = curReq.NumFrameSuggested;
    curReq.Type = curReq.Type | newReq.Type;

    if ((curReq.Type & MFX_MEMTYPE_SYSTEM_MEMORY) && ((curReq.Type & 0xf0) != MFX_MEMTYPE_SYSTEM_MEMORY))
        curReq.Type = (mfxU16)(curReq.Type & (~ MFX_MEMTYPE_SYSTEM_MEMORY));
    if ((curReq.Type & MFX_MEMTYPE_DXVA2_PROCESSOR_TARGET) && ((curReq.Type & 0xf0) != MFX_MEMTYPE_DXVA2_PROCESSOR_TARGET))
        curReq.Type = (mfxU16)(curReq.Type & (~ MFX_MEMTYPE_DXVA2_PROCESSOR_TARGET));

    if (curReq.Info.Width == 0)
    {
        curReq.Info = newReq.Info;
    }
    else
    {
        curReq.Info.Width  = curReq.Info.Width < newReq.Info.Width ? newReq.Info.Width : curReq.Info.Width ;
        curReq.Info.Height = curReq.Info.Height < newReq.Info.Height ? newReq.Info.Height : curReq.Info.Height ;
    }
}

static void CheckAllocRequest(mfxFrameAllocRequest  &curReq, mfxFrameAllocRequest  &newReq)
{
    curReq.NumFrameSuggested = curReq.NumFrameSuggested <  newReq.NumFrameSuggested ? newReq.NumFrameSuggested : curReq.NumFrameSuggested;
    curReq.NumFrameMin = curReq.NumFrameSuggested;
    curReq.Type = curReq.Type | newReq.Type;

    if ((curReq.Type & MFX_MEMTYPE_SYSTEM_MEMORY) && ((curReq.Type & 0xf0) != MFX_MEMTYPE_SYSTEM_MEMORY))
        curReq.Type = (mfxU16)(curReq.Type & (~ MFX_MEMTYPE_SYSTEM_MEMORY));
    if ((curReq.Type & MFX_MEMTYPE_DXVA2_PROCESSOR_TARGET) && ((curReq.Type & 0xf0) != MFX_MEMTYPE_DXVA2_PROCESSOR_TARGET))
        curReq.Type = (mfxU16)(curReq.Type & (~ MFX_MEMTYPE_DXVA2_PROCESSOR_TARGET));

    if (curReq.Info.Width == 0)
    {
        curReq.Info = newReq.Info;
    }
    else
    {
        curReq.Info.Width  = curReq.Info.Width < newReq.Info.Width ? newReq.Info.Width : curReq.Info.Width ;
        curReq.Info.Height = curReq.Info.Height < newReq.Info.Height ? newReq.Info.Height : curReq.Info.Height ;
    }
}

mfxStatus CTranscodingPipeline::AllocFrames()
{
    mfxStatus sts = MFX_ERR_NONE;
    bool bAddFrames = true;   // correct shared pool between session

    mfxFrameAllocRequest DecOut;
    mfxFrameAllocRequest VPPOut;

    MSDK_ZERO_MEMORY(DecOut);
    MSDK_ZERO_MEMORY(VPPOut);

    sts = CalculateNumberOfReqFrames(DecOut,VPPOut);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    if (VPPOut.NumFrameSuggested)
    {
        if (bAddFrames)
        {
            SumAllocRequest(VPPOut, m_Request);
            bAddFrames = false;
        }
        sts = CorrectAsyncDepth(VPPOut, m_AsyncDepth);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

#ifdef LIBVA_SUPPORT
        if ((m_nVPPCompEnable == VppCompOnly) &&
            (m_libvaBackend == MFX_LIBVA_DRM_MODESET))
        {
            VPPOut.Type |= MFX_MEMTYPE_EXPORT_FRAME;
        }
#endif

        sts = AllocFrames(&VPPOut, false);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    }
    if (DecOut.NumFrameSuggested)
    {
        if (bAddFrames)
        {
            SumAllocRequest(DecOut, m_Request);
            bAddFrames = false;
        }

        if (m_bDecodeEnable )
        {
            if(0 == m_nVPPCompEnable || !m_bUseOpaqueMemory)
            {
                //--- We should not multiply surface number in case of composition with opaque. Separate pool will be allocated for that case
                sts = CorrectAsyncDepth(DecOut, m_AsyncDepth);
            }
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

            // AllocId just opaque handle which allow separate decoder requests in case of VPP Composition with external allocator
            static mfxU32 mark_alloc = 0;
            m_mfxDecParams.AllocId = mark_alloc;
            DecOut.AllocId = mark_alloc;
            if(m_nVPPCompEnable) // WORKAROUND: Remove this if clause after problem with AllocID is fixed in library (mark_alloc++ should be left here)
            {
                mark_alloc++;
            }

            sts = AllocFrames(&DecOut, true);
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
            sts = CorrectPreEncAuxPool((VPPOut.NumFrameSuggested ? VPPOut.NumFrameSuggested : DecOut.NumFrameSuggested) + m_AsyncDepth);
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
            sts = AllocPreEncAuxPool();
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
        }
        else if((m_nVPPCompEnable==VppComp || m_nVPPCompEnable==VppCompOnly) && m_bUseOpaqueMemory)
        {
            //--- N->1 case, allocating empty pool for opaque only
            sts = AllocFrames(&DecOut, true);
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
        }
        else
        {
            if ((m_pParentPipeline) &&
                (0 == m_nVPPCompEnable) /* case if 1_to_N  */)
            {
                m_pParentPipeline->CorrectNumberOfAllocatedFrames(&DecOut);
                sts = m_pParentPipeline->CorrectPreEncAuxPool(VPPOut.NumFrameSuggested + DecOut.NumFrameSuggested + m_AsyncDepth);
                MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
            }
        }
    }
    return MFX_ERR_NONE;
}

mfxStatus CTranscodingPipeline::CalculateNumberOfReqFrames(mfxFrameAllocRequest  &pDecOut, mfxFrameAllocRequest  &pVPPOut)
{
    mfxStatus sts = MFX_ERR_NONE;
    mfxFrameAllocRequest *pSumRequest = &pDecOut;

    memset(&pDecOut,0,sizeof(pDecOut));
    memset(&pVPPOut,0,sizeof(pVPPOut));

    mfxFrameAllocRequest DecRequest;
    MSDK_ZERO_MEMORY(DecRequest);

    if (m_pmfxDEC.get())
    {
        sts = m_pmfxDEC.get()->QueryIOSurf(&m_mfxDecParams, &DecRequest);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

        if (!CheckAsyncDepth(DecRequest, m_mfxDecParams.AsyncDepth))
                return MFX_ERR_MEMORY_ALLOC;

        SumAllocRequest(*pSumRequest, DecRequest);
    }
    if (m_pmfxVPP.get())
    {
        mfxFrameAllocRequest VppRequest[2];

        MSDK_ZERO_MEMORY(VppRequest);
        if (m_bIsPlugin && m_bIsVpp)
        {
            sts = m_pmfxVPP.get()->QueryIOSurf(&m_mfxPluginParams, &(VppRequest[0]), &m_mfxVppParams);
            if (!CheckAsyncDepth(VppRequest[0], m_mfxPluginParams.AsyncDepth) ||
                !CheckAsyncDepth(VppRequest[1], m_mfxPluginParams.AsyncDepth) ||
                !CheckAsyncDepth(VppRequest[0], m_mfxVppParams.AsyncDepth) ||
                !CheckAsyncDepth(VppRequest[1], m_mfxVppParams.AsyncDepth))
                return MFX_ERR_MEMORY_ALLOC;
        }
        else if (m_bIsPlugin)
        {
            sts = m_pmfxVPP.get()->QueryIOSurf(&m_mfxPluginParams, &(VppRequest[0]));
            if (!CheckAsyncDepth(VppRequest[0], m_mfxPluginParams.AsyncDepth) ||
                !CheckAsyncDepth(VppRequest[1], m_mfxPluginParams.AsyncDepth))
                return MFX_ERR_MEMORY_ALLOC;
        }
        else
        {
            sts = m_pmfxVPP.get()->QueryIOSurf(&m_mfxVppParams, &(VppRequest[0]));
            if (!CheckAsyncDepth(VppRequest[0], m_mfxVppParams.AsyncDepth) ||
                !CheckAsyncDepth(VppRequest[1], m_mfxVppParams.AsyncDepth))
                return MFX_ERR_MEMORY_ALLOC;
        }

        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

        SumAllocRequest(*pSumRequest, VppRequest[0]);
        pSumRequest = &pVPPOut;
        SumAllocRequest(*pSumRequest, VppRequest[1]);
    }
    if (m_pmfxPreENC.get())
    {
        mfxFrameAllocRequest PreEncRequest;

        MSDK_ZERO_MEMORY(PreEncRequest);
        sts = m_pmfxPreENC.get()->QueryIOSurf(&m_mfxPreEncParams, &PreEncRequest);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

        if (!CheckAsyncDepth(PreEncRequest, m_mfxPreEncParams.AsyncDepth))
            return MFX_ERR_MEMORY_ALLOC;
        SumAllocRequest(*pSumRequest, PreEncRequest);
    }
    if (m_pmfxENC.get())
    {
        mfxFrameAllocRequest EncRequest;

        MSDK_ZERO_MEMORY(EncRequest);

        sts = m_pmfxENC.get()->QueryIOSurf(&m_mfxEncParams, &EncRequest);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

        if (!CheckAsyncDepth(EncRequest, m_mfxEncParams.AsyncDepth))
            return MFX_ERR_MEMORY_ALLOC;
        SumAllocRequest(*pSumRequest, EncRequest);
    }

    if(!pSumRequest->Type && m_pmfxDEC.get())
    {
        //--- If noone has set type to VPP request type yet, set it now basing on decoder request type
        pSumRequest->Type = MFX_MEMTYPE_BASE(DecRequest.Type) | MFX_MEMTYPE_FROM_VPPOUT;
    }

    return MFX_ERR_NONE;
}
void CTranscodingPipeline::CorrectNumberOfAllocatedFrames(mfxFrameAllocRequest  *pRequest)
{
    CheckAllocRequest(m_Request, *pRequest);
}

mfxStatus CTranscodingPipeline::InitOpaqueAllocBuffers()
{
    if (m_pmfxDEC.get() ||
        (m_bUseOpaqueMemory && (m_nVPPCompEnable==VppComp || m_nVPPCompEnable==VppCompOnly) && m_pSurfaceDecPool.size()))
    {
        m_DecOpaqueAlloc.Out.Surfaces = &m_pSurfaceDecPool[0]; // vestor is stored linearly in memory
        m_DecOpaqueAlloc.Out.NumSurface = (mfxU16)m_pSurfaceDecPool.size();
        m_DecOpaqueAlloc.Out.Type = (mfxU16)(MFX_MEMTYPE_BASE(m_DecSurfaceType) | MFX_MEMTYPE_FROM_DECODE);
    }
    else
    {
        // if no decoder in the pipeline we need to query m_DecOpaqueAlloc structure from parent sink pipeline
        m_DecOpaqueAlloc = m_pParentPipeline->GetDecOpaqueAlloc();
    }

    if (m_pmfxVPP.get())
    {
        m_EncOpaqueAlloc.In.Surfaces = &m_pSurfaceEncPool[0];
        m_EncOpaqueAlloc.In.NumSurface = (mfxU16)m_pSurfaceEncPool.size();
        m_EncOpaqueAlloc.In.Type = (mfxU16)(MFX_MEMTYPE_BASE(m_EncSurfaceType) | MFX_MEMTYPE_FROM_ENCODE);

        // decode will be connected with either VPP or Plugin
        if (m_bIsVpp)
        {
            m_VppOpaqueAlloc.In = m_DecOpaqueAlloc.Out;
        }
        else if (m_bIsPlugin)
        {
            m_PluginOpaqueAlloc.In = m_DecOpaqueAlloc.Out;
        }
        else
            return MFX_ERR_UNSUPPORTED;

        // encode will be connected with either Plugin or VPP
        if (m_bIsPlugin)
            m_PluginOpaqueAlloc.Out = m_EncOpaqueAlloc.In;
        else if (m_bIsVpp)
            m_VppOpaqueAlloc.Out = m_EncOpaqueAlloc.In;
        else
            return MFX_ERR_UNSUPPORTED;
    }
    else if (m_pmfxENC.get() || m_pmfxPreENC.get())
    {
        m_EncOpaqueAlloc.In = m_DecOpaqueAlloc.Out;
    }
    if (m_pmfxPreENC.get())
    {
        m_PreEncOpaqueAlloc.In = m_EncOpaqueAlloc.In;
    }


    return MFX_ERR_NONE;
}

void CTranscodingPipeline::FreeFrames()
{
    // free mfxFrameSurface structures and arrays of pointers
    mfxU32 i;
    for (i = 0; i < m_pSurfaceDecPool.size(); i++)
    {
        MSDK_SAFE_DELETE(m_pSurfaceDecPool[i]);
    }

    m_pSurfaceDecPool.clear();

    for (i = 0; i < m_pSurfaceEncPool.size(); i++)
    {
        MSDK_SAFE_DELETE(m_pSurfaceEncPool[i]);
    }

    m_pSurfaceEncPool.clear();

    if (m_pMFXAllocator && !m_bUseOpaqueMemory)
    {
        m_pMFXAllocator->Free(m_pMFXAllocator->pthis, &m_mfxEncResponse);
        m_pMFXAllocator->Free(m_pMFXAllocator->pthis, &m_mfxDecResponse);
    }
} // CTranscodingPipeline::FreeFrames()

mfxStatus CTranscodingPipeline::Init(sInputParams *pParams,
                                     MFXFrameAllocator *pMFXAllocator,
                                     void* hdl,
                                     CTranscodingPipeline *pParentPipeline,
                                     SafetySurfaceBuffer  *pBuffer,
                                     BitstreamProcessor   *pBSProc)
{
    MSDK_CHECK_POINTER(pParams, MFX_ERR_NULL_PTR);
    MSDK_CHECK_POINTER(pMFXAllocator, MFX_ERR_NULL_PTR);
    mfxStatus sts = MFX_ERR_NONE;
    m_MaxFramesForTranscode = pParams->MaxFrameNumber;
    // use external allocator
    m_pMFXAllocator = pMFXAllocator;

    m_pParentPipeline = pParentPipeline;

    m_nTimeout = pParams->nTimeout;
    m_AsyncDepth = (0 == pParams->nAsyncDepth)? 1: pParams->nAsyncDepth;
    m_FrameNumberPreference = pParams->FrameNumberPreference;
    m_numEncoders = 0;

    statisticsWindowSize = pParams->statisticsWindowSize;

    if (m_bEncodeEnable)
    {
        m_pBSStore.reset(new ExtendedBSStore(m_AsyncDepth));
    }

    if (pBSProc)
    {
        sts = CheckExternalBSProcessor(pBSProc);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
        m_pBSProcessor = pBSProc;
    }
    else
    {
        return MFX_ERR_UNSUPPORTED;
    }

    // Determine processing mode
    switch(pParams->eMode)
    {
    case Native:
        break;
    case Sink:
        if ((VppComp == pParams->eModeExt) || (VppCompOnly == pParams->eModeExt))
        {
            if ((NULL != pParentPipeline) && (NULL != pParentPipeline->m_pMFXAllocator))
                m_pMFXAllocator = pParentPipeline->m_pMFXAllocator;
        }

        m_bEncodeEnable = false; // decode only
        break;
    case Source:
        // for heterogeneous pipeline use parent allocator
        MSDK_CHECK_POINTER(pParentPipeline, MFX_ERR_NULL_PTR);
        m_pMFXAllocator = pParentPipeline->m_pMFXAllocator;
        m_bDecodeEnable = false; // encode only
        /**/
        if ((pParams->m_hwdev != NULL) && (VppCompOnly == pParams->eModeExt))
        {
#if defined(_WIN32) || defined(_WIN64)
            m_hwdev4Rendering = new CDecodeD3DRender;

            m_hwdev4Rendering->SetHWDevice(pParams->m_hwdev);

            sWindowParams RenderParam;

            memset(&RenderParam, 0, sizeof(sWindowParams));

            RenderParam.lpWindowName = MSDK_STRING("sample_multi_transcode");
            RenderParam.nx           = 0;
            RenderParam.ny           = 0;
            RenderParam.nWidth       = pParams->nDstWidth;
            RenderParam.nHeight      = pParams->nDstHeight;
            RenderParam.ncell        = 0;
            RenderParam.nAdapter     = 0;

            RenderParam.lpClassName  = MSDK_STRING("Render Window Class");
            RenderParam.dwStyle      = WS_OVERLAPPEDWINDOW;
            RenderParam.hWndParent   = NULL;
            RenderParam.hMenu        = NULL;
            RenderParam.hInstance    = GetModuleHandle(NULL);
            RenderParam.lpParam      = NULL;
            RenderParam.bFullScreen  = TRUE;

            m_hwdev4Rendering->Init(RenderParam);
#else
            m_hwdev4Rendering = pParams->m_hwdev;
#endif
        }
        break;
    default:
        // unknown mode
        return MFX_ERR_UNSUPPORTED;
    }

    if ((VppComp == pParams->eModeExt) || (VppCompOnly == pParams->eModeExt))
        m_nVPPCompEnable = pParams->eModeExt;

#ifdef LIBVA_SUPPORT
    m_libvaBackend = pParams->libvaBackend;
#endif

    m_pBuffer = pBuffer;

    mfxInitParam initPar;
    mfxExtThreadsParam threadsPar;
    mfxExtBuffer* extBufs[1];

    MSDK_ZERO_MEMORY(initPar);
    MSDK_ZERO_MEMORY(threadsPar);

    // we set version to 1.0 and later we will query actual version of the library which will got leaded
    initPar.Version.Major = 1;
    initPar.Version.Minor = 0;
    initPar.Implementation = pParams->libType;

    init_ext_buffer(threadsPar);

    bool needInitExtPar = false;

    if (pParams->nThreadsNum) {
        threadsPar.NumThread = pParams->nThreadsNum;
        needInitExtPar = true;
    }
    if (needInitExtPar) {
        extBufs[0] = (mfxExtBuffer*)&threadsPar;
        initPar.ExtParam = extBufs;
        initPar.NumExtParam = 1;
    }

    //--- GPU Copy settings
    initPar.GPUCopy = pParams->nGpuCopyMode;

    // init session
    m_pmfxSession.reset(new MFXVideoSession);

    if (initPar.Implementation & MFX_IMPL_HARDWARE_ANY)
    {
        // try search for MSDK on all display adapters
        sts = m_pmfxSession->InitEx(initPar);

        // MSDK API version may have no support for multiple adapters - then try initialize on the default
        if (MFX_ERR_NONE != sts) {
            initPar.Implementation = (initPar.Implementation & (!MFX_IMPL_HARDWARE_ANY)) | MFX_IMPL_HARDWARE;
            sts = m_pmfxSession->InitEx(initPar);
        }
    }
    else
        sts = m_pmfxSession->InitEx(initPar);

    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // check the API version of actually loaded library
    sts = m_pmfxSession->QueryVersion(&m_Version);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    sts = CheckRequiredAPIVersion(m_Version, pParams);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    mfxIMPL impl = 0;
    m_pmfxSession->QueryIMPL(&impl);

    // opaque memory feature is available starting with API 1.3 and
    // can be used within 1 intra session or joined inter sessions only
    if (m_Version.Major >= 1 && m_Version.Minor >= 3 &&
        (pParams->eMode == Native || pParams->bIsJoin) )
    {
        m_bUseOpaqueMemory = true;
    }

    if (!pParams->bUseOpaqueMemory || pParams->EncodeId==MFX_FOURCC_DUMP) // Don't use opaque in case of yuv output or if it was specified explicitly
        m_bUseOpaqueMemory = false;

    // Media SDK session doesn't require external allocator if the application uses opaque memory
    if (!m_bUseOpaqueMemory)
    {
        sts = m_pmfxSession->SetFrameAllocator(m_pMFXAllocator);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    }

    bool bIsInterOrJoined = pParams->eMode == Sink || pParams->eMode == Source || pParams->bIsJoin;

    mfxHandleType handleType = (mfxHandleType)0;
    bool bIsMustSetExternalHandle = 0;

    if (MFX_IMPL_VIA_D3D11 == MFX_IMPL_VIA_MASK(impl))
    {
        handleType = MFX_HANDLE_D3D11_DEVICE;
        bIsMustSetExternalHandle = false;
    }
    else if (MFX_IMPL_VIA_D3D9 == MFX_IMPL_VIA_MASK(impl))
    {
        handleType = MFX_HANDLE_D3D9_DEVICE_MANAGER;
        bIsMustSetExternalHandle = false;
    }
#ifdef LIBVA_SUPPORT
    else if (MFX_IMPL_VIA_VAAPI == MFX_IMPL_VIA_MASK(impl))
    {
        handleType = MFX_HANDLE_VA_DISPLAY;
        bIsMustSetExternalHandle = true;
    }
#endif

    if (hdl && (bIsMustSetExternalHandle || (bIsInterOrJoined || !m_bUseOpaqueMemory)))
    {
      sts = m_pmfxSession->SetHandle(handleType, hdl);
      MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
      m_hdl = hdl; // save handle
    }

    // Joining sessions if required
    if (pParams->bIsJoin && pParentPipeline)
    {
        sts = pParentPipeline->Join(m_pmfxSession.get());
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
        m_bIsJoinSession = true;
    }

    // Initialize pipeline components following downstream direction
    // Pre-init methods fill parameters and create components

    // Decode component initialization
    sts = DecodePreInit(pParams);
    if (MFX_ERR_MORE_DATA == sts)
        return sts;
    else
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // VPP component initialization
    sts = VPPPreInit(pParams);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // LA component initialization
    sts = PreEncPreInit(pParams);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // Encode component initialization
    if (m_nVPPCompEnable != VppCompOnly)
    {
        sts = EncodePreInit(pParams);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    }

    // Frames allocation for all component
    if (Native == pParams->eMode)
    {
        sts = AllocFrames();
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    }
    else if (Source == pParams->eMode)// need allocate frames only for VPP and Encode if VPP exist
    {
        if (!m_bDecodeEnable)
        {
            sts = AllocFrames();
            MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
        }
    }
    // if sink - suspended allocation

    // common session settings
    if (m_Version.Major >= 1 && m_Version.Minor >= 1)
        sts = m_pmfxSession->SetPriority(pParams->priority);

    // if sink - suspended allocation
    if (Native !=  pParams->eMode)
        return sts;

    // after surfaces arrays are allocated configure mfxOpaqueAlloc buffers to be passed to components' Inits
    if (m_bUseOpaqueMemory)
    {
        sts = InitOpaqueAllocBuffers();
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    }

    // Init decode
    if (m_pmfxDEC.get())
    {
        sts = m_pmfxDEC->Init(&m_mfxDecParams);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    }

    // Init VPP
    if (m_pmfxVPP.get())
    {
        if (m_bIsPlugin && m_bIsVpp)
            sts = m_pmfxVPP->Init(&m_mfxPluginParams, &m_mfxVppParams);
        else if (m_bIsPlugin)
            sts = m_pmfxVPP->Init(&m_mfxPluginParams);
        else
            sts = m_pmfxVPP->Init(&m_mfxVppParams);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    }
    // LA initialization
    if (m_pmfxPreENC.get())
    {
        sts = m_pmfxPreENC->Init(&m_mfxEncParams);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    }
    // Init encode
    if (m_pmfxENC.get())
    {
        sts = m_pmfxENC->Init(&m_mfxEncParams);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    }

    m_bIsInit = true;
    return sts;

} //mfxStatus CTranscodingPipeline::Init(sInputParams *pParams)


mfxStatus CTranscodingPipeline::CompleteInit()
{
    mfxStatus sts = MFX_ERR_NONE;

    if (m_bIsInit)
        return MFX_ERR_NONE;

    // need to allocate remaining frames
    if (m_bDecodeEnable)
    {
        sts = AllocFrames();
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    }

    // after surfaces arrays are allocated configure mfxOpaqueAlloc buffers to be passed to components' Inits
    if (m_bUseOpaqueMemory)
    {
        sts = InitOpaqueAllocBuffers();
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    }

        // Init decode
    if (m_pmfxDEC.get())
    {
        sts = m_pmfxDEC->Init(&m_mfxDecParams);
        if (MFX_WRN_PARTIAL_ACCELERATION == sts)
        {
            msdk_printf(MSDK_STRING("WARNING: partial acceleration\n"));
            MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
        }
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    }

    // Init vpp
    if (m_pmfxVPP.get())
    {
        if (m_bIsPlugin && m_bIsVpp)
            sts = m_pmfxVPP->Init(&m_mfxPluginParams, &m_mfxVppParams);
        else if (m_bIsPlugin)
            sts = m_pmfxVPP->Init(&m_mfxPluginParams);
        else
            sts = m_pmfxVPP->Init(&m_mfxVppParams);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    }
        // Pre init encode
    if (m_pmfxPreENC.get())
    {
        sts = m_pmfxPreENC->Init(&m_mfxPreEncParams);
        if (MFX_WRN_PARTIAL_ACCELERATION == sts)
        {
            msdk_printf(MSDK_STRING("WARNING: partial acceleration\n"));
            MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
        }
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    }
    // Init encode
    if (m_pmfxENC.get())
    {
        sts = m_pmfxENC->Init(&m_mfxEncParams);
        if (MFX_WRN_PARTIAL_ACCELERATION == sts)
        {
            msdk_printf(MSDK_STRING("WARNING: partial acceleration\n"));
            MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
        }
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    }

    m_bIsInit = true;

    return sts;
} // mfxStatus CTranscodingPipeline::CompleteInit()
mfxFrameSurface1* CTranscodingPipeline::GetFreeSurface(bool isDec)
{
    SurfPointersArray & workArray = isDec ? m_pSurfaceDecPool : m_pSurfaceEncPool;

    for (mfxU32 i = 0; i < workArray.size(); i++)
    {
        if (!workArray[i]->Data.Locked)
            return workArray[i];
    }
    return NULL;
} // mfxFrameSurface1* CTranscodingPipeline::GetFreeSurface(bool isDec)

PreEncAuxBuffer*  CTranscodingPipeline::GetFreePreEncAuxBuffer()
{
    for(mfxU32 i = 0; i < m_pPreEncAuxPool.size(); i++)
    {
        if (!m_pPreEncAuxPool[i].Locked)
            return &(m_pPreEncAuxPool[i]);
    }
    return NULL;
}

void CTranscodingPipeline::LockPreEncAuxBuffer(PreEncAuxBuffer* pBuff)
{
    if (!pBuff) return;
    for (mfxU32 i=0; i < m_numEncoders; i++)
    {
        msdk_atomic_inc16(&pBuff->Locked);
    }
}

void CTranscodingPipeline::UnPreEncAuxBuffer(PreEncAuxBuffer* pBuff)
{
    if (!pBuff) return;
    msdk_atomic_dec16(&pBuff->Locked);
}

mfxU32 CTranscodingPipeline::GetNumFramesForReset()
{
    AutomaticMutex guard(m_mReset);
    return m_NumFramesForReset;
}

void CTranscodingPipeline::SetNumFramesForReset(mfxU32 nFrames)
{
    AutomaticMutex guard(m_mReset);
    m_NumFramesForReset = nFrames;
}

void CTranscodingPipeline::Close()
{
    if (m_pmfxDEC.get())
        m_pmfxDEC->Close();

    if (m_pmfxENC.get())
        m_pmfxENC->Close();

    if (m_pmfxVPP.get())
        m_pmfxVPP->Close();

    if (m_pUserDecoderPlugin.get())
        m_pUserDecoderPlugin.reset();

    if (m_pUserEncoderPlugin.get())
        m_pUserEncoderPlugin.reset();

    if (m_pUserEncPlugin.get())
        m_pUserEncPlugin.reset();


    FreeVppDoNotUse();
    FreeMVCSeqDesc();
    FreePreEncAuxPool();

    if (m_VppCompParams.InputStream != NULL)
        free(m_VppCompParams.InputStream);

    m_EncExtParams.clear();
    m_DecExtParams.clear();
    m_VppExtParamsStorage.Clear();
    m_PreEncExtParams.clear();

    if (m_bIsJoinSession)
    {
        //m_pmfxSession->DisjoinSession();
        m_bIsJoinSession = false;
    }

    // free allocated surfaces AFTER closing components
    FreeFrames();

    m_bIsInit = false;

} // void CTranscodingPipeline::Close()

mfxStatus CTranscodingPipeline::AllocAndInitVppDoNotUse(sInputParams *pInParams)
{
    std::vector<mfxU32> filtersDisabled;
    if(pInParams->DenoiseLevel==-1)
    {
        filtersDisabled.push_back(MFX_EXTBUFF_VPP_DENOISE); // turn off denoising (on by default)
    }
    filtersDisabled.push_back(MFX_EXTBUFF_VPP_SCENE_ANALYSIS); // turn off scene analysis (on by default)

    m_VppDoNotUse.NumAlg = (mfxU32)filtersDisabled.size();

    m_VppDoNotUse.AlgList = new mfxU32 [m_VppDoNotUse.NumAlg];
    MSDK_CHECK_POINTER(m_VppDoNotUse.AlgList,  MFX_ERR_MEMORY_ALLOC);
    MSDK_MEMCPY(m_VppDoNotUse.AlgList,&filtersDisabled[0],sizeof(mfxU32)*filtersDisabled.size());

    return MFX_ERR_NONE;

} // CTranscodingPipeline::AllocAndInitVppDoNotUse()

void CTranscodingPipeline::FreeVppDoNotUse()
{
    MSDK_SAFE_DELETE_ARRAY(m_VppDoNotUse.AlgList);
}

mfxStatus CTranscodingPipeline::AllocateSufficientBuffer(mfxBitstream* pBS)
{
    MSDK_CHECK_POINTER(pBS, MFX_ERR_NULL_PTR);

    mfxVideoParam par;
    MSDK_ZERO_MEMORY(par);

    // find out the required buffer size
    mfxStatus sts = m_pmfxENC->GetVideoParam(&par);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    mfxU32 new_size = 0;
    // if encoder provided us information about buffer size
    if (0 != par.mfx.BufferSizeInKB)
    {
        new_size = par.mfx.BufferSizeInKB * 1000;
    }
    else
    {
        // trying to guess the size (e.g. for JPEG encoder)
        new_size = (0 == pBS->MaxLength)
            // some heuristic init value
            ? 4 + (par.mfx.FrameInfo.Width * par.mfx.FrameInfo.Height * 3 + 1023)
            // double existing size
            : 2 * pBS->MaxLength;
    }

    sts = ExtendMfxBitstream(pBS, new_size);
    MSDK_CHECK_RESULT_SAFE(sts, MFX_ERR_NONE, sts, WipeMfxBitstream(pBS));

    return MFX_ERR_NONE;
} // CTranscodingPipeline::AllocateSufficientBuffer(mfxBitstream* pBS)

mfxStatus CTranscodingPipeline::Join(MFXVideoSession *pChildSession)
{
    mfxStatus sts = MFX_ERR_NONE;
    MSDK_CHECK_POINTER(pChildSession, MFX_ERR_NULL_PTR);
    sts = m_pmfxSession->JoinSession(*pChildSession);
    m_bIsJoinSession = (MFX_ERR_NONE == sts);
    return sts;
} // CTranscodingPipeline::Join(MFXVideoSession *pChildSession)

mfxStatus CTranscodingPipeline::Run()
{
    if (m_bDecodeEnable && m_bEncodeEnable)
        return Transcode();
    else if (m_bDecodeEnable)
        return Decode();
    else if (m_bEncodeEnable)
        return Encode();
    else
        return MFX_ERR_UNSUPPORTED;
}

mfxStatus CTranscodingPipeline::CheckExternalBSProcessor(BitstreamProcessor   *pBSProc)
{
    FileBitstreamProcessor *pProc = dynamic_cast<FileBitstreamProcessor*>(pBSProc);
    if (!pProc)
        return MFX_ERR_UNSUPPORTED;

    return MFX_ERR_NONE;
}//  mfxStatus CTranscodingPipeline::CheckExternalBSProcessor()

void IncreaseReference(mfxFrameData *ptr)
{
    msdk_atomic_inc16((volatile mfxU16 *)(&ptr->Locked));
}

void DecreaseReference(mfxFrameData *ptr)
{
    msdk_atomic_dec16((volatile mfxU16 *)&ptr->Locked);
}

SafetySurfaceBuffer::SafetySurfaceBuffer(SafetySurfaceBuffer *pNext):m_pNext(pNext)
{
} // SafetySurfaceBuffer::SafetySurfaceBuffer

SafetySurfaceBuffer::~SafetySurfaceBuffer()
{
} //SafetySurfaceBuffer::~SafetySurfaceBuffer()

void SafetySurfaceBuffer::AddSurface(ExtendedSurface Surf)
{
    AutomaticMutex  guard(m_mutex);

    SurfaceDescriptor sDescriptor;
    // Locked is used to signal when we can free surface
    sDescriptor.Locked     = 1;
    sDescriptor.ExtSurface = Surf;

    if (Surf.pSurface)
    {
        IncreaseReference(&Surf.pSurface->Data);
    }

    m_SList.push_back(sDescriptor);

} // SafetySurfaceBuffer::AddSurface(mfxFrameSurface1 *pSurf)

mfxStatus SafetySurfaceBuffer::GetSurface(ExtendedSurface &Surf)
{
    AutomaticMutex guard(m_mutex);

    // no ready surfaces
    if (0 == m_SList.size())
    {
        Surf.pSurface=NULL;
        Surf.pCtrl=NULL;
        Surf.Syncp=0;
        return MFX_ERR_MORE_SURFACE;
    }

    SurfaceDescriptor sDescriptor = m_SList.front();

    Surf = sDescriptor.ExtSurface;


    return MFX_ERR_NONE;

} // SafetySurfaceBuffer::GetSurface()

mfxStatus SafetySurfaceBuffer::ReleaseSurface(mfxFrameSurface1* pSurf)
{
    AutomaticMutex guard(m_mutex);
    std::list<SurfaceDescriptor>::iterator it;
    for (it = m_SList.begin(); it != m_SList.end(); it++)
    {
        if (pSurf == it->ExtSurface.pSurface)
        {
            it->Locked--;
            if (it->ExtSurface.pSurface)
                DecreaseReference(&it->ExtSurface.pSurface->Data);
            if (0 == it->Locked)
                m_SList.erase(it);
            return MFX_ERR_NONE;
        }
    }
    return MFX_ERR_UNKNOWN;

} // mfxStatus SafetySurfaceBuffer::ReleaseSurface(mfxFrameSurface1* pSurf)

FileBitstreamProcessor::FileBitstreamProcessor()
{
    MSDK_ZERO_MEMORY(m_Bitstream);
    m_Bitstream.TimeStamp=(mfxU64)-1;
} // FileBitstreamProcessor::FileBitstreamProcessor()

FileBitstreamProcessor::~FileBitstreamProcessor()
{
    if (m_pFileReader.get())
        m_pFileReader->Close();
    if (m_pFileWriter.get())
        m_pFileWriter->Close();
    WipeMfxBitstream(&m_Bitstream);
} // FileBitstreamProcessor::~FileBitstreamProcessor()

mfxStatus FileBitstreamProcessor::Init(msdk_char *pStrSrcFile, msdk_char *pStrDstFile)
{
    mfxStatus sts;
    if (pStrSrcFile)
    {
        m_pFileReader.reset(new CSmplBitstreamReader());
        sts = m_pFileReader->Init(pStrSrcFile);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    }

    if (pStrDstFile && *pStrDstFile)
    {
        m_pFileWriter.reset(new CSmplBitstreamWriter);
        sts = m_pFileWriter->Init(pStrDstFile);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    }

    sts = InitMfxBitstream(&m_Bitstream, 1024 * 1024);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    return MFX_ERR_NONE;

} // FileBitstreamProcessor::Init(msdk_char *pStrSrcFile, msdk_char *pStrDstFile)

mfxStatus FileBitstreamProcessor::GetInputBitstream(mfxBitstream **pBitstream)
{
    mfxStatus sts = m_pFileReader->ReadNextFrame(&m_Bitstream);
    if (MFX_ERR_NONE == sts)
    {
        *pBitstream = &m_Bitstream;
        return sts;
    }
    return sts;

} //  FileBitstreamProcessor::GetInputBitstream(mfxBitstream* pBitstream)

mfxStatus FileBitstreamProcessor::ProcessOutputBitstream(mfxBitstream* pBitstream)
{
    if (m_pFileWriter.get())
        return m_pFileWriter->WriteNextFrame(pBitstream, false);
    else
        return MFX_ERR_NONE;

} // mfxStatus FileBitstreamProcessor::ProcessOutputBitstream(mfxBitstream* pBitstream)

mfxStatus FileBitstreamProcessor_WithReset::Init(msdk_char *pStrSrcFile, msdk_char *pStrDstFile)
{
    mfxStatus sts;
    if (pStrSrcFile)
    {
        size_t SrcFileNameSize = msdk_strlen(pStrSrcFile);
        m_pSrcFile.assign(pStrSrcFile, pStrSrcFile + SrcFileNameSize + 1);
        m_pFileReader.reset(new CSmplBitstreamReader());
        sts = m_pFileReader->Init(pStrSrcFile);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    } else
    {
        m_pSrcFile.resize(1, 0);
    }

    if (pStrDstFile)
    {
        size_t DstFileNameSize = msdk_strlen(pStrDstFile);
        m_pDstFile.assign(pStrDstFile, pStrDstFile + DstFileNameSize + 1);
        m_pFileWriter.reset(new CSmplBitstreamWriter);
        sts = m_pFileWriter->Init(pStrDstFile);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    }
    else
    {
        m_pDstFile.resize(1, 0);
    }

    sts = InitMfxBitstream(&m_Bitstream, 1024 * 1024);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    return MFX_ERR_NONE;

} // FileBitstreamProcessor_Benchmark::Init(msdk_char *pStrSrcFile, msdk_char *pStrDstFile)

mfxStatus FileBitstreamProcessor_WithReset::ResetInput()
{
    mfxStatus sts = m_pFileReader->Init(&m_pSrcFile.front());
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    return MFX_ERR_NONE;
} // FileBitstreamProcessor_Benchmark::ResetInput()

mfxStatus FileBitstreamProcessor_WithReset::ResetOutput()
{
    mfxStatus sts = m_pFileWriter->Init(&m_pDstFile.front());
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    return MFX_ERR_NONE;
} // FileBitstreamProcessor_Benchmark::ResetOutput()
