# libseurat (leonardo) 빌드 시스템 분석 보고서

작성일: 2026-03-20

---

## 1. 프로젝트 개요

`libseurat`는 `leonardo`라는 CMake 기반 멀티플랫폼 미디어 라이브러리 빌더입니다.  
외부 오픈소스 라이브러리들을 Android, iOS, macOS, Windows 대상으로 크로스컴파일하여 정적 라이브러리 형태로 패키징하는 것이 목적입니다.

### 지원 예정 라이브러리 (README 기준)
| 라이브러리 | 용도 | 현재 상태 |
|---|---|---|
| OpenSSL | 암호화 | 빌드 대상 포함 (버전 매우 오래됨) |
| x264 | H.264 인코더 | 빌드 대상 포함 (미완성) |
| OpenH264 | H.264 코덱 (Cisco) | 빌드 대상 포함 (심각하게 미완성) |
| libx264 / faac | AAC 인코더 | 주석처리됨 |
| librtmp | RTMP 프로토콜 | 주석처리됨 + 소스 없음 |
| libsrt | SRT 프로토콜 | 주석처리됨 + 소스 없음 |
| libyuv | YUV 처리 | 주석처리됨 + 심각한 버그 |
| libjingle (WebRTC) | P2P 미디어 | 주석처리됨 + 소스 없음 |

---

## 2. 빌드 흐름

```
build.sh [TARGET] [BUILD_TYPE]
    ↓
cmake . -B WORK/{ARCH} -D LEONARDO_TARGET=... -D LEONARDO_ARCH=...
    ↓
CMakeLists.txt
    → toolchain-{android|ios|osx|msvc}.cmake  (크로스컴파일 환경 설정)
    → toolchain-common.cmake                   (시스템 감지)
    → build-openssl.cmake                      (ExternalProject)
    → build-x264.cmake                         (ExternalProject)
    → build-open-h264.cmake                    (ExternalProject, 미완성)
    ↓
make && make install
    → out/{ARCH}/include/
    → out/{ARCH}/lib/
```

---

## 3. 발견된 문제점

### 3.1 [치명적] CMakeLists.txt 하드코딩으로 인한 옵션 무시

**파일**: `CMakeLists.txt`, 9~10번 줄

```cmake
set(LEONARDO_TARGET "ANDROID")   ← 이 값이 -D 옵션을 덮어씀
set(LEONARDO_ARCH "arm64-v8a")   ← 이 값이 -D 옵션을 덮어씀
```

**원인**: CMake에서 일반 `set()` 명령은 `-D`로 전달된 CACHE 변수를 shadow(덮어쓰기)합니다.  
따라서 `build.sh`에서 `-D LEONARDO_TARGET=IOS`를 전달해도 항상 `ANDROID`로 동작합니다.

**수정 방법**:
```cmake
# 기존 하드코딩 제거 후 CACHE 변수로 선언
set(LEONARDO_TARGET "ANDROID" CACHE STRING "Build target platform: ANDROID, IOS, MSVC, OSX")
set(LEONARDO_ARCH "arm64-v8a" CACHE STRING "Target architecture")
```

---

### 3.2 [치명적] Android NDK r13 의존 (2016년 버전)

**파일**: `cmake/toolchain/toolchain-android.cmake`

**문제 목록**:

| 항목 | 현재 코드 | 문제 |
|---|---|---|
| NDK 버전 | `NDK_R13` 환경변수 | NDK r13은 2016년 배포. 현재 최신은 r27 |
| 컴파일러 | GCC 4.9 (`NDK_TOOLCHAIN_VERSION=4.9`) | NDK r18(2018)에서 GCC 완전 제거 |
| STL | `gnustl_static` | NDK r18에서 제거. 현재는 `c++_static` 사용 |
| Toolchain 구조 | `toolchains/{arch}-4.9/prebuilt/` | 현재 NDK는 `toolchains/llvm/prebuilt/` 구조 |
| sysroot 경로 | `platforms/android-{api}/{arch-tag}` | NDK r21부터 단일 sysroot로 통합됨 |

**pre_build_android.sh 문제**:
```bash
# cmake/builder/openssl/pre_build_android.sh
export CC=$target_host-gcc   ← GCC가 없는 NDK에서 실패
export CXX=$target_host-g++  ← 동일
```

**수정 방향**: NDK r21 이상(권장 r26) 기준으로 전면 재작성 필요.
```cmake
# 현대적 NDK toolchain 설정 예시
set(CMAKE_ANDROID_NDK "$ENV{ANDROID_NDK_HOME}")
set(CMAKE_ANDROID_STL_TYPE "c++_static")       # gnustl_static → c++_static
set(CMAKE_ANDROID_NDK_TOOLCHAIN_VERSION "clang") # gcc → clang
```

---

### 3.3 [치명적] CMakeForceCompiler 사용 (CMake 3.12에서 제거됨)

**파일**: `cmake/toolchain/toolchain-ios.cmake`, 99번 줄

```cmake
include(CMakeForceCompiler)  ← CMake 3.6 deprecated, 3.12에서 완전 제거
```

`cmake_minimum_required(VERSION 4.0)`을 요구하는 프로젝트에서 이미 존재하지 않는 모듈을 포함하려 하므로 즉시 오류 발생합니다.

**수정 방법**: 해당 줄 제거. 현대 CMake는 컴파일러 감지를 자동으로 처리합니다.

---

### 3.4 [치명적] OpenSSL 버전이 모두 EOL (보안 지원 종료)

**파일**: `cmake/builder/build-openssl.cmake`

| 플랫폼 | 현재 버전 | EOL 일자 | 현재 최신 |
|---|---|---|---|
| Android | 1.0.2q | 2019-12-31 | 3.4.x |
| iOS | 1.1.1d | 2023-09-11 | 3.4.x |

두 버전 모두 보안 패치가 완전히 중단된 상태입니다.  
OpenSSL 1.0.2 시대의 `./Configure` 인자 방식도 3.x에서 일부 변경되었습니다.

**수정 방향**: OpenSSL 3.3.x 또는 3.4.x로 업그레이드, `Configure` 인자 재검토 필요.

---

### 3.5 [심각] OSX toolchain에 Apple Silicon(arm64) 미지원

**파일**: `cmake/toolchain/toolchain-osx.cmake`

```cmake
if(LEONARDO_ARCH STREQUAL "x86")     # i386
elseif(LEONARDO_ARCH STREQUAL "x86_64")  # Intel Mac
else()
    message(FATAL_ERROR ...)         # arm64은 오류 처리
```

Apple Silicon Mac(M1/M2/M3/M4)에서는 x86_64 빌드도 가능하지만 네이티브 arm64 빌드가 필요합니다.  
**현재 arm64를 전달하면 FATAL_ERROR로 즉시 중단됩니다.**

**build.sh OSX 아키텍처 현황**:
```bash
elif [ "$1" = "OSX" ] ; then
    ARCHS="x86_64"   # arm64 누락
```

**수정 방향**:
```cmake
# toolchain-osx.cmake에 arm64 추가
elseif(LEONARDO_ARCH STREQUAL "arm64")
    set(CMAKE_SYSTEM_NAME "Darwin")
    set(CMAKE_SYSTEM_PROCESSOR "arm64")
    set(CMAKE_OSX_ARCHITECTURES "arm64")
```

Universal Binary(fat binary) 생성을 위해 `lipo` 후처리 단계도 추가 권장.

---

### 3.6 [심각] build-open-h264.cmake 미완성 및 빌드 불가

**파일**: `cmake/builder/build-open-h264.cmake`

```cmake
ExternalProject_Add(OPEN_H264-EXTERNAL
    GIT_REPOSITORY "https://github.com/cisco/openh264.git"
    UPDATE_COMMAND ""
    BUILD_COMMAND cd <SOURCE_DIR> && source ${PRE_BUILD}
#   BUILD_COMMAND && make ${OPEN_H264_BUILD_OPTIONS}  ← 주석처리
#   INSTALL_COMMAND ""                                 ← 주석처리
)
```

- BUILD_COMMAND가 환경변수 설정만 하고 실제 빌드를 수행하지 않음
- INSTALL_COMMAND 없음
- OSX/MSVC 플랫폼 처리 코드 없음
- IOS 섹션에서 `OPEN_H264_BUILD_VERSION`은 설정하지만 `URL`로 사용하지 않음
- `PRE_BUILD` 변수가 Android/iOS에서만 설정되고 OSX에서는 undefined

---

### 3.7 [심각] build-yuv.cmake 심각한 오류

**파일**: `cmake/builder/build-yuv.cmake`

```cmake
# ExternalProject_Add가 동일 이름 "YUV"으로 두 번 정의됨
ExternalProject_Add(YUV
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/src/yuv  ← src/yuv 디렉토리 없음
    ...
)

ExternalProject_Add(YUV                      ← 중복 정의, CMake 오류
    GIT_REPOSITORY "https://code.videolan.org/videolan/x264.git"  ← x264 repo?
    ...
    BUILD_COMMAND cd <SOURCE_DIR> && source ${PRE_BUILD}
    BUILD_COMMAND && make ...  ← BUILD_COMMAND 두 번 = 마지막 것만 적용
    ...
)
```

- `ExternalProject_Add`를 동일 이름으로 두 번 호출 → CMake 오류
- libyuv repo가 아닌 x264 repo 주소 사용
- `src/yuv` 소스 디렉토리 없음

---

### 3.8 [보통] 소스 코드 없는 라이브러리 빌드 참조

다음 라이브러리들은 `src/` 하위 로컬 소스를 참조하지만 해당 디렉토리가 존재하지 않습니다:

| cmake 파일 | 참조 경로 | 현재 상태 |
|---|---|---|
| build-rtmp.cmake | `${CMAKE_SOURCE_DIR}/src/rtmp` | 없음 |
| build-srt.cmake | `${CMAKE_SOURCE_DIR}/src/srt` | 없음 |
| build-rtc.cmake | `${CMAKE_SOURCE_DIR}/src/webrtc` | 없음 |
| build-yuv.cmake | `${CMAKE_SOURCE_DIR}/src/yuv` | 없음 |

이들은 현재 CMakeLists.txt에서 주석처리 되어 있어 즉각적 문제는 없지만, 활성화 시 즉시 실패합니다.

---

### 3.9 [보통] build.sh Android dist 복사 로직 미완성

**파일**: `build.sh`, 91~93번 줄

```bash
#        cp -r ${TEMP_OUT_DIR}/include ${DIST}/jni   ← 주석처리
#        cp ${TEMP_OUT_DIR}/lib/libseurat.* ${TEMP_DIST_DIR}/  ← 주석처리
#        cp ${TEMP_OUT_DIR}/lib/libjingle_peerconnection*.* ${TEMP_DIST_DIR}/  ← 주석처리
```

빌드 완료 후 Android 프로젝트에서 사용할 수 있도록 `dist/` 디렉토리에 복사하는 로직이 모두 주석처리됩니다.

---

### 3.10 [경미] x264 configure 대소문자 문제

**파일**: `cmake/builder/build-x264.cmake`

```cmake
CONFIGURE_COMMAND ./Configure --host=...   # 대문자 C
```

x264의 configure 스크립트는 `./configure` (소문자)입니다.  
OpenSSL은 `./Configure` (대문자)가 맞습니다. 확인 필요.

---

## 4. 문제 요약표

| # | 파일 | 심각도 | 문제 | 수정 가능 여부 |
|---|---|---|---|---|
| 1 | CMakeLists.txt:9-10 | 치명적 | TARGET/ARCH 하드코딩으로 -D 옵션 무시 | 즉시 수정 가능 |
| 2 | toolchain-android.cmake | 치명적 | NDK r13/GCC 4.9 의존 (현재 NDK와 구조 불일치) | 전면 재작성 필요 |
| 3 | toolchain-ios.cmake:99 | 치명적 | CMakeForceCompiler 제거된 모듈 사용 | 해당 줄 제거 |
| 4 | build-openssl.cmake | 치명적 | OpenSSL 1.0.2q / 1.1.1d (모두 EOL) | 버전 업그레이드 |
| 5 | toolchain-osx.cmake | 심각 | Apple Silicon(arm64) 미지원 | 분기 추가 |
| 6 | build-open-h264.cmake | 심각 | BUILD_COMMAND 미완성, INSTALL_COMMAND 없음 | 재작성 필요 |
| 7 | build-yuv.cmake | 심각 | ExternalProject 중복 정의, 잘못된 repo | 재작성 필요 |
| 8 | build-{rtmp,srt,rtc}.cmake | 보통 | src/ 소스 코드 없음 | 소스 추가 또는 외부 다운로드로 변경 |
| 9 | build.sh:91-93 | 보통 | dist 복사 로직 주석처리 | 주석 해제 및 경로 수정 |
| 10 | build-x264.cmake | 경미 | `./Configure` 대소문자 확인 필요 | 검증 후 수정 |

---

## 5. 수정 우선순위별 작업 계획

### Phase 1: 즉시 수정 (빌드 자체가 안 되는 문제)

1. **CMakeLists.txt** - CACHE 변수로 변경 (30분)
2. **toolchain-ios.cmake** - `CMakeForceCompiler` 줄 제거 (5분)
3. **toolchain-osx.cmake** - `arm64` 분기 추가 (30분)
4. **build-openssl.cmake** - OpenSSL 3.x로 버전 업그레이드 (2시간)

### Phase 2: 플랫폼별 재작성 (크로스컴파일 환경 현대화)

5. **toolchain-android.cmake** - NDK r26 기준 전면 재작성 (4~8시간)
   - `c++_static` STL 사용
   - Clang 기반 toolchain
   - 현대적 sysroot 구조 반영
   - `pre_build_android.sh` 클랜 기반으로 재작성

6. **toolchain-osx.cmake** - Universal Binary 지원 추가 (2~4시간)
   - x86_64 + arm64 각각 빌드
   - `lipo` 명령으로 fat binary 생성

### Phase 3: 라이브러리 빌더 완성

7. **build-open-h264.cmake** - 재작성 (2~4시간)
   - Makefile 기반 빌드 방식 올바르게 구현
   - 플랫폼별 빌드 옵션 정리

8. **build-x264.cmake** - Android/iOS/OSX 전 플랫폼 대응 완성 (2~4시간)

9. **build-yuv.cmake** - 재작성 (2시간)
   - libyuv 올바른 repo 사용 (`https://chromium.googlesource.com/libyuv/libyuv`)
   - 중복 ExternalProject 제거

### Phase 4: 선택적 라이브러리 추가

10. **build-rtmp.cmake / build-srt.cmake** - 외부 다운로드 방식으로 전환 (4시간)
    - SRT: `https://github.com/Haivision/srt`
    - librtmp: `https://git.ffmpeg.org/rtmpdump.git` 또는 ffmpeg 내장 사용 검토

---

## 6. 환경 요구사항 현대화 권장 사양

### Android 빌드 (현재 → 권장)
| 항목 | 현재 | 권장 |
|---|---|---|
| NDK | r13 (GCC 4.9) | r26 이상 (Clang 17+) |
| min API | android-16 | android-21 (Android 5.0) |
| STL | gnustl_static | c++_static |
| ABI | x86_64, arm64-v8a | armeabi-v7a, arm64-v8a, x86, x86_64 |

### iOS 빌드 (현재 → 권장)
| 항목 | 현재 | 권장 |
|---|---|---|
| Xcode | 미지정 | 15 이상 |
| iOS min | 7.0 | 13.0 이상 |
| 아키텍처 | x86_64(sim), arm64 | arm64(sim/device), xcframework 패키징 |

### macOS 빌드 (현재 → 권장)
| 항목 | 현재 | 권장 |
|---|---|---|
| 아키텍처 | x86_64만 | x86_64 + arm64 → Universal Binary |
| min macOS | 미지정 | 11.0 이상 (Big Sur) |

### 공통
| 항목 | 현재 | 권장 |
|---|---|---|
| CMake | 4.0 required | 3.21 이상 (4.0은 2025년 릴리스로 호환성 위험) |
| OpenSSL | 1.0.2q / 1.1.1d | 3.3.x 또는 3.4.x |

---

## 7. 참고: 현재 동작 가능한 빌드 조합

위의 문제들을 고려했을 때, **현재 어떤 플랫폼도 성공적으로 빌드되지 않습니다.**

- **ANDROID**: NDK r13 환경이 없으면 즉시 실패, CMakeLists.txt 하드코딩으로 타겟 무시
- **IOS**: `CMakeForceCompiler` 오류로 즉시 실패
- **OSX**: `FATAL_ERROR` 없이 진행 가능한 조건(`x86_64`)이어도 CMakeLists.txt 하드코딩으로 ANDROID로 동작
- **MSVC**: toolchain-msvc.cmake가 사실상 비어 있음

---

*이 문서는 현재 코드베이스의 정적 분석 결과이며, 실제 빌드 시도 없이 작성되었습니다.*
