//--------------------------------------------------------------------
// CameraFrameGrabber.h
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
// NOTES:
//
// * General Usage:  Declare an object of type CameraFrameGrabber,
//   call the Open() member with the desired device and format,
//   call the GrabFrame() member as many times as desired to
//   capture frames, then call the Close() member when done.
//
// * This module supports capture devices that produce images
//   in following pixel encoding formats:  BGR-24, BGR-32,
//   YUY-2, and NV-12.
//
// * The output format produced by this module is always BGRA-32
//   regardless of the device's capture format.
//--------------------------------------------------------------------

#pragma once

#include <stdio.h>
#include <vector>
#include <string>
#include <guiddef.h>

//---------------------------------------------------------------
// Possible values for the pixel type member of CaptureFormat.
//---------------------------------------------------------------
enum CapturePixelType
{
    CPT_INVALID = 0,
    CPT_RGB24   = 1,
    CPT_RGB32   = 2,
    CPT_YUY2    = 3,
    CPT_NV12    = 4
};

//---------------------------------------------------------------
// Structure that describes a video image format.
//---------------------------------------------------------------
struct CaptureFormat
{
    CaptureFormat() = default;

    // Index of this capture format.
    // This is used to select this format in a device with multiple formats.
    unsigned m_index = 0;

    // Size of the captured image in pixels.
    unsigned m_width = 0;
    unsigned m_height = 0;

    // Width of each scanline in bytes.
    // Note that only counts one color plane in multi-plane images!
    unsigned m_stride = 0;

    // Size of each sample frame buffer in bytes.
    // May be zero, in which case we have to calculate the size.
    unsigned m_frameSize = 0;

    // Indicates the kind of pixel encoding.
    CapturePixelType m_pixelType = CPT_INVALID;

    // The GUID of this video format.
    GUID m_vidFormatGuid = {0};
};

//---------------------------------------------------------------
// A C++ class for capturing still images from a camera (or other
// capture device).  Uses the Microsoft Media Foundation APIs.
//---------------------------------------------------------------
class CameraFrameGrabber
{
public:
    CameraFrameGrabber();
    ~CameraFrameGrabber();

    // Retrieves a list of the names of the available camera
    // capture devices.  Returns an empty list if there are no
    // capture devices installed on the system.
    std::vector<std::wstring> GetDeviceNames();

    // Retrieves a list of the supported capture formats for
    // the specified capture device.  Returns an empty list if
    // the device index is out of range or fails to return any
    // format information.
    std::vector<CaptureFormat> GetDeviceFormats(unsigned deviceIndex);

    // Opens a capture session with the specified device.
    // Returns true if successful.
    bool Open(unsigned deviceIndex, unsigned formatIndex);

    // Closes the capture session.
    void Close();

    // Return information about the format of the images
    // retrieved by GrabFrame().
    unsigned GetWidth() const { return m_captureFormat.m_width; }
    unsigned GetHeight() const { return m_captureFormat.m_height; }
    unsigned GetStride() const { return GetWidth() * 4; }
    unsigned GetBitsPerPixel() const { return 32; }

    // Captures an image frame from the currently open device.
    // The pixels are converted from the internal format to
    // 32-bit BGRA format and placed into the buffer given by
    // the caller.  Returns true if successful.  Note the very
    // first frame may be all black, as some devices take some
    // time to fully initialize. 
    bool GrabFrame(void *data, size_t dataSize, std::string &errText);

private:
    void *m_pReader = nullptr;      // opaque pointer to internally used IMFSourceReader object.
    unsigned m_deviceIndex = 0;     // Index of currently open capture device.
    CaptureFormat m_captureFormat;  // Current capture image format.
};

