LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := sdlvideo

SDL_PATH := ../SDL2-2.0.4
FFMPEG_PATH := ../ffmpeg

LOCAL_C_INCLUDES := $(LOCAL_PATH)/$(SDL_PATH)/include \
					$(LOCAL_PATH)/$(FFMPEG_PATH)/include

# Add your application source files here...
LOCAL_SRC_FILES := $(SDL_PATH)/src/main/android/SDL_android_main.c \
	sdlvideo.c

LOCAL_SHARED_LIBRARIES := SDL2 \
						  avcodec \
						  avdevice \
						  avfilter \
						  avformat \
						  avutil \
						  postproc \
						  swresample \
						  swscale \
						  x264 \
						  fdkaac

LOCAL_LDLIBS := -lGLESv1_CM -lGLESv2 -llog

include $(BUILD_SHARED_LIBRARY)
