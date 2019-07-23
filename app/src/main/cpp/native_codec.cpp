/*
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* This is a JNI example where we use native methods to play video
 * using the native AMedia* APIs.
 * See the corresponding Java source file located at:
 *
 *   src/com/example/nativecodec/NativeMedia.java
 *
 * In this example we use assert() for "impossible" error conditions,
 * and explicit handling and recovery for more likely error conditions.
 */

#include <assert.h>
#include <jni.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <omp.h>

#include <opencv2/opencv.hpp>

#include "looper.h"
#include "media/NdkMediaCodec.h"
#include "media/NdkMediaExtractor.h"

#include "platform.h"
#include "net.h"

// for __android_log_print(ANDROID_LOG_INFO, "YourApp", "formatted message");
#include <android/log.h>
#define TAG "NativeCodec"
#define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// for native window JNI
#include <android/native_window_jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

typedef struct {
    int fd;
    ANativeWindow* window;
    AMediaExtractor* ex;
    AMediaCodec *codec;
    int64_t renderstart;
    bool sawInputEOS;
    bool sawOutputEOS;
    bool isPlaying;
    bool renderonce;
} workerdata;

workerdata data = {-1, NULL, NULL, NULL, 0, false, false, false, false};

enum {
    kMsgCodecBuffer,
    kMsgPause,
    kMsgResume,
    kMsgPauseAck,
    kMsgDecodeDone,
    kMsgSeek,
};

struct Object {
    cv::Rect_<float> rect;
    int label;
    float prob;
};

class mylooper: public looper {
    virtual void handle(int what, void* obj);
};

static mylooper *mlooper = NULL;
static int frame_counter = 0, video_width = 0, video_height = 0;

static ncnn::Net mobilenet;

int64_t systemnanotime() {
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000000000LL + now.tv_nsec;
}

void doCodecWork(workerdata *d) {
    ssize_t bufidx = -1;
    if (!d->sawInputEOS) {
        bufidx = AMediaCodec_dequeueInputBuffer(d->codec, 2000);
        //LOGV("input buffer %zd", bufidx);
        if (bufidx >= 0) {
            size_t bufsize;
            auto buf = AMediaCodec_getInputBuffer(d->codec, bufidx, &bufsize);
            auto sampleSize = AMediaExtractor_readSampleData(d->ex, buf, bufsize);
            if (sampleSize < 0) {
                sampleSize = 0;
                d->sawInputEOS = true;
                LOGV("EOS");
            }
            auto presentationTimeUs = AMediaExtractor_getSampleTime(d->ex);

            AMediaCodec_queueInputBuffer(d->codec, bufidx, 0, sampleSize, presentationTimeUs,
                    d->sawInputEOS ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0);
            AMediaExtractor_advance(d->ex);
        }
    }

    if (!d->sawOutputEOS) {
        AMediaCodecBufferInfo info;
        auto status = AMediaCodec_dequeueOutputBuffer(d->codec, &info, 0);
        //LOGV("buffer size: %d, buffer offset: %d", info.size, info.offset);
        if (status >= 0) {
            size_t outsize;
            uint8_t *yuv_data = AMediaCodec_getOutputBuffer(d->codec, status, &outsize);

            int64 start_ticks = systemnanotime();
            cv::Mat yuvImage(video_height+video_height/2, video_width, CV_8UC1, yuv_data);
            cv::Mat rgba_image(video_height, video_width, CV_8UC4);
            cv::Mat bgr_image(video_height, video_width, CV_8UC3);
            cv::cvtColor(yuvImage, rgba_image, cv::COLOR_YUV2RGBA_NV12);
            cv::cvtColor(rgba_image, bgr_image, cv::COLOR_RGBA2BGR);
            int64 end_ticks = systemnanotime();
            float cvt_time = (float)(end_ticks-start_ticks);

            const int target_size = 300;

            start_ticks = systemnanotime();

            ncnn::Mat in = ncnn::Mat::from_pixels_resize(bgr_image.data, ncnn::Mat::PIXEL_BGR,
                    video_width, video_height, target_size, target_size);

            const float mean_vals[3] = {127.5f, 127.5f, 127.5f};
            const float norm_vals[3] = {1.0/127.5,1.0/127.5,1.0/127.5};
            in.substract_mean_normalize(mean_vals, norm_vals);

            ncnn::Extractor ex = mobilenet.create_extractor();
            ex.set_num_threads(8);
            ex.input("data", in);

            ncnn::Mat out;
            ex.extract("detection_out",out);

            std::vector<Object> objects;

            for (int i = 0; i < out.h; i++) {
                const float* values = out.row(i);

                Object object;
                object.label = values[0];
                object.prob = values[1];
                object.rect.x = values[2] * video_width;
                object.rect.y = values[3] * video_height;
                object.rect.width = values[4] * video_width - object.rect.x;
                object.rect.height = values[5] * video_height - object.rect.y;

                objects.push_back(object);
            }

            end_ticks = systemnanotime();
            float inference_time = (float)(end_ticks-start_ticks);

            for (size_t i = 0; i < objects.size(); i++) {
                const Object &obj = objects[i];
                cv::Scalar color;

                if (obj.label == 7)
                    color = cv::Scalar(255, 0, 0, 0);
                else if (obj.label == 6)
                    color = cv::Scalar(0, 255, 0, 0);
                else if (obj.label == 15)
                    color = cv::Scalar(0, 0, 255, 0);
                else if (obj.label == 2 || obj.label == 14)
                    color = cv::Scalar(0, 255, 255, 0);

                if (obj.label == 7 || obj.label == 6 || obj.label == 15 ||
                    obj.label == 2 || obj.label == 14)
                    cv::rectangle(rgba_image, obj.rect, color);
            }

            char cvt_time_str[100], infer_time_str[100];
            sprintf(cvt_time_str, "cvt color: %.2f us", cvt_time/1000);
            sprintf(infer_time_str, "inference: %.2f us", inference_time/1000);

            cv::putText(rgba_image, cvt_time_str, cv::Point(20, 30), cv::FONT_HERSHEY_SIMPLEX, 0.6,
                    cv::Scalar(255, 0, 0, 0), 1);
            cv::putText(rgba_image, infer_time_str, cv::Point(20, 60), cv::FONT_HERSHEY_SIMPLEX, 0.6,
                        cv::Scalar(255, 0, 0, 0), 1);

            ANativeWindow_Buffer out_buffer;
            ANativeWindow_lock(d->window, &out_buffer, NULL);
            //LOGV("width %d, height %d, stride %d, format %d", out_buffer.width, out_buffer.height,
            //        out_buffer.stride, out_buffer.format);

            memcpy(out_buffer.bits, rgba_image.data, video_width*video_height*4);

            ANativeWindow_unlockAndPost(d->window);

            if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
                LOGV("output EOS");
                d->sawOutputEOS = true;
            }

            int64_t presentationNano = info.presentationTimeUs * 1000;
            if (d->renderstart < 0) {
                d->renderstart = systemnanotime() - presentationNano;
            }
            int64_t delay = (d->renderstart + presentationNano) - systemnanotime();
            if (delay > 0) {
                //usleep(delay / 1000);
            }

            LOGV("pt: %d, delay: %d", info.presentationTimeUs, delay);

            AMediaCodec_releaseOutputBuffer(d->codec, status, false);
            if (d->renderonce) {
                d->renderonce = false;
                return;
            }
        } else if (status == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
            LOGV("output buffers changed");
        } else if (status == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            auto format = AMediaCodec_getOutputFormat(d->codec);
            LOGV("format changed to: %s", AMediaFormat_toString(format));
            AMediaFormat_delete(format);
        } else if (status == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
            LOGV("no output buffer right now");
        } else {
            LOGV("unexpected info code: %zd", status);
        }
    }

    if (!d->sawInputEOS || !d->sawOutputEOS) {
        mlooper->post(kMsgCodecBuffer, d);
    }
}

void mylooper::handle(int what, void* obj) {
    switch (what) {
        case kMsgCodecBuffer:
            doCodecWork((workerdata*)obj);
            break;

        case kMsgDecodeDone:
        {
            workerdata *d = (workerdata*)obj;
            AMediaCodec_stop(d->codec);
            AMediaCodec_delete(d->codec);
            AMediaExtractor_delete(d->ex);
            d->sawInputEOS = true;
            d->sawOutputEOS = true;
        }
        break;

        case kMsgSeek:
        {
            workerdata *d = (workerdata*)obj;
            AMediaExtractor_seekTo(d->ex, 0, AMEDIAEXTRACTOR_SEEK_NEXT_SYNC);
            AMediaCodec_flush(d->codec);
            d->renderstart = -1;
            d->sawInputEOS = false;
            d->sawOutputEOS = false;
            if (!d->isPlaying) {
                d->renderonce = true;
                post(kMsgCodecBuffer, d);
            }
            LOGV("seeked");
        }
        break;

        case kMsgPause:
        {
            workerdata *d = (workerdata*)obj;
            if (d->isPlaying) {
                // flush all outstanding codecbuffer messages with a no-op message
                d->isPlaying = false;
                post(kMsgPauseAck, NULL, true);
            }
        }
        break;

        case kMsgResume:
        {
            workerdata *d = (workerdata*)obj;
            if (!d->isPlaying) {
                d->renderstart = -1;
                d->isPlaying = true;
                post(kMsgCodecBuffer, d);
            }
        }
        break;
    }
}

extern "C" {

JNIEXPORT jboolean JNICALL Java_com_testapp_h264nativedecoder_MainActivity_createStreamingMediaPlayer(JNIEnv* env,
        jclass clazz, jstring filename)
{
    LOGV("@@@ create");

    mobilenet.load_param("/sdcard/Movies/mobilenet_ssd_voc_ncnn.param");
    mobilenet.load_model("/sdcard/Movies/mobilenet_ssd_voc_ncnn.bin");

    // convert Java string to UTF-8
    const char *utf8 = env->GetStringUTFChars(filename, NULL);
    LOGV("opening %s", utf8);

    off_t outStart, outLen;
    int fd = open(utf8, O_RDONLY);

    env->ReleaseStringUTFChars(filename, utf8);
    if (fd < 0) {
        LOGE("failed to open file: %s %d (%s)", utf8, fd, strerror(errno));
        return JNI_FALSE;
    }

    struct stat file_stat;
    fstat(fd, &file_stat);
    off64_t file_size = file_stat.st_size;

    data.fd = fd;

    workerdata *d = &data;

    AMediaExtractor *ex = AMediaExtractor_new();
    media_status_t err = AMediaExtractor_setDataSourceFd(ex, d->fd, 0, file_size);
    close(d->fd);
    if (err != AMEDIA_OK) {
        LOGV("setDataSource error: %d", err);
        return JNI_FALSE;
    }

    int numtracks = AMediaExtractor_getTrackCount(ex);

    AMediaCodec *codec = NULL;

    LOGV("input has %d tracks", numtracks);
    for (int i = 0; i < numtracks; i++) {
        AMediaFormat *format = AMediaExtractor_getTrackFormat(ex, i);
        const char *s = AMediaFormat_toString(format);
        LOGV("track %d format: %s", i, s);
        const char *mime;
        if (!AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime)) {
            LOGV("no mime type");
            return JNI_FALSE;
        } else if (!strncmp(mime, "video/", 6)) {
            AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_WIDTH, &video_width);
            AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_HEIGHT, &video_height);
            LOGV("video width %d, video height %d", video_width, video_height);

            // Omitting most error handling for clarity.
            // Production code should check for errors.
            AMediaExtractor_selectTrack(ex, i);
            codec = AMediaCodec_createDecoderByType(mime);
            AMediaCodec_configure(codec, format, NULL, NULL, 0);

            d->ex = ex;
            d->codec = codec;
            d->renderstart = -1;
            d->sawInputEOS = false;
            d->sawOutputEOS = false;
            d->isPlaying = false;
            d->renderonce = true;
            AMediaCodec_start(codec);
        }
        AMediaFormat_delete(format);
    }

    mlooper = new mylooper();
    mlooper->post(kMsgCodecBuffer, d);

    return JNI_TRUE;
}

// set the playing state for the streaming media player
JNIEXPORT void JNICALL Java_com_testapp_h264nativedecoder_MainActivity_setPlayingStreamingMediaPlayer(JNIEnv* env,
        jclass clazz, jboolean isPlaying)
{
    LOGV("@@@ playpause: %d", isPlaying);
    if (mlooper) {
        if (isPlaying) {
            mlooper->post(kMsgResume, &data);
        } else {
            mlooper->post(kMsgPause, &data);
        }
    }
}


// shut down the native media system
JNIEXPORT void JNICALL Java_com_testapp_h264nativedecoder_MainActivity_shutdown(JNIEnv* env, jclass clazz)
{
    LOGV("@@@ shutdown");
    if (mlooper) {
        mlooper->post(kMsgDecodeDone, &data, true /* flush */);
        mlooper->quit();
        delete mlooper;
        mlooper = NULL;
    }
    if (data.window) {
        ANativeWindow_release(data.window);
        data.window = NULL;
    }
}


// set the surface
JNIEXPORT void JNICALL Java_com_testapp_h264nativedecoder_MainActivity_setSurface(JNIEnv *env, jclass clazz, jobject surface)
{
    // obtain a native window from a Java surface
    if (data.window) {
        ANativeWindow_release(data.window);
        data.window = NULL;
    }
    data.window = ANativeWindow_fromSurface(env, surface);
    ANativeWindow_setBuffersGeometry(data.window, video_width, video_height,
            AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM);
    LOGV("@@@ setsurface %p", data.window);
}


// rewind the streaming media player
JNIEXPORT void JNICALL Java_com_testapp_h264nativedecoder_MainActivity_rewindStreamingMediaPlayer(JNIEnv *env, jclass clazz)
{
    LOGV("@@@ rewind");
    if (mlooper) {
        mlooper->post(kMsgSeek, &data);
    }
}

}
