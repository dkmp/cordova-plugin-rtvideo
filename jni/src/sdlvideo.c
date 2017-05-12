#include <stdlib.h>
#include "SDL.h"
#include "libswscale/swscale.h"
#include "libavformat/avformat.h"
#include "libavutil/pixfmt.h"
#include "libavutil/imgutils.h"
#include "libavutil/Timestamp.h"
#include "libavutil/Time.h"
#include "libavutil/Opt.h"
#include "libavcodec/avcodec.h"
#include "libswresample/swresample.h"
#include <android/log.h>
#include <jni.h>

#define APPNAME "dkmp"
#define ENABLE_DEBUG

#define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, APPNAME, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG , APPNAME, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO , APPNAME, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN , APPNAME, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR , APPNAME, __VA_ARGS__)

#define COMMAND_DKMP_LOAD_FINISH 6
#define COMMAND_DKMP_INIT_ERROR 7

typedef struct _StreamContext {
    AVStream* stream;
    AVCodec* codec;
    AVCodecContext* codecCtx;
    AVFrame* frame;
    SwrContext* swrCtx;
    struct SwsContext* swsCtx;
    int64_t samplesCount;
    uint8_t enable;
    int linesizes[4];
    uint8_t* data[4];
} StreamContext;

AVFormatContext* m_recOutFrameCtx;

StreamContext m_audioRecCtx = {0};

StreamContext m_videoRecCtx = {0};


//debug
#ifdef ENABLE_DEBUG
static void custom_log(void *ptr, int level, const char* fmt, va_list vl){
	FILE *fp=fopen("/storage/emulated/0/av_log.txt","a+");
	if(fp){
		vfprintf(fp,fmt,vl);
		fflush(fp);
		fclose(fp);
	}
}

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char* tag)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    LOGD("tag:%s pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",tag,
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
}
#endif

static void syncPts()
{
	int64_t apts,vpts;
	while(m_audioRecCtx.enable&&m_videoRecCtx.enable)
	{
		apts = av_rescale_q(m_audioRecCtx.samplesCount, m_audioRecCtx.codecCtx->time_base, m_audioRecCtx.stream->time_base);
		vpts = av_rescale_q(m_videoRecCtx.samplesCount, m_videoRecCtx.codecCtx->time_base, m_videoRecCtx.stream->time_base);
		double at = av_q2d(m_audioRecCtx.stream->time_base) * apts;
		double vt = av_q2d(m_videoRecCtx.stream->time_base) * vpts;
		if(vt-at>1)
		{
			m_audioRecCtx.samplesCount += m_audioRecCtx.frame->nb_samples;
			LOGD("syncPts");
		}
		else if(at-vt>1)
		{
			m_videoRecCtx.samplesCount++;
			LOGD("syncPts");
		}
		else
		{
			break;
		}
	}
}

//audio
static int initRecAudio(int audioBitRate)
{
	m_audioRecCtx.codec = avcodec_find_encoder_by_name("libfdk_aac");
	if (!m_audioRecCtx.codec){
		LOGE("avcodec_find_encoder(AV_CODEC_ID_AAC) failed!\n");
		return -1;
	}

	m_audioRecCtx.codecCtx = avcodec_alloc_context3(m_audioRecCtx.codec);
	m_audioRecCtx.codecCtx->codec_type = AVMEDIA_TYPE_AUDIO;
	m_audioRecCtx.codecCtx->sample_fmt = AV_SAMPLE_FMT_S16;
	m_audioRecCtx.codecCtx->sample_rate = 44100;
	m_audioRecCtx.codecCtx->channel_layout = AV_CH_LAYOUT_MONO;
	m_audioRecCtx.codecCtx->channels = av_get_channel_layout_nb_channels(m_audioRecCtx.codecCtx->channel_layout);
	m_audioRecCtx.codecCtx->bit_rate = audioBitRate;
	m_audioRecCtx.codecCtx->time_base = (AVRational){ 1, m_audioRecCtx.codecCtx->sample_rate };

	if (avcodec_open2(m_audioRecCtx.codecCtx, m_audioRecCtx.codec, NULL) < 0){
		LOGE("audio avcodec_open2 failed!\n");
		return -1;
	}

	m_audioRecCtx.stream = avformat_new_stream(m_recOutFrameCtx, m_audioRecCtx.codec);
	if (m_audioRecCtx.stream==NULL){
		LOGE("audio avformat_new_stream failed!\n");
		return -1;
	}
	m_audioRecCtx.stream->id = m_recOutFrameCtx->nb_streams-1;
	avcodec_parameters_from_context(m_audioRecCtx.stream->codecpar, m_audioRecCtx.codecCtx);
	int nbSamples;
	if (m_audioRecCtx.codecCtx->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE)
	{
		nbSamples = 10000;
	}
	else
	{
		nbSamples = m_audioRecCtx.codecCtx->frame_size;
	}


	m_audioRecCtx.frame = av_frame_alloc();
	m_audioRecCtx.frame->format = m_audioRecCtx.codecCtx->sample_fmt;
	m_audioRecCtx.frame->channel_layout = m_audioRecCtx.codecCtx->channel_layout;
	m_audioRecCtx.frame->sample_rate = m_audioRecCtx.codecCtx->sample_rate;
	m_audioRecCtx.frame->nb_samples = nbSamples;
	if (nbSamples) {
	   av_frame_get_buffer(m_audioRecCtx.frame, 0);
	}

	m_audioRecCtx.swrCtx = swr_alloc();
	av_opt_set_int(m_audioRecCtx.swrCtx, "in_channel_layout",  m_audioRecCtx.codecCtx->channel_layout, 0);
	av_opt_set_int(m_audioRecCtx.swrCtx, "out_channel_layout", m_audioRecCtx.codecCtx->channel_layout,  0);
	av_opt_set_int(m_audioRecCtx.swrCtx, "in_sample_rate",     m_audioRecCtx.codecCtx->sample_rate, 0);
	av_opt_set_int(m_audioRecCtx.swrCtx, "out_sample_rate",    m_audioRecCtx.codecCtx->sample_rate, 0);
	av_opt_set_sample_fmt(m_audioRecCtx.swrCtx, "in_sample_fmt", AV_SAMPLE_FMT_S16 , 0);
	av_opt_set_sample_fmt(m_audioRecCtx.swrCtx, "out_sample_fmt", AV_SAMPLE_FMT_S16,  0);
	swr_init(m_audioRecCtx.swrCtx);

	m_audioRecCtx.samplesCount = 0;
	return 0;
}

static int encodeAudio(const uint8_t** audioData)
{
	int ret;
	int gotPacket=0;
	syncPts();
	AVPacket pkt = {0};
	av_init_packet(&pkt);
	swr_convert(m_audioRecCtx.swrCtx, m_audioRecCtx.frame->data, m_audioRecCtx.frame->nb_samples, audioData, m_audioRecCtx.frame->nb_samples);
	ret = avcodec_encode_audio2(m_audioRecCtx.codecCtx, &pkt, m_audioRecCtx.frame, &gotPacket);
	if(ret < 0){
		LOGE("avcodec_encode_audio2 failed!\n");
		return -1;
	}
	if (gotPacket) {
		pkt.pts = av_rescale_q(m_audioRecCtx.samplesCount, m_audioRecCtx.codecCtx->time_base, m_audioRecCtx.stream->time_base);
		m_audioRecCtx.samplesCount += m_audioRecCtx.frame->nb_samples;
		pkt.stream_index = m_audioRecCtx.stream->index;
#ifdef ENABLE_DEBUG
		log_packet(m_recOutFrameCtx, &pkt, "audio");
#endif
		av_interleaved_write_frame(m_recOutFrameCtx, &pkt);
	}
	return 0;
}

static void flushAudio()
{
	int gotPacket;
	AVPacket pkt = {0};
	if (!(m_audioRecCtx.codecCtx->codec->capabilities & CODEC_CAP_DELAY))
	{
		return;
	}
	while (1)
	{
		av_init_packet(&pkt);
		if (avcodec_encode_audio2 (m_audioRecCtx.codecCtx, &pkt, NULL, &gotPacket) < 0)
		{
			break;
		}
		if (!gotPacket)
		{
			break;
		}
		pkt.pts = av_rescale_q(m_audioRecCtx.samplesCount, m_audioRecCtx.codecCtx->time_base, m_audioRecCtx.stream->time_base);
		m_audioRecCtx.samplesCount += m_audioRecCtx.frame->nb_samples;
		pkt.stream_index = m_audioRecCtx.stream->index;
		if (av_interleaved_write_frame(m_recOutFrameCtx, &pkt) < 0)
		{
			break;
		}
	}
	return;
}

static void closeAudio()
{
	avcodec_free_context(&m_audioRecCtx.codecCtx);
	av_frame_free(&m_audioRecCtx.frame);
	swr_free(&m_audioRecCtx.swrCtx);
}

//video
static int initRecVideo(int videoBitRate, int videoWidth, int videoHeight, int videoGopSize, int videoMaxBFrames)
{
	m_videoRecCtx.codec = avcodec_find_encoder(AV_CODEC_ID_H264);
	if (!m_videoRecCtx.codec){
		LOGE("avcodec_find_encoder(AV_CODEC_ID_H264) failed!\n");
		return -1;
	}

	m_videoRecCtx.codecCtx = avcodec_alloc_context3(m_videoRecCtx.codec);
	m_videoRecCtx.codecCtx->codec_id = AV_CODEC_ID_H264;
	m_videoRecCtx.codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
	m_videoRecCtx.codecCtx->width = videoWidth;
	m_videoRecCtx.codecCtx->height = videoHeight;
	m_videoRecCtx.codecCtx->time_base = (AVRational){ 1, 30 };
	m_videoRecCtx.codecCtx->bit_rate = videoBitRate;
	m_videoRecCtx.codecCtx->gop_size = videoGopSize;
	if (m_recOutFrameCtx->oformat->flags & AVFMT_GLOBALHEADER)
	{
		m_videoRecCtx.codecCtx->flags |= CODEC_FLAG_GLOBAL_HEADER;
	}
	m_videoRecCtx.codecCtx->qmin = 10;
	m_videoRecCtx.codecCtx->qmax = 51;
	m_videoRecCtx.codecCtx->max_b_frames = videoMaxBFrames;
	AVDictionary *param = 0;
	av_dict_set(&param, "preset", "ultrafast", 0);
	av_dict_set(&param, "tune", "zerolatency", 0);
	if (avcodec_open2(m_videoRecCtx.codecCtx, m_videoRecCtx.codec, &param) < 0){
		LOGE("video avcodec_open2 failed!\n");
		return -1;
	}

	m_videoRecCtx.stream = avformat_new_stream(m_recOutFrameCtx, m_videoRecCtx.codec);
	if (m_videoRecCtx.stream == NULL){
		LOGE("video avformat_new_stream failed!\n");
		return -1;
	}
	m_videoRecCtx.stream->id = m_recOutFrameCtx->nb_streams-1;
	avcodec_parameters_from_context(m_videoRecCtx.stream->codecpar, m_videoRecCtx.codecCtx);

	m_videoRecCtx.frame = av_frame_alloc();
	m_videoRecCtx.frame->format = m_videoRecCtx.codecCtx->pix_fmt;
	m_videoRecCtx.frame->width  = videoWidth;
	m_videoRecCtx.frame->height = videoHeight;

	av_frame_get_buffer(m_videoRecCtx.frame, 32);
	m_videoRecCtx.samplesCount = 0;

	m_videoRecCtx.swsCtx = sws_getContext(videoWidth, videoHeight, AV_PIX_FMT_NV21,
			m_videoRecCtx.codecCtx->width, m_videoRecCtx.codecCtx->height, m_videoRecCtx.codecCtx->pix_fmt,
			SWS_FAST_BILINEAR, NULL, NULL, NULL);
	return 0;
}

static int encodeVideo(const uint8_t* data)
{
	int gotPacket=0;
	int ret;
	int i;
	syncPts();
	AVPacket pkt={0};
	av_init_packet(&pkt);
	av_image_fill_arrays(m_videoRecCtx.data, m_videoRecCtx.linesizes, data, AV_PIX_FMT_NV21,
			m_videoRecCtx.codecCtx->width, m_videoRecCtx.codecCtx->height, 32);
	sws_scale(m_videoRecCtx.swsCtx, (const uint8_t * const *)m_videoRecCtx.data, m_videoRecCtx.linesizes,
			0, m_videoRecCtx.codecCtx->height, m_videoRecCtx.frame->data, m_videoRecCtx.frame->linesize);
	ret = avcodec_encode_video2(m_videoRecCtx.codecCtx, &pkt, m_videoRecCtx.frame, &gotPacket);
	if(ret < 0){
		LOGE("avcodec_encode_video2 failed!\n");
		return -1;
	}
	if (gotPacket == 1){
		pkt.pts = av_rescale_q(m_videoRecCtx.samplesCount, m_videoRecCtx.codecCtx->time_base, m_videoRecCtx.stream->time_base);;
		pkt.dts = pkt.pts;//need fix
		pkt.duration = av_rescale_q(1, m_videoRecCtx.codecCtx->time_base, m_videoRecCtx.stream->time_base);
		pkt.pos = -1;
		pkt.stream_index = m_videoRecCtx.stream->index;
		m_videoRecCtx.samplesCount++;
#ifdef ENABLE_DEBUG
		log_packet(m_recOutFrameCtx, &pkt, "video");
#endif
		av_interleaved_write_frame(m_recOutFrameCtx, &pkt);
	}
	return 0;
}


static void flushVideo()
{
	int ret;
	int gotPacket;
	AVPacket pkt;
	if (!(m_videoRecCtx.codecCtx->codec->capabilities & CODEC_CAP_DELAY))
	{
		return;
	}
	while (1)
	{
		pkt.data = NULL;
		pkt.size = 0;
		av_init_packet(&pkt);
		if (avcodec_encode_video2(m_videoRecCtx.codecCtx, &pkt, NULL, &gotPacket) < 0)
		{
			break;
		}
		if (!gotPacket){
			break;
		}
		pkt.pts = av_rescale_q(m_videoRecCtx.samplesCount, m_videoRecCtx.codecCtx->time_base, m_videoRecCtx.stream->time_base);;
		pkt.dts = pkt.pts;//need fix
		pkt.duration = av_rescale_q(1, m_videoRecCtx.codecCtx->time_base, m_videoRecCtx.stream->time_base);
		pkt.pos = -1;
		pkt.stream_index = m_videoRecCtx.stream->index;
		m_videoRecCtx.samplesCount++;
		if (av_interleaved_write_frame(m_recOutFrameCtx, &pkt) < 0)
		{
			break;
		}
	}
	av_write_trailer(m_recOutFrameCtx);
}

static void closeVideo()
{
	avcodec_free_context(&m_videoRecCtx.codecCtx);
	av_frame_free(&m_videoRecCtx.frame);
	sws_freeContext(m_videoRecCtx.swsCtx);
}

JNIEXPORT jint JNICALL Java_com_dkmp_activity_BroadcastActivity_initial(JNIEnv *env, jobject obj,
		jstring ip, jstring sdpName, jboolean bTcp,
		jboolean enableAudio, jint audioBitRate, jboolean enableVideo, jint videoBitRate, jint videoWidth,
		jint videoHeight, jint videoGopSize, jint videoMaxBFrames)
{
	LOGD("begin init");
	int ret;
	const char* srv_ip = NULL;
	const char* sdp_name = NULL;
	srv_ip = (*env)->GetStringUTFChars(env, ip, 0);
	sdp_name = (*env)->GetStringUTFChars(env, sdpName, 0);
	char rtsp_path[1024];
	char rtp_path[1024];
	sprintf(rtsp_path,"rtsp://%s/%s", srv_ip, sdp_name);
	sprintf(rtp_path,"rtp://%s", srv_ip);
	if (srv_ip)
	{
		(*env)->ReleaseStringUTFChars(env, ip, srv_ip);
	}
	if (sdp_name)
	{
		(*env)->ReleaseStringUTFChars(env, sdpName, sdp_name);
	}
#ifdef ENABLE_DEBUG
	av_log_set_callback(custom_log);
#endif
	av_register_all();
	avformat_network_init();

	m_audioRecCtx.enable = enableAudio;
	m_videoRecCtx.enable = enableVideo;

	if(!m_audioRecCtx.enable&&!m_videoRecCtx.enable)
	{
		m_videoRecCtx.enable = 1;
	}

	if(avformat_alloc_output_context2(&m_recOutFrameCtx, NULL, "rtsp", rtsp_path)<0)
	{
		LOGE("avformat_alloc_output_context2 failed!\n");
	}

	if(m_audioRecCtx.enable)
	{
		if(initRecAudio(audioBitRate) < 0)
		{
			LOGE("init Audio failed!\n");
			return -1;
		}
		LOGD("init audio finished");
	}

    if(m_videoRecCtx.enable)
	{
    	if(initRecVideo(videoBitRate, videoWidth, videoHeight, videoGopSize, videoMaxBFrames) < 0)
		{
			LOGE("init Video failed!\n");
			return -1;
		}
		LOGD("init video finished");
	}

	char sdp[2048];
    av_sdp_create(&m_recOutFrameCtx,1, sdp, sizeof(sdp));
    LOGE("%s", sdp);

    ret = avio_open(&m_recOutFrameCtx->pb, rtp_path, AVIO_FLAG_WRITE);
    if (ret < 0) {
    	char errbuf[100];
		av_strerror(ret, errbuf, sizeof(errbuf));
		LOGE("Failed to open output file,%s\n", errbuf);
		return -1;
	}

    AVDictionary *param = 0;
    av_dict_set(&param, "rtpflags", "latm", 0);
    if(bTcp)
    {
    	av_dict_set(&param, "rtsp_transport", "tcp", 0);
    }
    else
    {
    	av_dict_set(&param, "rtsp_transport", "udp", 0);
    }
	if(avformat_write_header(m_recOutFrameCtx, &param) < 0)
	{
		LOGE("avformat_write_header failed!\n");
		return -1;
	}

    LOGD("end init");
	return 0;
}

JNIEXPORT jint JNICALL Java_com_dkmp_activity_BroadcastActivity_encodeAudio(JNIEnv *env, jobject obj, jbyteArray audioData)
{
	jbyte* in= (jbyte*)(*env)->GetByteArrayElements(env, audioData, 0);
	if(encodeAudio((const uint8_t**)(&in)) < 0)
	{
		LOGE("encodeAudio failed!\n");
	}
	(*env)->ReleaseByteArrayElements(env,audioData,in,0);
	return 0;
}

JNIEXPORT jint JNICALL Java_com_dkmp_activity_BroadcastActivity_encodeVideo(JNIEnv *env, jobject obj, jbyteArray videoData)
{
	jbyte* in= (jbyte*)(*env)->GetByteArrayElements(env, videoData, 0);
	if(encodeVideo(in) < 0)
	{
		LOGE("encodeVideo failed!\n");
	}
	(*env)->ReleaseByteArrayElements(env,videoData,in,0);
	return 0;
}

JNIEXPORT jint JNICALL Java_com_dkmp_activity_BroadcastActivity_closeAudio(JNIEnv *env, jobject obj)
{
	flushAudio();
	closeAudio();
	return 0;
}

JNIEXPORT jint JNICALL Java_com_dkmp_activity_BroadcastActivity_closeVideo(JNIEnv *env, jobject obj)
{
	flushVideo();
	closeVideo();
	return 0;
}

JNIEXPORT jint JNICALL Java_com_dkmp_activity_BroadcastActivity_close(JNIEnv *env, jobject obj)
{
	avio_close(m_recOutFrameCtx->pb);
	avformat_free_context(m_recOutFrameCtx);
	return 0;
}

AVFormatContext* m_plyInFormatCtx;
StreamContext m_audioPlyCtx = {0};
StreamContext m_videoPlyCtx = {0};

static int initPlyAudio(AVStream* stream)
{
	m_audioPlyCtx.enable = 1;
	m_audioPlyCtx.stream = stream;
	m_audioPlyCtx.codec = avcodec_find_decoder(m_audioPlyCtx.stream->codecpar->codec_id);
	if(m_audioPlyCtx.codec == NULL)
	{
		LOGE("audio avcodec_find_decoder fail.");
		return -1;
	}
	m_audioPlyCtx.codecCtx = avcodec_alloc_context3(m_audioPlyCtx.codec);
	avcodec_parameters_to_context(m_audioPlyCtx.codecCtx, m_audioPlyCtx.stream->codecpar);

	if(avcodec_open2(m_audioPlyCtx.codecCtx, m_audioPlyCtx.codec, NULL)<0)
	{
		LOGE("audio avcodec_open2 fail.");
		return -1;
	}

	m_audioPlyCtx.frame = av_frame_alloc();
	m_audioPlyCtx.frame->format = AV_SAMPLE_FMT_S16;
	m_audioPlyCtx.frame->channel_layout = 1;
	m_audioPlyCtx.frame->sample_rate = 44100;
	m_audioPlyCtx.frame->nb_samples = 1024;
	av_frame_get_buffer(m_audioPlyCtx.frame, 0);

	m_audioPlyCtx.swrCtx = swr_alloc();
	av_opt_set_int(m_audioPlyCtx.swrCtx, "in_channel_layout",  m_audioPlyCtx.codecCtx->channel_layout, 0);
	av_opt_set_int(m_audioPlyCtx.swrCtx, "out_channel_layout", 1,  0);
	av_opt_set_int(m_audioPlyCtx.swrCtx, "in_sample_rate",     m_audioPlyCtx.codecCtx->sample_rate, 0);
	av_opt_set_int(m_audioPlyCtx.swrCtx, "out_sample_rate",    44100, 0);
	av_opt_set_sample_fmt(m_audioPlyCtx.swrCtx, "in_sample_fmt", m_audioPlyCtx.codecCtx->sample_fmt , 0);
	av_opt_set_sample_fmt(m_audioPlyCtx.swrCtx, "out_sample_fmt", AV_SAMPLE_FMT_S16,  0);
	swr_init(m_audioPlyCtx.swrCtx);
	return 0;
}

static int initPlyVideo(AVStream* stream)
{
	m_videoPlyCtx.enable = 1;
	m_videoPlyCtx.stream = stream;
	m_videoPlyCtx.codec = avcodec_find_decoder(m_videoPlyCtx.stream->codecpar->codec_id);
	if(m_videoPlyCtx.codec == NULL)
	{
		LOGE("video avcodec_find_decoder fail.");
		return -1;
	}
	m_videoPlyCtx.codecCtx = avcodec_alloc_context3(m_videoPlyCtx.codec);
	avcodec_parameters_to_context(m_videoPlyCtx.codecCtx, m_videoPlyCtx.stream->codecpar);

	if(avcodec_open2(m_videoPlyCtx.codecCtx, m_videoPlyCtx.codec, NULL)<0)
	{
		LOGE("video avcodec_open2 fail.");
		return -1;
	}

	if(m_videoPlyCtx.codecCtx->pix_fmt==AV_PIX_FMT_NONE)
	{
		LOGE("video unkonw pix_fmt.");
		return -1;
	}

	m_videoPlyCtx.frame = av_frame_alloc();
	m_videoPlyCtx.data[0] = (uint8_t *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P,
			m_videoPlyCtx.codecCtx->width, m_videoPlyCtx.codecCtx->height, 1));
	av_image_fill_arrays( m_videoPlyCtx.frame->data, m_videoPlyCtx.frame->linesize,
			m_videoPlyCtx.data[0], AV_PIX_FMT_YUV420P, m_videoPlyCtx.codecCtx->width, m_videoPlyCtx.codecCtx->height, 1);

	m_videoPlyCtx.swsCtx = sws_getContext(m_videoPlyCtx.codecCtx->width, m_videoPlyCtx.codecCtx->height, m_videoPlyCtx.codecCtx->pix_fmt,
			m_videoPlyCtx.codecCtx->width, m_videoPlyCtx.codecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
	return 0;
}

int main(int argc, char** argv)
{
	LOGE("open %s", argv[1]);
#ifdef ENABLE_DEBUG
	av_log_set_callback(custom_log);
#endif

    SDL_Rect screenRect;
    SDL_Renderer* videoRenderer;
	SDL_Texture* pVideoTexture;
	SDL_Rect videoRect;
    SDL_Event event;

    int failRetryTimes = atoi(argv[3]);
    int failRetryInterval = atoi(argv[4]);
    if(failRetryTimes < 0)
    {
    	failRetryTimes = 0;
    }
    if(failRetryInterval < 0)
    {
    	failRetryTimes = 0;
    }

    int i;
    int ret=0,got_picture=1;
    LOGD("begin SDL_Init.");
	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
	{
		LOGE("SDL_Init fail.");
		Android_JNI_SendMessage(COMMAND_DKMP_INIT_ERROR,0);
		SDL_Quit();
		return -1;
	}
	LOGD("begin av_register_all.");
    av_register_all();
    LOGD("begin avformat_network_init.");
    avformat_network_init();

	m_plyInFormatCtx = avformat_alloc_context();

	AVDictionary *param = 0;
	av_dict_set(&param, "rtpflags", "latm", 0);

	if(atoi(argv[2]))
	{
		av_dict_set(&param, "rtsp_transport", "tcp", 0);
	}
	else
	{
		av_dict_set(&param, "rtsp_transport", "udp", 0);
	}
	av_dict_set(&param, "stimeout", "5000000", 0);
	ret = 1;
	for(i=0;i<failRetryTimes+1;i++)
    {
		LOGD("begin avformat_open_input.");
		if(avformat_open_input(&m_plyInFormatCtx, argv[1], NULL, &param) == 0)
		{
			ret = 0;
			break;
		}
		SDL_PeepEvents(&event,1,SDL_GETEVENT,SDL_FIRSTEVENT,SDL_LASTEVENT);
		switch( event.type ) {
			case SDL_QUIT:
				LOGD("quit.");
				return -1;
			default:
				break;
		}
		LOGE("open %s fail, and will try again.", argv[1]);
		av_usleep(failRetryInterval*1000);
    }

	if(ret)
	{
		LOGE("open %s fail.", argv[1]);
		Android_JNI_SendMessage(COMMAND_DKMP_INIT_ERROR,0);
		SDL_Quit();
		return -1;
	}

    if(avformat_find_stream_info(m_plyInFormatCtx, NULL) < 0)
    {
    	LOGE("avformat_find_stream_info fail.");
		Android_JNI_SendMessage(COMMAND_DKMP_INIT_ERROR,0);
    	SDL_Quit();
        return -1;
    }
    m_audioPlyCtx.enable = 0;
    m_videoPlyCtx.enable = 0;
    for(i=0; i<m_plyInFormatCtx->nb_streams; i++)
    {
        if(m_plyInFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
        	if(initPlyVideo(m_plyInFormatCtx->streams[i]) < 0)
			{
				LOGE("initPlyVideo fail.");
				Android_JNI_SendMessage(COMMAND_DKMP_INIT_ERROR,0);
				SDL_Quit();
				return -1;
			}
        	int tw=0,th=0;
			int modes = SDL_GetNumDisplayModes(0);
			for (i = 0; i < modes; i++)
			{
				SDL_DisplayMode mode;
				SDL_GetDisplayMode(0, i, &mode);
				if (mode.w > tw)
				{
					tw = mode.w;
					th = mode.h;
				}
			}
			screenRect.w = tw;
			screenRect.h = (int)(1.0*tw/m_videoPlyCtx.codecCtx->width*m_videoPlyCtx.codecCtx->height);
			screenRect.x = 0;
			screenRect.y = (th - screenRect.h)/2;

			SDL_Window *screen = SDL_CreateWindow("rtsp", SDL_WINDOWPOS_UNDEFINED,
			    		SDL_WINDOWPOS_UNDEFINED,screenRect.w, screenRect.h,SDL_WINDOW_OPENGL);
			if(!screen)
			{
				LOGE("SDL_CreateWindow fail.");
				Android_JNI_SendMessage(COMMAND_DKMP_INIT_ERROR,0);
				SDL_Quit();
				return -1;
			}
			videoRenderer = SDL_CreateRenderer(screen, -1, 0);
			pVideoTexture = SDL_CreateTexture(videoRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,
		    		m_videoPlyCtx.codecCtx->width, m_videoPlyCtx.codecCtx->height);
        }
        else if(m_plyInFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            if(initPlyAudio(m_plyInFormatCtx->streams[i]) < 0)
            {
            	LOGE("initPlyAudio fail.");
        		Android_JNI_SendMessage(COMMAND_DKMP_INIT_ERROR,0);
        		SDL_Quit();
        		return -1;
            }
            SDL_AudioSpec wanted_spec;
			wanted_spec.freq = 44100;
			wanted_spec.format = AUDIO_S16SYS;
			wanted_spec.channels = 1;
			wanted_spec.samples = 1024;
			wanted_spec.callback = NULL;

			if (SDL_OpenAudio(&wanted_spec, NULL)<0){
				LOGE("can't open audio.\n");
				Android_JNI_SendMessage(COMMAND_DKMP_INIT_ERROR,0);
				SDL_Quit();
				return -1;
			}

			SDL_PauseAudio(0);
        }
    }

    LOGD("begin.");

    AVFrame* pFrameSource;
    pFrameSource = av_frame_alloc();
    AVPacket pkt;
    av_init_packet(&pkt);
	pkt.data = NULL;
	pkt.size = 0;

	Android_JNI_SendMessage(COMMAND_DKMP_LOAD_FINISH,0);

	videoRect.x = 0;
	videoRect.y = 0;
	videoRect.w = m_videoPlyCtx.codecCtx->width;
	videoRect.h = m_videoPlyCtx.codecCtx->height;
    while((ret=av_read_frame(m_plyInFormatCtx, &pkt))>=0)
    {
		if(m_videoPlyCtx.enable && pkt.stream_index==m_videoPlyCtx.stream->index)
		{
			ret = avcodec_send_packet(m_videoPlyCtx.codecCtx, &pkt);
			if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
				break;
			ret = avcodec_receive_frame(m_videoPlyCtx.codecCtx, pFrameSource);
			if (ret >= 0)
			{
				sws_scale(m_videoPlyCtx.swsCtx, (const uint8_t* const*)pFrameSource->data, pFrameSource->linesize, 0,
						m_videoPlyCtx.codecCtx->height, m_videoPlyCtx.frame->data, m_videoPlyCtx.frame->linesize);

				SDL_UpdateTexture(pVideoTexture, &videoRect, m_videoPlyCtx.frame->data[0], m_videoPlyCtx.frame->linesize[0]);
				SDL_RenderClear(videoRenderer);
				SDL_RenderCopy(videoRenderer, pVideoTexture, &videoRect, &screenRect);

				SDL_RenderPresent(videoRenderer);
			}
		}
		else if(m_audioPlyCtx.enable && pkt.stream_index == m_audioPlyCtx.stream->index)
		{
			ret = avcodec_send_packet(m_audioPlyCtx.codecCtx, &pkt);
			if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
				break;
			ret = avcodec_receive_frame(m_audioPlyCtx.codecCtx, pFrameSource);
			if (ret >= 0)
			{
				swr_convert(m_audioPlyCtx.swrCtx, m_audioPlyCtx.frame->data, m_audioPlyCtx.frame->nb_samples,
						(const uint8_t**)pFrameSource->data, pFrameSource->nb_samples);
				SDL_QueueAudio(1, m_audioPlyCtx.frame->data[0],
						m_audioPlyCtx.frame->nb_samples*av_get_bytes_per_sample(m_audioPlyCtx.frame->format));
			}
		}
		SDL_PeepEvents(&event,1,SDL_GETEVENT,SDL_FIRSTEVENT,SDL_LASTEVENT);
		switch( event.type ) {
			case SDL_QUIT:
				LOGD("quit.");
				goto label_destory;
			case SDL_WINDOWEVENT:
				if(event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
				{
					if(screenRect.w == event.window.data1||screenRect.h == event.window.data2)
					{
						break;
					}
					LOGD("w:%d,h:%d",event.window.data1,event.window.data2);

					screenRect.w = event.window.data1;
					screenRect.h = (int)(1.0*event.window.data1/m_videoPlyCtx.codecCtx->width*m_videoPlyCtx.codecCtx->height);
					if(screenRect.h>event.window.data2)
					{
						screenRect.h = event.window.data2;
						screenRect.w = (int)(1.0*event.window.data2/m_videoPlyCtx.codecCtx->height*m_videoPlyCtx.codecCtx->width);
						screenRect.x = (event.window.data1 - screenRect.w)/2;
						screenRect.y = 0;
					}
					else
					{
						screenRect.x = 0;
						screenRect.y = (event.window.data2 - screenRect.h)/2;
					}
				}
				break;
			default:
				break;
		}
    }
    LOGD("av_read_frame ret:%d",ret);

label_destory:
	SDL_Quit();

	if(m_videoPlyCtx.enable)
	{
	    SDL_DestroyTexture(pVideoTexture);

		av_frame_free(&m_videoPlyCtx.frame);
		avcodec_free_context(&m_videoPlyCtx.codecCtx);
		sws_freeContext(m_videoPlyCtx.swsCtx);
		av_free(m_videoPlyCtx.data[0]);
	}
	if(m_audioPlyCtx.enable)
	{
		SDL_CloseAudio();
		av_frame_free(&m_audioPlyCtx.frame);
		avcodec_free_context(&m_audioPlyCtx.codecCtx);
		swr_free(&m_audioPlyCtx.swrCtx);
	}

	av_frame_free(&pFrameSource);
	avio_close(m_plyInFormatCtx->pb);
	avformat_close_input(&m_plyInFormatCtx);
	LOGD("sdl video return.");
	return 0;
}

