# -----------------------------------------------------------------------------
# libseurat override for OpenH264's build/platform-ios.mk
#
# The stock file forces ARCH=arm64 to SDK=iPhoneOS and injects a hardcoded
# `-miphoneos-version-min=5.1 -fembed-bitcode`. That breaks:
#   * arm64 iOS simulator (produces device-platform objects that xcframework
#     refuses to package with x86_64 simulator slices),
#   * Xcode 14+ linkers (bitcode support removed).
#
# We rely on the parent build to hand us CC/CXX/CFLAGS/LDFLAGS via the
# environment (see cmake/builder/build-open-h264.cmake), so this override just
# pulls in the common darwin bits and leaves every arch/SDK/version-min flag
# exactly as the caller set it.
# -----------------------------------------------------------------------------

include $(SRC_PATH)build/platform-darwin.mk

CC  ?= clang
CXX ?= clang++
