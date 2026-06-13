//
// GTUltra optional MP4 video sync window
//

#define GVIDEO_C

#include "goattrk2.h"

#ifdef GTULTRA_VIDEO

#include <stdint.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>

extern int bypassPlayRoutine;

typedef struct
{
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *texture;
	Uint32 windowID;

	AVFormatContext *format;
	AVCodecContext *codec;
	AVPacket *packet;
	AVFrame *frame;
	struct SwsContext *sws;
	AVStream *stream;
	int streamIndex;

	uint8_t *pendingPixels;
	int pendingPitch;
	int width;
	int height;
	double streamStart;
	double pendingPts;
	double presentedPts;
	int hasPending;
	int hasPresented;
	int eof;
	int draining;
	int wasPlaying;
	char filename[MAX_PATHNAME];
} GTVIDEO_STATE;

static GTVIDEO_STATE video;

static int has_mp4_extension(const char *path)
{
	const char *dot;
	char ext[5];
	int i;

	if (!path) return 0;
	dot = strrchr(path, '.');
	if (!dot) return 0;
	if (strlen(dot) != 4) return 0;
	for (i = 0; i < 4; i++)
		ext[i] = (char)tolower((unsigned char)dot[i]);
	ext[4] = '\0';
	return !strcmp(ext, ".mp4") || !strcmp(ext, ".m4v");
}

int gt_video_enabled(void)
{
	return 1;
}

int gt_video_is_video_path(const char *path)
{
	return has_mp4_extension(path);
}

int gt_video_is_loaded(void)
{
	return video.format != NULL;
}

static double gt_video_clock_seconds(GTOBJECT *gt)
{
	double fps;

	if (!gt) return 0.0;
	if (editorInfo.ntsc)
		fps = editorInfo.multiplier ? NTSCFRAMERATE * editorInfo.multiplier : NTSCFRAMERATE / 2.0;
	else
		fps = editorInfo.multiplier ? PALFRAMERATE * editorInfo.multiplier : PALFRAMERATE / 2.0;

	if (fps <= 0.0) fps = 1.0;
	return (double)gt->timemin * 60.0 + (double)gt->timesec + (double)gt->timeframe / fps;
}

static double frame_pts_seconds(const AVFrame *frame)
{
	int64_t pts = frame->best_effort_timestamp;
	double seconds;

	if (pts == AV_NOPTS_VALUE)
		pts = frame->pts;
	if (pts == AV_NOPTS_VALUE)
		return video.presentedPts >= 0.0 ? video.presentedPts : 0.0;

	seconds = (double)pts * av_q2d(video.stream->time_base) - video.streamStart;
	if (seconds < 0.0) seconds = 0.0;
	return seconds;
}

static void render_video(void)
{
	int winW;
	int winH;
	SDL_Rect dst;
	double srcAspect;
	double winAspect;

	if (!video.renderer || !video.texture || !video.hasPresented)
		return;

	SDL_GetWindowSize(video.window, &winW, &winH);
	if (winW <= 0 || winH <= 0)
		return;

	srcAspect = (double)video.width / (double)video.height;
	winAspect = (double)winW / (double)winH;

	if (winAspect > srcAspect)
	{
		dst.h = winH;
		dst.w = (int)(winH * srcAspect);
		dst.x = (winW - dst.w) / 2;
		dst.y = 0;
	}
	else
	{
		dst.w = winW;
		dst.h = (int)(winW / srcAspect);
		dst.x = 0;
		dst.y = (winH - dst.h) / 2;
	}

	SDL_SetRenderDrawColor(video.renderer, 0, 0, 0, 255);
	SDL_RenderClear(video.renderer);
	SDL_RenderCopy(video.renderer, video.texture, NULL, &dst);
	SDL_RenderPresent(video.renderer);
}

static void promote_pending_frame(void)
{
	if (!video.hasPending || !video.texture)
		return;

	SDL_UpdateTexture(video.texture, NULL, video.pendingPixels, video.pendingPitch);
	video.presentedPts = video.pendingPts;
	video.hasPresented = 1;
	video.hasPending = 0;
	render_video();
}

static void clear_decode_state(void)
{
	video.hasPending = 0;
	video.eof = 0;
	video.draining = 0;
	avcodec_flush_buffers(video.codec);
}

static void initial_window_size(int *width, int *height)
{
	const int maxWidth = 1280;
	const int maxHeight = 720;
	double scale;

	*width = video.width;
	*height = video.height;
	if (*width <= maxWidth && *height <= maxHeight)
		return;

	scale = (double)maxWidth / (double)*width;
	if ((double)maxHeight / (double)*height < scale)
		scale = (double)maxHeight / (double)*height;

	*width = (int)((double)*width * scale);
	*height = (int)((double)*height * scale);
	if (*width < 1) *width = 1;
	if (*height < 1) *height = 1;
}

static int seek_video(double seconds)
{
	int64_t timestamp;
	double seekSeconds;
	int ret;

	if (!video.format || !video.stream)
		return 0;

	if (seconds < 0.0) seconds = 0.0;
	seekSeconds = seconds + video.streamStart;
	timestamp = (int64_t)(seekSeconds / av_q2d(video.stream->time_base));
	ret = av_seek_frame(video.format, video.streamIndex, timestamp, AVSEEK_FLAG_BACKWARD);
	if (ret < 0)
		ret = av_seek_frame(video.format, video.streamIndex, 0, AVSEEK_FLAG_BACKWARD);

	clear_decode_state();
	video.presentedPts = -1.0;
	video.hasPresented = 0;
	return ret >= 0;
}

static int decode_one_frame(void)
{
	int ret;

	if (!video.format || !video.codec || !video.packet || !video.frame || video.eof)
		return 0;

	for (;;)
	{
		ret = avcodec_receive_frame(video.codec, video.frame);
		if (ret == 0)
		{
			uint8_t *dstData[4] = { video.pendingPixels, NULL, NULL, NULL };
			int dstLinesize[4] = { video.pendingPitch, 0, 0, 0 };

			sws_scale(video.sws,
				(const uint8_t * const *)video.frame->data,
				video.frame->linesize,
				0,
				video.height,
				dstData,
				dstLinesize);
			video.pendingPts = frame_pts_seconds(video.frame);
			video.hasPending = 1;
			av_frame_unref(video.frame);
			return 1;
		}
		if (ret == AVERROR_EOF)
		{
			video.eof = 1;
			return 0;
		}
		if (ret != AVERROR(EAGAIN))
		{
			video.eof = 1;
			return 0;
		}

		if (video.draining)
			continue;

		ret = av_read_frame(video.format, video.packet);
		if (ret < 0)
		{
			if (avcodec_send_packet(video.codec, NULL) < 0)
			{
				video.eof = 1;
				return 0;
			}
			video.draining = 1;
			continue;
		}

		if (video.packet->stream_index == video.streamIndex &&
			avcodec_send_packet(video.codec, video.packet) < 0)
		{
			av_packet_unref(video.packet);
			video.eof = 1;
			return 0;
		}
		av_packet_unref(video.packet);
	}
}

static void decode_to_time(double seconds)
{
	for (;;)
	{
		if (video.hasPending)
		{
			if (video.pendingPts > seconds)
				return;
			promote_pending_frame();
			continue;
		}

		if (!decode_one_frame())
			return;
	}
}

void gt_video_tick(GTOBJECT *gt)
{
	double seconds;
	int playing;

	if (!gt_video_is_loaded())
		return;

	playing = gt && gt->songinit == PLAY_PLAYING && !bypassPlayRoutine;
	if (!playing)
	{
		video.wasPlaying = 0;
		return;
	}

	seconds = gt_video_clock_seconds(gt);
	if (!video.wasPlaying ||
		!video.hasPresented ||
		seconds + 0.05 < video.presentedPts ||
		seconds - video.presentedPts > 1.0)
	{
		seek_video(seconds);
	}

	decode_to_time(seconds);
	video.wasPlaying = 1;
}

void gt_video_close(void)
{
	if (video.texture)
	{
		SDL_DestroyTexture(video.texture);
		video.texture = NULL;
	}
	if (video.renderer)
	{
		SDL_DestroyRenderer(video.renderer);
		video.renderer = NULL;
	}
	if (video.window)
	{
		SDL_DestroyWindow(video.window);
		video.window = NULL;
	}
	if (video.sws)
	{
		sws_freeContext(video.sws);
		video.sws = NULL;
	}
	if (video.pendingPixels)
	{
		av_free(video.pendingPixels);
		video.pendingPixels = NULL;
	}
	if (video.frame)
	{
		av_frame_free(&video.frame);
		video.frame = NULL;
	}
	if (video.packet)
	{
		av_packet_free(&video.packet);
		video.packet = NULL;
	}
	if (video.codec)
	{
		avcodec_free_context(&video.codec);
		video.codec = NULL;
	}
	if (video.format)
	{
		avformat_close_input(&video.format);
		video.format = NULL;
	}

	memset(&video, 0, sizeof(video));
	video.presentedPts = -1.0;
}

int gt_video_load(const char *path)
{
	const AVCodec *decoder;
	AVCodecParameters *params;
	int windowW;
	int windowH;
	int ret;

	if (!has_mp4_extension(path))
		return 0;

	gt_video_close();
	video.presentedPts = -1.0;

	ret = avformat_open_input(&video.format, path, NULL, NULL);
	if (ret < 0)
		goto fail;

	ret = avformat_find_stream_info(video.format, NULL);
	if (ret < 0)
		goto fail;

	video.streamIndex = av_find_best_stream(video.format, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (video.streamIndex < 0)
		goto fail;

	video.stream = video.format->streams[video.streamIndex];
	params = video.stream->codecpar;
	decoder = avcodec_find_decoder(params->codec_id);
	if (!decoder)
		goto fail;

	video.codec = avcodec_alloc_context3(decoder);
	if (!video.codec)
		goto fail;
	if (avcodec_parameters_to_context(video.codec, params) < 0)
		goto fail;
	video.codec->pkt_timebase = video.stream->time_base;
	if (avcodec_open2(video.codec, decoder, NULL) < 0)
		goto fail;

	video.width = video.codec->width;
	video.height = video.codec->height;
	if (video.width <= 0 || video.height <= 0)
		goto fail;
	if (video.width > 8192 || video.height > 8192)
		goto fail;

	video.packet = av_packet_alloc();
	video.frame = av_frame_alloc();
	if (!video.packet || !video.frame)
		goto fail;

	video.pendingPitch = video.width * 4;
	video.pendingPixels = av_malloc((size_t)video.pendingPitch * (size_t)video.height);
	if (!video.pendingPixels)
		goto fail;

	video.sws = sws_getContext(video.width,
		video.height,
		video.codec->pix_fmt,
		video.width,
		video.height,
		AV_PIX_FMT_RGBA,
		SWS_BILINEAR,
		NULL,
		NULL,
		NULL);
	if (!video.sws)
		goto fail;

	initial_window_size(&windowW, &windowH);
	video.window = SDL_CreateWindow("GTUltraPro Video",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		windowW,
		windowH,
		SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
	if (!video.window)
		goto fail;

	video.windowID = SDL_GetWindowID(video.window);
	video.renderer = SDL_CreateRenderer(video.window, -1, SDL_RENDERER_ACCELERATED);
	if (!video.renderer)
		video.renderer = SDL_CreateRenderer(video.window, -1, SDL_RENDERER_SOFTWARE);
	if (!video.renderer)
		goto fail;

	video.texture = SDL_CreateTexture(video.renderer,
		SDL_PIXELFORMAT_RGBA32,
		SDL_TEXTUREACCESS_STREAMING,
		video.width,
		video.height);
	if (!video.texture)
		goto fail;

	if (video.stream->start_time != AV_NOPTS_VALUE)
		video.streamStart = (double)video.stream->start_time * av_q2d(video.stream->time_base);
	else
		video.streamStart = 0.0;

	strncpy(video.filename, path, sizeof(video.filename) - 1);
	seek_video(0.0);
	decode_to_time(0.0);
	render_video();
	return 1;

fail:
	gt_video_close();
	return 0;
}

int gt_video_handle_sdl_event(const SDL_Event *event)
{
	if (!event)
		return 0;

	if (event->type == SDL_DROPFILE)
	{
		if (has_mp4_extension(event->drop.file))
		{
			gt_video_load(event->drop.file);
			SDL_free(event->drop.file);
			return 1;
		}
		return 0;
	}

	if (!video.window || event->type != SDL_WINDOWEVENT || event->window.windowID != video.windowID)
		return 0;

	switch (event->window.event)
	{
	case SDL_WINDOWEVENT_CLOSE:
		gt_video_close();
		return 1;
	case SDL_WINDOWEVENT_EXPOSED:
	case SDL_WINDOWEVENT_SIZE_CHANGED:
	case SDL_WINDOWEVENT_RESIZED:
		render_video();
		return 1;
	default:
		return 1;
	}
}

#endif
