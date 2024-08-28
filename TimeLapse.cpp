//--------------------------------------------------------------------
// TimeLapse.cpp
// Program to capture a time lapse picture series from a camera
// device connected to the computer.  Also useful as a test of
// the CameraFrameGrabber module.
//
// (C) Copyright 2022 Ammon R. Campbell.
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
#include <stdlib.h>
#include <stdio.h>
#include <conio.h>
#include <windows.h>

struct Settings
{
    unsigned m_deviceIndex = 0;       // Which capture device to grab frames from.
    unsigned m_formatIndex = 0;       // Which of the capture device's available formats to use.
    unsigned m_numFramesToGrab = 10;
    unsigned m_secondsBetweenFrames = 1;
};

//---------------------------------------------------------------
// Gets the list of available capture devices and prints it to
// stdout in human-readable form.
//---------------------------------------------------------------
static void ShowCaptureDevices()
{
    printf("Checking capture devices.\n");
    CameraFrameGrabber cam;
    std::vector<std::wstring> devNames = cam.GetDeviceNames();
    size_t index = 0;
    printf("Capture device(s) found:\n");
    for (const auto &name : devNames)
    {
        printf("  %3zu:  %S\n", index + 1, name.c_str());
        ++index;
    }

    if (devNames.empty())
    {
        printf("  No capture devices found!\n");
    }
}

//---------------------------------------------------------------
// Shows the available capture formats for the specified device.
// Note that the device index is 1-based, not 0-based.
//---------------------------------------------------------------
static void ShowCaptureFormatsForDevice(unsigned deviceIndex)
{
    if (deviceIndex < 1)
        return;
    printf("Checking capture formats for device %u.\n", deviceIndex);

    --deviceIndex;  // Change device index to zero-based.

    CameraFrameGrabber cam;
    auto formats = cam.GetDeviceFormats(deviceIndex);
    if (formats.empty())
    {
        printf("Device has no capture formats.\n");
        return;
    }

    for (const auto &fmt : formats)
    {
        if (fmt.m_index != 0)
            printf("  %3d:  width=%u  height=%u  stride=%u  frameSize=%u\n",
                fmt.m_index, fmt.m_width, fmt.m_height, fmt.m_stride, fmt.m_frameSize);
    }
}

//---------------------------------------------------------------
// Writes a 24-bit BGR or 32-bit BGRA image from memory to a
// Microsoft .BMP file on disk.  Returns true if successful.
//---------------------------------------------------------------
static bool BmpWrite(const char *szPath, unsigned width, unsigned height,
    unsigned stride, unsigned bitsPerPixel, const void *pBits)
{
    // Check for bogus arguments.
    if (szPath == nullptr || szPath[0] == '\0' || width < 1 || height < 1 ||
        stride < width * 3 || (bitsPerPixel != 24 && bitsPerPixel != 32) ||
        pBits == nullptr)
    {
        return false;
    }

    // Open the output file.
    FILE *fp = nullptr;
    if (fopen_s(&fp, szPath, "wb") || fp == nullptr)
    {
        // Failed opening output file!
        return false;
    }

    unsigned outStride = width * bitsPerPixel / 8;
    while (outStride % 4)
        outStride++;

    // Build BITMAPINFOHEADER to write to file.
    BITMAPINFOHEADER stInfoHdr = {0};
    stInfoHdr.biSize = sizeof(stInfoHdr);
    stInfoHdr.biBitCount = bitsPerPixel;
    stInfoHdr.biWidth = width;
    stInfoHdr.biHeight = height;
    stInfoHdr.biPlanes = 1;
    stInfoHdr.biSizeImage = outStride * height;

    // Build BITMAPFILEHEADER structure.
    BITMAPFILEHEADER stFileHdr;
    memset(&stFileHdr, 0, sizeof(stFileHdr));
    stFileHdr.bfType = (WORD)'B' + 256 * (WORD)'M';
    stFileHdr.bfSize = sizeof(BITMAPFILEHEADER) + stInfoHdr.biSize + stInfoHdr.biSizeImage;
    stFileHdr.bfOffBits = sizeof(BITMAPFILEHEADER) + stInfoHdr.biSize;

    // Write the BITMAPFILEHEADER to the output file.
    if (fwrite(&stFileHdr, sizeof(stFileHdr), 1, fp) != 1)
    {
        // Write to output file failed!
        fclose(fp);
        _unlink(szPath);
        return false;
    }

    // Write the BITMAPINFOHEADER to the output file.
    if (fwrite(&stInfoHdr, stInfoHdr.biSize, 1, fp) != 1)
    {
        // Write to output file failed!
        fclose(fp);
        _unlink(szPath);
        return false;
    }

    // Write the bitmap bits one scanline at a time.
    const unsigned char *scanline = static_cast<const unsigned char *>(pBits) + stride * (height - 1);
    for (unsigned y = 0; y < height; y++)
    {
        if (fwrite(scanline, outStride, 1, fp) != 1)
        {
            // Write to output file failed!
            fclose(fp);
            _unlink(szPath);
            return false;
        }

        scanline -= stride;
    }

    fclose(fp);
    return true;
}

//---------------------------------------------------------------
// Captures a series of images from the specified capture device.
// The captured images are written to Microsoft .BMP files, with
// the filename template "frameXXXX.bmp", in the current working
// directory.
//
// The deviceIndex and formatIndex parameters are 0-based, not
// 1-based.
//
// Returns true if successful.
//---------------------------------------------------------------
static bool DoTimeLapseCapture(Settings &settings)
{
    printf("Opening capture device %u in capture format %u.\n",
        settings.m_deviceIndex + 1, settings.m_formatIndex);
    CameraFrameGrabber cam;
    if (!cam.Open(settings.m_deviceIndex, settings.m_formatIndex))
    {
        printf("Failed opening capture device!\n");
        return false;
    }

    printf("Capture device opened.\n");
    fflush(stdout);

    std::vector<unsigned char> frame(cam.GetStride() * cam.GetHeight());

    for (unsigned iframe = 0; iframe < settings.m_numFramesToGrab; iframe++)
    {
        if (_kbhit() && _getch() == 27)
        {
            printf("ESC pressed.  Aborted by user.\n");
            break;
        }

        std::string errText;
        if (!cam.GrabFrame(frame.data(), frame.size(), errText))
        {
            printf("Failed capturing frame!\n");
            printf("  Error Text:  %s\n", errText.c_str());
            continue;
        }

        // Write the captured frame to a .BMP file.
        try
        {
            char filename[MAX_PATH] = {0};
            sprintf_s(filename, _countof(filename), "frame%04u.bmp", iframe);
            printf("Writing frame to \"%s\"\n", filename);

            BmpWrite(filename, cam.GetWidth(), cam.GetHeight(),
                cam.GetStride(), 32, frame.data());
        }
        catch(...)
        {
            printf("Failed writing captured image to BMP file!\n");
            return false;
        }

        Sleep(settings.m_secondsBetweenFrames * 1000);
    }

    printf("Closing capture device %u.\n", settings.m_deviceIndex + 1);
    cam.Close();

    printf("Capture session done.\n");
    return true;
}

//---------------------------------------------------------------
// Parses the program's command line arguments, placing the
// parameter values into 'settings'.  Returns false if error.
//---------------------------------------------------------------
static bool ParseArguments(int argc, char **argv, Settings &settings)
{
    const char *str_device = "device=";
    const char *str_format = "format=";
    const char *str_delay  = "delay=";
    const char *str_frames = "frames=";

    for (int iarg = 1; iarg < argc; iarg++)
    {
        const char *arg = argv[iarg];
        if (arg[0] == '/' || arg[0] == '-')
            arg++;

        if (_strnicmp(arg, str_device, strlen(str_device)) == 0)
        {
            settings.m_deviceIndex = atoi(&arg[strlen(str_device)]);
            if (settings.m_deviceIndex < 1)
            {
                printf("\"%s\" is not a valid capture device index.\n", arg);
                return false;
            }
        }
        else if (_strnicmp(arg, str_format, strlen(str_format)) == 0)
        {
            settings.m_formatIndex = atoi(&arg[strlen(str_format)]);
            if (settings.m_formatIndex < 1)
            {
                printf("\"%s\" is not a valid format index.\n", arg);
                return false;
            }
        }
        else if (_strnicmp(arg, str_delay, strlen(str_delay)) == 0)
        {
            settings.m_secondsBetweenFrames = atoi(&arg[strlen(str_delay)]);
            if (settings.m_secondsBetweenFrames < 1)
            {
                printf("\"%s\" is not a valid number of seconds.\n", arg);
                return false;
            }
        }
        else if (_strnicmp(arg, str_frames, strlen(str_frames)) == 0)
        {
            settings.m_numFramesToGrab = atoi(&arg[strlen(str_frames)]);
            if (settings.m_numFramesToGrab < 1)
            {
                printf("\"%s\" is not a valid number of frames.\n", arg);
                return false;
            }
        }
        else
        {
            printf("Unrecognized argument:  \"%s\"\n", arg);
            return false;
        }
    }

    return true;
}

//---------------------------------------------------------------
// Prints command line help summary to stdout.
//---------------------------------------------------------------
static void PrintUsage()
{
    printf("Usage:  TimeLapse device=x format=x [frames=x] [delay=x]\n");
    printf("\n");
    printf("Options:\n");
    printf("  device=x  Specify the index of the camera capture device.\n");
    printf("  format=x  Specify which of the device's frame formats to\n");
    printf("            capture with.\n");
    printf("  frames=x  Specify the number of frames to capture.\n");
    printf("  delay=x   Specify the number of seconds to delay between\n");
    printf("            frames.\n");
}

//---------------------------------------------------------------
int main(int argc, char **argv)
{
    if (argc < 2)
    {
        PrintUsage();
        return EXIT_FAILURE;
    }

    Settings settings;
    if (!ParseArguments(argc, argv, settings))
        return EXIT_FAILURE;

    if (settings.m_deviceIndex < 1)
    {
        printf("No camera device index specified!\n");
        ShowCaptureDevices();
        return EXIT_FAILURE;
    }

    if (settings.m_formatIndex < 1)
    {
        printf("No capture format index specified!\n");
        ShowCaptureFormatsForDevice(settings.m_deviceIndex);
        return EXIT_FAILURE;
    }

    printf("Settings:\n");
    printf("  Camera capture device:    %u\n", settings.m_deviceIndex);
    printf("  Capture format:           %u\n", settings.m_formatIndex);
    printf("  Number of frames to grab: %u\n", settings.m_numFramesToGrab);
    printf("  Seconds between frames:   %u\n", settings.m_secondsBetweenFrames);

    // Command-line uses 1-based device index, but internally
    // we use a 0-based index.
    --settings.m_deviceIndex;

    bool ok = DoTimeLapseCapture(settings);

    printf("TimeLapse done.\n");
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

