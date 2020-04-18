/*
 * This file is part of Chiaki.
 *
 * Chiaki is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Chiaki is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Chiaki.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <videodecoder.h>

#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>

#include <QImage>

#define HWDEVICE_NAME "vaapi"

VideoDecoder::VideoDecoder(bool hw_decode, ChiakiLog *log) : hw_decode(hw_decode), log(log)
{
	enum AVHWDeviceType type;
	hw_device_ctx = nullptr;
	cc = nullptr;

	#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 10, 100)
	avcodec_register_all();
	#endif
	codec = avcodec_find_decoder(AV_CODEC_ID_H264);
	if(!codec)
		throw VideoDecoderException("H264 Codec not available");

	codec_context = avcodec_alloc_context3(codec);
	if(!codec_context)
		throw VideoDecoderException("Failed to alloc codec context");

	if(hw_decode)
	{
		type = av_hwdevice_find_type_by_name(HWDEVICE_NAME);
		if (type == AV_HWDEVICE_TYPE_NONE) {
			throw VideoDecoderException("Can't initialize vaapi");
		}

		for(int i = 0;; i++) {
			const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
			if(!config) {
				throw VideoDecoderException("avcodec_get_hw_config failed");
			}
			if(config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
				config->device_type == type) {
				hw_pix_fmt = config->pix_fmt;
				break;
			}
		}

		if(av_hwdevice_ctx_create(&hw_device_ctx, type, NULL, NULL, 0) < 0)
			throw VideoDecoderException("Failed to create hwdevice context");
		codec_context->hw_device_ctx = av_buffer_ref(hw_device_ctx);
	}

	if(avcodec_open2(codec_context, codec, nullptr) < 0)
	{
		avcodec_free_context(&codec_context);
		throw VideoDecoderException("Failed to open codec context");
	}
}

VideoDecoder::~VideoDecoder()
{
	avcodec_close(codec_context);
	avcodec_free_context(&codec_context);
	if(hw_device_ctx)
	{
		av_buffer_unref(&hw_device_ctx);
	}
}

void VideoDecoder::PushFrame(uint8_t *buf, size_t buf_size)
{
	{
		QMutexLocker locker(&mutex);

		AVPacket packet;
		av_init_packet(&packet);
		packet.data = buf;
		packet.size = buf_size;
		int r;
send_packet:
		r = avcodec_send_packet(codec_context, &packet);
		if(r != 0)
		{
			if(r == AVERROR(EAGAIN))
			{
				CHIAKI_LOGE(log, "AVCodec internal buffer is full removing frames before pushing");
				AVFrame *frame = av_frame_alloc();
				if(!frame)
				{
					CHIAKI_LOGE(log, "Failed to alloc AVFrame");
					return;
				}
				r = avcodec_receive_frame(codec_context, frame);
				av_frame_free(&frame);
				if(r != 0)
				{
					CHIAKI_LOGE(log, "Failed to pull frame");
					return;
				}
				goto send_packet;
			}
			else
			{
				char errbuf[128];
				av_make_error_string(errbuf, sizeof(errbuf), r);
				CHIAKI_LOGE(log, "Failed to push frame: %s", errbuf);
				return;
			}
		}
	}

	emit FramesAvailable();
}

AVFrame *VideoDecoder::PullFrame()
{
	QMutexLocker locker(&mutex);

	// always try to pull as much as possible and return only the very last frame
	AVFrame *frame_last = nullptr;
	AVFrame *sw_frame = nullptr;
	AVFrame *frame = nullptr; 
	while(true)
	{
		AVFrame *next_frame;
		if(frame_last)
		{
			av_frame_unref(frame_last);
			next_frame = frame_last;
		}
		else
		{
			next_frame = av_frame_alloc();
			if(!next_frame)
				return frame;
		}
		frame_last = frame;
		frame = next_frame;
		int r = avcodec_receive_frame(codec_context, frame);
		if(r == 0)
		{
			frame = hw_decode ? GetFromHardware(frame) : frame;
		}
		else
		{
			if(r != AVERROR(EAGAIN))
				CHIAKI_LOGE(log, "Decoding with FFMPEG failed");
			av_frame_free(&frame);
			return frame_last;
		}
	}
}

AVFrame *VideoDecoder::GetFromHardware(AVFrame *hw_frame)
{
	/*
	(1) gets frame from hardware buffer
	(2) converts frame from NV12 color space to YUV420P
	*/
	AVFrame *frame;
	AVFrame *sw_frame;

	sw_frame = av_frame_alloc();

	int ret = av_hwframe_transfer_data(sw_frame, hw_frame, 0);

	if(ret < 0)
	{
		CHIAKI_LOGE(log, "Failed to transfer frame from hardware");
	}

	av_frame_unref(hw_frame);

	if(sw_frame->width <= 0)
	{
		av_frame_unref(sw_frame);
		return nullptr;
	}

	frame = av_frame_alloc();
	frame->format = AV_PIX_FMT_YUV420P;
	frame->width = sw_frame->width;
	frame->height = sw_frame->height;
	av_frame_get_buffer(frame, 32);

	if(cc == nullptr)
	{
		cc = sws_getContext(sw_frame->width, sw_frame->height, AV_PIX_FMT_NV12, sw_frame->width, sw_frame->height, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
	}

	sws_scale(cc, sw_frame->data, sw_frame->linesize, 0, sw_frame->height, frame->data, frame->linesize);

	av_frame_unref(sw_frame);
	return frame;
}
