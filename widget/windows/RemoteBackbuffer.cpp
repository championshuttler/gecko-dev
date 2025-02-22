/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteBackbuffer.h"

namespace mozilla {
namespace widget {
namespace remote_backbuffer {

enum class ResponseResult {
  Unknown,
  Error,
  BorrowSuccess,
  BorrowSameBuffer,
  PresentSuccess
};

enum class SharedDataType {
  BorrowRequest,
  BorrowResponse,
  PresentRequest,
  PresentResponse
};

struct BorrowResponseData {
  ResponseResult result;
  int32_t width;
  int32_t height;
  HANDLE fileMapping;
};

struct PresentResponseData {
  ResponseResult result;
};

struct SharedData {
  SharedDataType dataType;
  union {
    BorrowResponseData borrowResponse;
    PresentResponseData presentResponse;
  } data;
};

class SharedImage {
 public:
  SharedImage()
      : mWidth(0), mHeight(0), mFileMapping(nullptr), mPixelData(nullptr) {}

  ~SharedImage() {
    if (mPixelData) {
      MOZ_ALWAYS_TRUE(::UnmapViewOfFile(mPixelData));
    }

    if (mFileMapping) {
      MOZ_ALWAYS_TRUE(::CloseHandle(mFileMapping));
    }
  }

  bool Initialize(int32_t aWidth, int32_t aHeight) {
    MOZ_ASSERT(aWidth);
    MOZ_ASSERT(aHeight);

    mWidth = aWidth;
    mHeight = aHeight;

    DWORD bufferSize = static_cast<DWORD>(mHeight * GetStride());

    mFileMapping = ::CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr /*secattr*/, PAGE_READWRITE,
        0 /*sizeHigh*/, bufferSize, nullptr /*name*/);
    if (!mFileMapping) {
      return false;
    }

    void* mappedFilePtr =
        ::MapViewOfFile(mFileMapping, FILE_MAP_ALL_ACCESS, 0 /*offsetHigh*/,
                        0 /*offsetLow*/, 0 /*bytesToMap*/);
    if (!mappedFilePtr) {
      return false;
    }

    mPixelData = reinterpret_cast<unsigned char*>(mappedFilePtr);

    return true;
  }

  bool InitializeRemote(int32_t aWidth, int32_t aHeight, HANDLE aFileMapping) {
    MOZ_ASSERT(aWidth);
    MOZ_ASSERT(aHeight);
    MOZ_ASSERT(aFileMapping);

    mWidth = aWidth;
    mHeight = aHeight;
    mFileMapping = aFileMapping;

    void* mappedFilePtr =
        ::MapViewOfFile(mFileMapping, FILE_MAP_ALL_ACCESS, 0 /*offsetHigh*/,
                        0 /*offsetLow*/, 0 /*bytesToMap*/);
    if (!mappedFilePtr) {
      return false;
    }

    mPixelData = reinterpret_cast<unsigned char*>(mappedFilePtr);

    return true;
  }

  HBITMAP CreateDIBSection() {
    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
    bitmapInfo.bmiHeader.biWidth = mWidth;
    bitmapInfo.bmiHeader.biHeight = -mHeight;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    void* dummy = nullptr;
    return ::CreateDIBSection(nullptr /*paletteDC*/, &bitmapInfo,
                              DIB_RGB_COLORS, &dummy, mFileMapping,
                              0 /*offset*/);
  }

  HANDLE CreateRemoteFileMapping(DWORD aTargetProcessId) {
    MOZ_ASSERT(aTargetProcessId);

    HANDLE fileMapping = nullptr;
    if (!ipc::DuplicateHandle(mFileMapping, aTargetProcessId, &fileMapping,
                              0 /*desiredAccess*/, DUPLICATE_SAME_ACCESS)) {
      return nullptr;
    }
    return fileMapping;
  }

  already_AddRefed<gfx::DrawTarget> CreateDrawTarget() {
    return gfx::Factory::CreateDrawTargetForData(
        gfx::BackendType::CAIRO, mPixelData, IntSize(mWidth, mHeight),
        GetStride(), gfx::SurfaceFormat::B8G8R8A8);
  }

  int32_t GetWidth() { return mWidth; }

  int32_t GetHeight() { return mHeight; }

  SharedImage(const SharedImage&) = delete;
  SharedImage(SharedImage&&) = delete;
  SharedImage& operator=(const SharedImage&) = delete;
  SharedImage& operator=(SharedImage&&) = delete;

 private:
  int32_t GetStride() {
    constexpr int32_t kBytesPerPixel = 4;

    // DIB requires 32-bit row alignment
    return (((mWidth * kBytesPerPixel) + 3) / 4) * 4;
  }

  int32_t mWidth;
  int32_t mHeight;
  HANDLE mFileMapping;
  unsigned char* mPixelData;
};

class PresentableSharedImage {
 public:
  PresentableSharedImage()
      : mSharedImage(),
        mDeviceContext(nullptr),
        mDIBSection(nullptr),
        mSavedObject(nullptr) {}

  ~PresentableSharedImage() {
    if (mSavedObject) {
      MOZ_ALWAYS_TRUE(::SelectObject(mDeviceContext, mSavedObject));
    }

    if (mDIBSection) {
      MOZ_ALWAYS_TRUE(::DeleteObject(mDIBSection));
    }

    if (mDeviceContext) {
      MOZ_ALWAYS_TRUE(::DeleteDC(mDeviceContext));
    }
  }

  bool Initialize(int32_t aWidth, int32_t aHeight) {
    if (!mSharedImage.Initialize(aWidth, aHeight)) {
      return false;
    }

    mDeviceContext = ::CreateCompatibleDC(nullptr);
    if (!mDeviceContext) {
      return false;
    }

    mDIBSection = mSharedImage.CreateDIBSection();
    if (!mDIBSection) {
      return false;
    }

    mSavedObject = ::SelectObject(mDeviceContext, mDIBSection);
    if (!mSavedObject) {
      return false;
    }

    return true;
  }

  bool PresentToWindow(HWND aWindowHandle,
                       nsTransparencyMode aTransparencyMode) {
    if (aTransparencyMode == eTransparencyTransparent) {
      // If our window is a child window or a child-of-a-child, the window
      // that needs to be updated is the top level ancestor of the tree
      HWND topLevelWindow = WinUtils::GetTopLevelHWND(aWindowHandle, true);
      MOZ_ASSERT(::GetWindowLongPtr(topLevelWindow, GWL_EXSTYLE) &
                 WS_EX_LAYERED);

      BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
      SIZE winSize = {mSharedImage.GetWidth(), mSharedImage.GetHeight()};
      POINT srcPos = {0, 0};
      return !!::UpdateLayeredWindow(
          topLevelWindow, nullptr /*paletteDC*/, nullptr /*newPos*/, &winSize,
          mDeviceContext, &srcPos, 0 /*colorKey*/, &bf, ULW_ALPHA);
    }

    HDC windowDC = ::GetDC(aWindowHandle);
    if (!windowDC) {
      return false;
    }

    bool result = ::BitBlt(windowDC, 0 /*dstX*/, 0 /*dstY*/,
                           mSharedImage.GetWidth(), mSharedImage.GetHeight(),
                           mDeviceContext, 0 /*srcX*/, 0 /*srcY*/, SRCCOPY);

    MOZ_ALWAYS_TRUE(::ReleaseDC(aWindowHandle, windowDC));

    return result;
  }

  HANDLE CreateRemoteFileMapping(DWORD aTargetProcessId) {
    return mSharedImage.CreateRemoteFileMapping(aTargetProcessId);
  }

  already_AddRefed<gfx::DrawTarget> CreateDrawTarget() {
    return mSharedImage.CreateDrawTarget();
  }

  int32_t GetWidth() { return mSharedImage.GetWidth(); }

  int32_t GetHeight() { return mSharedImage.GetHeight(); }

  PresentableSharedImage(const PresentableSharedImage&) = delete;
  PresentableSharedImage(PresentableSharedImage&&) = delete;
  PresentableSharedImage& operator=(const PresentableSharedImage&) = delete;
  PresentableSharedImage& operator=(PresentableSharedImage&&) = delete;

 private:
  SharedImage mSharedImage;
  HDC mDeviceContext;
  HBITMAP mDIBSection;
  HGDIOBJ mSavedObject;
};

Provider::Provider()
    : mWindowHandle(nullptr),
      mTargetProcessId(0),
      mFileMapping(nullptr),
      mRequestReadyEvent(nullptr),
      mResponseReadyEvent(nullptr),
      mSharedDataPtr(nullptr),
      mStopServiceThread(false),
      mServiceThread(),
      mBackbuffer() {}

Provider::~Provider() {
  mBackbuffer.reset();

  if (mServiceThread.joinable()) {
    mStopServiceThread = true;
    MOZ_ALWAYS_TRUE(::SetEvent(mRequestReadyEvent));
    mServiceThread.join();
  }

  if (mSharedDataPtr) {
    MOZ_ALWAYS_TRUE(::UnmapViewOfFile(mSharedDataPtr));
  }

  if (mResponseReadyEvent) {
    MOZ_ALWAYS_TRUE(::CloseHandle(mResponseReadyEvent));
  }

  if (mRequestReadyEvent) {
    MOZ_ALWAYS_TRUE(::CloseHandle(mRequestReadyEvent));
  }

  if (mFileMapping) {
    MOZ_ALWAYS_TRUE(::CloseHandle(mFileMapping));
  }
}

bool Provider::Initialize(HWND aWindowHandle, DWORD aTargetProcessId,
                          nsTransparencyMode aTransparencyMode) {
  MOZ_ASSERT(aWindowHandle);
  MOZ_ASSERT(aTargetProcessId);

  mWindowHandle = aWindowHandle;
  mTargetProcessId = aTargetProcessId;

  mFileMapping = ::CreateFileMappingW(
      INVALID_HANDLE_VALUE, nullptr /*secattr*/, PAGE_READWRITE, 0 /*sizeHigh*/,
      static_cast<DWORD>(sizeof(SharedData)), nullptr /*name*/);
  if (!mFileMapping) {
    return false;
  }

  mRequestReadyEvent =
      ::CreateEventW(nullptr /*secattr*/, FALSE /*manualReset*/,
                     FALSE /*initialState*/, nullptr /*name*/);
  if (!mRequestReadyEvent) {
    return false;
  }

  mResponseReadyEvent =
      ::CreateEventW(nullptr /*secattr*/, FALSE /*manualReset*/,
                     FALSE /*initialState*/, nullptr /*name*/);
  if (!mResponseReadyEvent) {
    return false;
  }

  void* mappedFilePtr =
      ::MapViewOfFile(mFileMapping, FILE_MAP_ALL_ACCESS, 0 /*offsetHigh*/,
                      0 /*offsetLow*/, 0 /*bytesToMap*/);
  if (!mappedFilePtr) {
    return false;
  }

  mSharedDataPtr = reinterpret_cast<SharedData*>(mappedFilePtr);

  mStopServiceThread = false;

  mServiceThread = std::thread([this] { this->ThreadMain(); });

  mTransparencyMode = aTransparencyMode;

  return true;
}

Maybe<RemoteBackbufferHandles> Provider::CreateRemoteHandles() {
  HANDLE fileMapping = nullptr;
  if (!ipc::DuplicateHandle(mFileMapping, mTargetProcessId, &fileMapping,
                            0 /*desiredAccess*/, DUPLICATE_SAME_ACCESS)) {
    return Nothing();
  }

  HANDLE requestReadyEvent = nullptr;
  if (!ipc::DuplicateHandle(mRequestReadyEvent, mTargetProcessId,
                            &requestReadyEvent, 0 /*desiredAccess*/,
                            DUPLICATE_SAME_ACCESS)) {
    return Nothing();
  }

  HANDLE responseReadyEvent = nullptr;
  if (!ipc::DuplicateHandle(mResponseReadyEvent, mTargetProcessId,
                            &responseReadyEvent, 0 /*desiredAccess*/,
                            DUPLICATE_SAME_ACCESS)) {
    return Nothing();
  }

  return Some(RemoteBackbufferHandles(
      reinterpret_cast<WindowsHandle>(fileMapping),
      reinterpret_cast<WindowsHandle>(requestReadyEvent),
      reinterpret_cast<WindowsHandle>(responseReadyEvent)));
}

void Provider::UpdateTransparencyMode(nsTransparencyMode aTransparencyMode) {
  mTransparencyMode = aTransparencyMode;
}

void Provider::ThreadMain() {
  while (true) {
    MOZ_ALWAYS_TRUE(::WaitForSingleObject(mRequestReadyEvent, INFINITE) ==
                    WAIT_OBJECT_0);

    if (mStopServiceThread) {
      break;
    }

    switch (mSharedDataPtr->dataType) {
      case SharedDataType::BorrowRequest: {
        BorrowResponseData responseData = {};

        HandleBorrowRequest(&responseData);

        mSharedDataPtr->dataType = SharedDataType::BorrowResponse;
        mSharedDataPtr->data.borrowResponse = responseData;

        MOZ_ALWAYS_TRUE(::SetEvent(mResponseReadyEvent));

        break;
      }
      case SharedDataType::PresentRequest: {
        PresentResponseData responseData = {};

        HandlePresentRequest(&responseData);

        mSharedDataPtr->dataType = SharedDataType::PresentResponse;
        mSharedDataPtr->data.presentResponse = responseData;

        MOZ_ALWAYS_TRUE(::SetEvent(mResponseReadyEvent));

        break;
      }
      default:
        break;
    };
  }
}

void Provider::HandleBorrowRequest(BorrowResponseData* aResponseData) {
  MOZ_ASSERT(aResponseData);

  aResponseData->result = ResponseResult::Error;

  RECT clientRect = {};
  if (!::GetClientRect(mWindowHandle, &clientRect)) {
    return;
  }

  MOZ_ASSERT(clientRect.left == 0);
  MOZ_ASSERT(clientRect.top == 0);

  int32_t width = clientRect.right ? clientRect.right : 1;
  int32_t height = clientRect.bottom ? clientRect.bottom : 1;

  bool needNewBackbuffer = !mBackbuffer || (mBackbuffer->GetWidth() != width) ||
                           (mBackbuffer->GetHeight() != height);

  if (!needNewBackbuffer) {
    aResponseData->result = ResponseResult::BorrowSameBuffer;
    return;
  }

  mBackbuffer.reset();

  mBackbuffer = std::make_unique<PresentableSharedImage>();
  if (!mBackbuffer->Initialize(width, height)) {
    return;
  }

  HANDLE remoteFileMapping =
      mBackbuffer->CreateRemoteFileMapping(mTargetProcessId);
  if (!remoteFileMapping) {
    return;
  }

  aResponseData->result = ResponseResult::BorrowSuccess;
  aResponseData->width = width;
  aResponseData->height = height;
  aResponseData->fileMapping = remoteFileMapping;
}

void Provider::HandlePresentRequest(PresentResponseData* aResponseData) {
  MOZ_ASSERT(aResponseData);

  aResponseData->result = ResponseResult::Error;

  if (!mBackbuffer) {
    return;
  }

  if (!mBackbuffer->PresentToWindow(mWindowHandle, mTransparencyMode)) {
    return;
  }

  aResponseData->result = ResponseResult::PresentSuccess;
}

Client::Client()
    : mFileMapping(nullptr),
      mRequestReadyEvent(nullptr),
      mResponseReadyEvent(nullptr),
      mSharedDataPtr(nullptr),
      mBackbuffer() {}

Client::~Client() {
  mBackbuffer.reset();

  if (mSharedDataPtr) {
    MOZ_ALWAYS_TRUE(::UnmapViewOfFile(mSharedDataPtr));
  }

  if (mResponseReadyEvent) {
    MOZ_ALWAYS_TRUE(::CloseHandle(mResponseReadyEvent));
  }

  if (mRequestReadyEvent) {
    MOZ_ALWAYS_TRUE(::CloseHandle(mRequestReadyEvent));
  }

  if (mFileMapping) {
    MOZ_ALWAYS_TRUE(::CloseHandle(mFileMapping));
  }
}

bool Client::Initialize(const RemoteBackbufferHandles& aRemoteHandles) {
  MOZ_ASSERT(aRemoteHandles.fileMapping());
  MOZ_ASSERT(aRemoteHandles.requestReadyEvent());
  MOZ_ASSERT(aRemoteHandles.responseReadyEvent());

  mFileMapping = reinterpret_cast<HANDLE>(aRemoteHandles.fileMapping());
  mRequestReadyEvent =
      reinterpret_cast<HANDLE>(aRemoteHandles.requestReadyEvent());
  mResponseReadyEvent =
      reinterpret_cast<HANDLE>(aRemoteHandles.responseReadyEvent());

  void* mappedFilePtr =
      ::MapViewOfFile(mFileMapping, FILE_MAP_ALL_ACCESS, 0 /*offsetHigh*/,
                      0 /*offsetLow*/, 0 /*bytesToMap*/);
  if (!mappedFilePtr) {
    return false;
  }

  mSharedDataPtr = reinterpret_cast<SharedData*>(mappedFilePtr);

  return true;
}

already_AddRefed<gfx::DrawTarget> Client::BorrowDrawTarget() {
  mSharedDataPtr->dataType = SharedDataType::BorrowRequest;
  MOZ_ALWAYS_TRUE(::SetEvent(mRequestReadyEvent));
  MOZ_ALWAYS_TRUE(::WaitForSingleObject(mResponseReadyEvent, INFINITE) ==
                  WAIT_OBJECT_0);

  if (mSharedDataPtr->dataType != SharedDataType::BorrowResponse) {
    return nullptr;
  }

  BorrowResponseData responseData = mSharedDataPtr->data.borrowResponse;

  if ((responseData.result != ResponseResult::BorrowSameBuffer) &&
      (responseData.result != ResponseResult::BorrowSuccess)) {
    return nullptr;
  }

  if (responseData.result == ResponseResult::BorrowSuccess) {
    mBackbuffer.reset();

    mBackbuffer = std::make_unique<SharedImage>();
    if (!mBackbuffer->InitializeRemote(responseData.width, responseData.height,
                                       responseData.fileMapping)) {
      return nullptr;
    }
  }

  return mBackbuffer->CreateDrawTarget();
}

bool Client::PresentDrawTarget() {
  mSharedDataPtr->dataType = SharedDataType::PresentRequest;
  MOZ_ALWAYS_TRUE(::SetEvent(mRequestReadyEvent));
  MOZ_ALWAYS_TRUE(::WaitForSingleObject(mResponseReadyEvent, INFINITE) ==
                  WAIT_OBJECT_0);

  if (mSharedDataPtr->dataType != SharedDataType::PresentResponse) {
    return false;
  }

  if (mSharedDataPtr->data.presentResponse.result !=
      ResponseResult::PresentSuccess) {
    return false;
  }

  return true;
}

}  // namespace remote_backbuffer
}  // namespace widget
}  // namespace mozilla
