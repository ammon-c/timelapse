//--------------------------------------------------------------------
// CameraFrameGrabber.cpp
// A C++ module for capturing images from a camera using the
// Microsoft Media Foundation APIs on Windows.
//
// (C) Copyright 2019,2022 Ammon R. Campbell.
//
// I wrote this code for use in my own educational and experimental
// programs, but you may also freely use it in yours as long as you
// abide by the following terms and conditions:
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//   * Redistributions of source code must retain the above copyright
//     notice, this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above
//     copyright notice, this list of conditions and the following
//     disclaimer in the documentation and/or other materials
//     provided with the distribution.
//   * The name(s) of the author(s) and contributors (if any) may not
//     be used to endorse or promote products derived from this
//     software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
// CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
// INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
// OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
// BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
// USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
// DAMAGE.  IN OTHER WORDS, USE AT YOUR OWN RISK, NOT OURS.  
//--------------------------------------------------------------------

#include "CameraFrameGrabber.h"

#include <stdio.h>
#include <tchar.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mfobjects.h>
#include <atlbase.h>
#include <atlcom.h>
#include <conio.h>
#include <stdexcept>

// Link to Microsoft's Media Foundation libraries.
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace
{

//---------------------------------------------------------------
// Retrieves the width, height, and stride of the given media
// type.  Returns true if successful.
//---------------------------------------------------------------
bool GetImageFormatFromMediaType(
    IMFMediaType *type,         // in: Pointer to media type to be examined.
    GUID     &vidFormatGuid,    // out: GUID for this video format.
    unsigned &width,            // out: Width in pixels.
    unsigned &height,           // out: Height in pixels.
    unsigned &stride,           // out: Scanline size in bytes (note this is only counts one color plane for multi-plane images).
    unsigned &frameSize         // out: Size of each sample frame buffer in bytes.
    )
{
    // Null GUID will get default format for this media type.
    GUID subtype = GUID_NULL;
    if (type->GetGUID(MF_MT_SUBTYPE, &subtype) != S_OK)
        return false;

    // Get the width and height.
    uint32_t width32 = 0;
    uint32_t height32 = 0;
    if (MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width32, &height32) != S_OK)
        return false;

    // Get the stride info.
    LONG lstride = 0;
    if (MFGetStrideForBitmapInfoHeader(subtype.Data1, width32, &lstride) != S_OK)
        return false;

    // Compressed formats are not supported.
    uint32_t isCompressed = 0;
    type->GetUINT32(MF_MT_COMPRESSED, &isCompressed);
    if (isCompressed == 1)
        return false;

    // Formats with variable frame sizes are not supported.
    uint32_t samplesAreFixedSize = 1;
    type->GetUINT32(MF_MT_FIXED_SIZE_SAMPLES, &samplesAreFixedSize);
    if (samplesAreFixedSize == 0)
        return false;

    // Get the size of each frame in bytes (MS Media Foundation uses
    // the term 'sample' to refer to a 'frame'.  Note that this may
    // be zero for some formats (even though it's not actually zero).
    uint32_t sampleSize32 = 0;
    type->GetUINT32(MF_MT_SAMPLE_SIZE, &sampleSize32);

    // Put the results in the caller's variables.
    width  = static_cast<unsigned>(width32);
    height = static_cast<unsigned>(height32);
    stride = static_cast<unsigned>(lstride);
    frameSize = static_cast<unsigned>(sampleSize32);
    vidFormatGuid = subtype;

    return true; 
}

//---------------------------------------------------------------
// Converts a YUV color to an RGB color (actually a BGR color).
// The returned value is in xxxxxxxxRRRRRRRRGGGGGGGGBBBBBBBB
// format.  This was adapted from some public domain code.
//---------------------------------------------------------------
uint32_t ConvertYuvToRgbColor(int y, int u, int v)
{
    int r = 0, g = 0, b = 0;

    // U and V are actually -127 to +127 rather than 0 to 255.
    u -= 128;
    v -= 128;

    // Conversion formulas:
    //   r = y + 1.370705 * v;
    //   g = y - 0.698001 * v - 0.337633 * u;
    //   b = y + 1.732446 * u;
    r = static_cast<int>(y + 1.402 * v);
    g = static_cast<int>(y - 0.34414 * u - 0.71414 * v);
    b = static_cast<int>(y + 1.772 * u);
 
    // Limit results to the 0-to-1 range.
    if (r < 0)
        r = 0;
    if (g < 0)
        g = 0;
    if (b < 0)
        b = 0;
    if (r > 255)
        r = 255;
    if (g > 255)
        g = 255;
    if (b > 255)
        b = 255;

    uint32_t out = (r << 16) + (g << 8) + (b);
    return out;
}

//---------------------------------------------------------------
// Converts pixels from NV12 format to BGR32 format.
//---------------------------------------------------------------
void ConvertNv12ToBgr32(
    const void *inData,     // in:  Points to the Y plane, which is followed immediately by the UV plane.
    unsigned inWidth,       // in:  Width of image in pixels.
    unsigned inHeight,      // in:  Height of image in pixels.
    unsigned inStride,      // in:  Width of each scanline in bytes (usually same as inWidth but not always).
    void *outData           // out:  Buffer where converted image will be placed.
                            //       Must be at least inStride * inHeight * 4 bytes in size.
    )
{
    unsigned char *outDataBGR = reinterpret_cast<unsigned char *>(outData);

    // inY points to the first pixel of the 'Y' plane.
    // inUV points to the first pixel of the 'UV' plane.
    const unsigned char *inDataY = reinterpret_cast<const unsigned char *>(inData);
    const unsigned char *inDataUV = inDataY + (inStride * inHeight);

    // For each scanline..
    for (unsigned y = 0; y < inHeight; ++y)
    {
        uint32_t *outScan = reinterpret_cast<uint32_t *>(outDataBGR + inWidth * 4 * y);
        const unsigned char *inScanY = inDataY + inStride * y;
        const unsigned char *inScanUV = inDataUV + inStride * y / 2;

        // For each pixel on this scanline...
        for (unsigned x = 0; x < inWidth; ++x)
        {
            int y = *inScanY++;
            int u = inScanUV[0];
            int v = inScanUV[1];
            if (x & 1)
                inScanUV += 2;

            *outScan++ = ConvertYuvToRgbColor(y, u, v);
        }
    }
}

//---------------------------------------------------------------
// Returns 'v' clipped to the range 0-to-255 inclusive.
//---------------------------------------------------------------
int inline clip8(int v) { return __max(0, __min(255, v)); }

//---------------------------------------------------------------
// Converts pixels from YUY2 format to BGR32 format.
// This was adapted from some public domain code.
//---------------------------------------------------------------
void ConvertYuy2ToBgr32(
    const void *inData,     // in:  Points to the Y plane, which is followed immediately by the UV plane.
    unsigned inWidth,       // in:  Width of image in pixels.
    unsigned inHeight,      // in:  Height of image in pixels.
    unsigned inStride,      // in:  Width of each scanline in bytes (usually same as (inWidth x 2) but not always).
    void *outData           // out:  Buffer where converted image will be placed.
                            //       Must be at least inStride * inHeight * 4 bytes in size.
    )
{
    unsigned char *outDataBGR = reinterpret_cast<unsigned char *>(outData);
    const unsigned char *inDataYUY = reinterpret_cast<const unsigned char *>(inData);

    // For each scanline..
    for (unsigned y = 0; y < inHeight; ++y)
    {
        unsigned char *outScan = outDataBGR + inWidth * 4 * y;
        const unsigned char *inScan = inDataYUY + inStride * y;

        // For each pixel on this scanline...
        for (unsigned x = 0; x < inWidth / 2; ++x)
        {
            int y0 = *inScan++;
            int u0 = *inScan++;
            int y1 = *inScan++;
            int v0 = *inScan++;

            int c = y0 - 16;
            int d = u0 - 128;
            int e = v0 - 128;

            *outScan++ = clip8(( 298 * c + 516 * d + 128) >> 8);
            *outScan++ = clip8(( 298 * c - 100 * d - 208 * e + 128) >> 8);
            *outScan++ = clip8(( 298 * c + 409 * e + 128) >> 8);
            *outScan++ = '\0';

            c = y1 - 16;

            *outScan++ = clip8(( 298 * c + 516 * d + 128) >> 8);
            *outScan++ = clip8(( 298 * c - 100 * d - 208 * e + 128) >> 8);
            *outScan++ = clip8(( 298 * c + 409 * e + 128) >> 8);
            *outScan++ = '\0';
        }
    }
}

} // End anon namespace

//---------------------------------------------------------------
CameraFrameGrabber::CameraFrameGrabber()
{
    if (MFStartup(MF_VERSION) != S_OK)
    {
        throw std::runtime_error("Media Foundation startup failed!  Aborting.");
    }
}

//---------------------------------------------------------------
CameraFrameGrabber::~CameraFrameGrabber()
{
    Close();
    MFShutdown();
}

//---------------------------------------------------------------
// Retrieves a list of the names of the available camera capture
// devices.  Returns an empty list if there are no capture
// devices installed on the system.
//---------------------------------------------------------------
std::vector<std::wstring> CameraFrameGrabber::GetDeviceNames()
{
    std::vector<std::wstring> out;

    // Create an empty Media Foundation attributes object.
    CComPtr<IMFAttributes> pAttributes = nullptr;
    if (MFCreateAttributes(&pAttributes, 1) != S_OK)
        return out;

    // Set the attribute's GUID to the clas of devices that
    // support video capture.
    if (pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                             MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID) != S_OK)
        return out;

    // Enumerate the devices that support video capture.
    CComHeapPtr<IMFActivate*> ppActivates;
    UINT32 numActiveDevs = 0;
    if (MFEnumDeviceSources(pAttributes, &ppActivates, &numActiveDevs) != S_OK)
        return out;

    // Get the name of each device.
    for (UINT32 i = 0; i < numActiveDevs; ++i)
    {
        CComHeapPtr<WCHAR> pDevName;
        if (ppActivates[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &pDevName, nullptr) != S_OK)
            return out;

        out.push_back(std::wstring(pDevName));
    }

    // Release the devices.
    for (UINT32 i = 0; i < numActiveDevs; ++i)
        reinterpret_cast<CComPtr<IMFActivate>&>(ppActivates[i]).Release();

    return out;
}

//---------------------------------------------------------------
// Retrieves a list of the supported capture formats for
// the specified capture device.  Returns an empty list if
// error.
//---------------------------------------------------------------
std::vector<CaptureFormat> CameraFrameGrabber::GetDeviceFormats(unsigned deviceIndex)
{
    std::vector<CaptureFormat> out;

    // Create an empty Media Foundation attributes object.
    CComPtr<IMFAttributes> pAttributes = nullptr;
    if (MFCreateAttributes(&pAttributes, 1) != S_OK)
        return out;

    // Set the attribute's GUID to the class of devices that
    // support video capture.
    if (pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                              MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID) != S_OK)
        return out;

    // Enumerate the devices that support video capture.
    CComHeapPtr<IMFActivate*> ppActivates;
    UINT32 numActiveDevs = 0;
    if (MFEnumDeviceSources(pAttributes, &ppActivates, &numActiveDevs) != S_OK)
        return out;

    if (deviceIndex >= numActiveDevs)
        return out;

    // Get a pointer to the device the caller requested.
    const CComPtr<IMFActivate> pActivate = ppActivates[deviceIndex];

    // Release all of the devices.
    for (UINT32 i = 0; i < numActiveDevs; ++i)
        reinterpret_cast<CComPtr<IMFActivate>&>(ppActivates[i]).Release();

    // Activate the device the caller requested.
    CComPtr<IMFMediaSource> pMediaSource = nullptr;
    if (pActivate->ActivateObject(__uuidof(IMFMediaSource), (VOID**) &pMediaSource) != S_OK)
        return out;

    if (MFCreateSourceReaderFromMediaSource(pMediaSource, NULL, &reinterpret_cast<IMFSourceReader *>(m_pReader)) != S_OK)
        return out;

    // Step through the device's media types to get the size/format
    // of each one.
    unsigned width = 0, height = 0, stride = 0, frameSize = 0;
    DWORD formatIndex = 0;
    while (1)
    {
        IMFMediaType *mediaType = nullptr;
        if (reinterpret_cast<IMFSourceReader *>(m_pReader)->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, formatIndex, &mediaType) != S_OK)
            break;

        GUID vidFormatGuid;
        if (!GetImageFormatFromMediaType(mediaType, vidFormatGuid, width, height, stride, frameSize))
            break;

        CaptureFormat fmt;

        // Set the pixel type indicator.
        if (vidFormatGuid == MFVideoFormat_RGB32)   fmt.m_pixelType = CPT_RGB32;
        if (vidFormatGuid == MFVideoFormat_RGB24)   fmt.m_pixelType = CPT_RGB24;
        if (vidFormatGuid == MFVideoFormat_YUY2)    fmt.m_pixelType = CPT_YUY2;
        if (vidFormatGuid == MFVideoFormat_NV12)    fmt.m_pixelType = CPT_NV12;
        if (fmt.m_pixelType == CPT_INVALID)
        {
            // Skip unsupported video image formats.
            ++formatIndex;
            continue;
        }

        // Add to the list of formats.
        fmt.m_index = formatIndex;
        fmt.m_width = width;
        fmt.m_height = height;
        fmt.m_stride = stride;
        fmt.m_frameSize = frameSize;
        fmt.m_vidFormatGuid = vidFormatGuid;
        out.push_back(fmt);

        if (mediaType)
            mediaType->Release();

        ++formatIndex;
    }

    // Release all of the devices.
    for (UINT32 i = 0; i < numActiveDevs; ++i)
        reinterpret_cast<CComPtr<IMFActivate>&>(ppActivates[i]).Release();

    return out;
}

//---------------------------------------------------------------
// Opens a capture session to the specified device.
// Returns true if successful.
//---------------------------------------------------------------
bool CameraFrameGrabber::Open(unsigned deviceIndex, unsigned formatIndex)
{
    // Create an empty Media Foundation attributes object.
    CComPtr<IMFAttributes> pAttributes = nullptr;
    if (MFCreateAttributes(&pAttributes, 1) != S_OK)
        return false;

    // Set the attribute's GUID to the clas of devices that
    // support video capture.
    if (pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                              MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID) != S_OK)
        return false;

    // Enumerate the devices that support video capture.
    CComHeapPtr<IMFActivate*> ppActivates;
    UINT32 numActiveDevs = 0;
    if (MFEnumDeviceSources(pAttributes, &ppActivates, &numActiveDevs) != S_OK)
        return false;

    if (deviceIndex >= numActiveDevs)
        return false;

    // Get a pointer to the device the caller requested.
    const CComPtr<IMFActivate> pActivate = ppActivates[deviceIndex];

    // Release all of the devices.
    for (UINT32 i = 0; i < numActiveDevs; ++i)
        reinterpret_cast<CComPtr<IMFActivate>&>(ppActivates[i]).Release();

    // Activate the device the caller requested.
    CComPtr<IMFMediaSource> pMediaSource = nullptr;
    if (pActivate->ActivateObject(__uuidof(IMFMediaSource), (VOID**) &pMediaSource) != S_OK)
        return false;

    if (MFCreateSourceReaderFromMediaSource(pMediaSource, NULL, &reinterpret_cast<IMFSourceReader *>(m_pReader)) != S_OK)
        return false;

    // Get the media type for the format requested by the caller.
    DWORD fIndex = formatIndex;
    unsigned width = 0, height = 0, stride = 0, frameSize = 0;

    IMFMediaType *mediaType = nullptr;
    if (reinterpret_cast<IMFSourceReader *>(m_pReader)->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, fIndex, &mediaType) != S_OK)
        return false;

    // Set the video processing flag, which will enable YUV to RGB conversion.
    if (mediaType->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, 1) != S_OK)
        return false;

    GUID vidFormatGuid = {0};
    if (!GetImageFormatFromMediaType(mediaType, vidFormatGuid, width, height, stride, frameSize))
        return false;

    CaptureFormat fmt;
    if (vidFormatGuid == MFVideoFormat_RGB32)   fmt.m_pixelType = CPT_RGB32;
    if (vidFormatGuid == MFVideoFormat_RGB24)   fmt.m_pixelType = CPT_RGB24;
    if (vidFormatGuid == MFVideoFormat_YUY2)    fmt.m_pixelType = CPT_YUY2;
    if (vidFormatGuid == MFVideoFormat_NV12)    fmt.m_pixelType = CPT_NV12;
    if (fmt.m_pixelType == CPT_INVALID)
    {
        // Unsupported pixel format!
        return false;
    }

    fmt.m_index = formatIndex;
    fmt.m_width = width;
    fmt.m_height = height;
    fmt.m_stride = stride;
    fmt.m_frameSize = frameSize;
    fmt.m_vidFormatGuid = vidFormatGuid;
    m_captureFormat = fmt;

    if (reinterpret_cast<IMFSourceReader *>(m_pReader)->SetCurrentMediaType(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, mediaType) != S_OK)
        return false;

    if (mediaType)
        mediaType->Release();

    return true;
}

//---------------------------------------------------------------
void CameraFrameGrabber::Close()
{
    m_pReader = nullptr;
}

//---------------------------------------------------------------
// Captures an image frame from the currently open device.
// The pixels are converted from the internal format to
// 32-bit BGRA format and placed into the buffer given by
// the caller.  Returns true if successful.  Note the very
// first frame may be all black, as some devices take some
// time to fully initialize. 
//---------------------------------------------------------------
bool CameraFrameGrabber::GrabFrame(void *data, size_t dataSize, std::string &errText)
{
    errText.clear();

    if (data == nullptr || dataSize < 1)
    {
        errText = "Bad parameter.";
        return false;
    }

    if (m_pReader == nullptr)
    {
        errText = "Uninitialized.";
        return false;
    }

    // Read the frame buffer from the capture device.
    DWORD streamIndex = 0;
    DWORD flags = 0;
    LONGLONG streamTime = 0;
    CComPtr<IMFSample> pSample = nullptr;
    if (reinterpret_cast<IMFSourceReader *>(m_pReader)->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                            &streamIndex, &flags, &streamTime,
                                            &pSample) != S_OK)
    {
        errText = "ReadSample failed.";
        return false;
    }

    if ((streamIndex == 0) && (flags & MF_SOURCE_READERF_STREAMTICK))
    {
        // The camera dropped a frame or was unable to capture, so
        // fill the caller's frame buffer with black pixels.
        memset(data, 0, dataSize);
        return true;
    }

    if (pSample == nullptr)
    {
        errText = "No sample data.";
        return false;
    }

    // Extract the frame data from the sample object.
    // First we have to convert it to contiguous format.
    CComPtr<IMFMediaBuffer> mbuffer;
    if (pSample->ConvertToContiguousBuffer(&mbuffer) != S_OK)
    {
        errText = "ConvertToContiguousBuffer failed.";
        return false;
    }

    // Next we have to lock the contiguous buffer.
    unsigned char *mbufferData = nullptr;
    DWORD mbufferDataMax = 0, mbufferLen = 0;
    if (mbuffer->Lock(&mbufferData, &mbufferDataMax, &mbufferLen) != S_OK)
    {
        errText = "Failed locking IMFMediaBuffer.";
        return false;
    }

    // Convert the raw frame data to a usable format.
    // The converted frame data is placed into the caller's 'data' buffer.
    bool result = true;
    if (m_captureFormat.m_pixelType == CPT_RGB24)
    {
        // Convert 24-bit to 32-bit.
        for (unsigned y = 0; y < m_captureFormat.m_height; ++y)
        {
            const unsigned char *pin = reinterpret_cast<const unsigned char *>(mbufferData) + y * m_captureFormat.m_stride;
            unsigned char *pout = reinterpret_cast<unsigned char *>(data) + m_captureFormat.m_width * 4;
            for (unsigned x = 0; x < m_captureFormat.m_width; ++x)
            {
                *pout++ = *pin++;
                *pout++ = *pin++;
                *pout++ = *pin++;
                *pout++ = '\0';
            }
        }
    }
    else if (m_captureFormat.m_pixelType == CPT_RGB32)
    {
        // Copy 32-bit frame as-is.
        memcpy(data, mbufferData, m_captureFormat.m_stride * m_captureFormat.m_height);
    }
    else if (m_captureFormat.m_pixelType == CPT_YUY2)
    {
        // Convert YUY2 frame to 32-bit.
        ConvertYuy2ToBgr32(mbufferData, m_captureFormat.m_width,
                                        m_captureFormat.m_height,
                                        m_captureFormat.m_stride,
                                        data);
    }
    else if (m_captureFormat.m_pixelType == CPT_NV12)
    {
        // Convert NV12 frame to 32-bit.
        ConvertNv12ToBgr32(mbufferData, m_captureFormat.m_width,
                                        m_captureFormat.m_height,
                                        m_captureFormat.m_stride,
                                        data);
    }
    else
    {
        // Unsupported pixel format!
        errText = "Unsupported pixel format.";
        result = false;
    }

    mbuffer->Unlock();

    return result;
}

