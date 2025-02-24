// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/filters/ffmpeg_demuxer.h"

#include <algorithm>
#include <memory>
#include <set>
#include <utility>

#include "base/base64.h"
#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/sparse_histogram.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/sys_byteorder.h"
#include "base/task_runner_util.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "media/audio/sample_rates.h"
#include "media/base/bind_to_current_loop.h"
#include "media/base/decrypt_config.h"
#include "media/base/limits.h"
#include "media/base/media_log.h"
#include "media/base/media_tracks.h"
#include "media/base/timestamp_constants.h"
#include "media/base/video_codecs.h"
#include "media/ffmpeg/ffmpeg_common.h"
#include "media/filters/ffmpeg_aac_bitstream_converter.h"
#include "media/filters/ffmpeg_bitstream_converter.h"
#include "media/filters/ffmpeg_glue.h"
#include "media/filters/ffmpeg_h264_to_annex_b_bitstream_converter.h"
#include "media/filters/webvtt_util.h"
#include "media/formats/webm/webm_crypto_helpers.h"
#include "media/media_features.h"

#if BUILDFLAG(ENABLE_HEVC_DEMUXING)
#include "media/filters/ffmpeg_h265_to_annex_b_bitstream_converter.h"
#endif

namespace media {

static base::Time ExtractTimelineOffset(AVFormatContext* format_context) {
  if (strstr(format_context->iformat->name, "webm") ||
      strstr(format_context->iformat->name, "matroska")) {
    const AVDictionaryEntry* entry =
        av_dict_get(format_context->metadata, "creation_time", NULL, 0);

    base::Time timeline_offset;

    // FFmpegDemuxerTests assume base::Time::FromUTCString() is used here.
    if (entry != NULL && entry->value != NULL &&
        base::Time::FromUTCString(entry->value, &timeline_offset)) {
      return timeline_offset;
    }
  }

  return base::Time();
}

static base::TimeDelta FramesToTimeDelta(int frames, double sample_rate) {
  return base::TimeDelta::FromMicroseconds(
      frames * base::Time::kMicrosecondsPerSecond / sample_rate);
}

static base::TimeDelta ExtractStartTime(AVStream* stream,
                                        base::TimeDelta start_time_estimate) {
  DCHECK(start_time_estimate != kNoTimestamp);
  if (stream->start_time == static_cast<int64_t>(AV_NOPTS_VALUE)) {
    return start_time_estimate == kInfiniteDuration ? base::TimeDelta()
                                                    : start_time_estimate;
  }

  // First try the lower of the estimate and the |start_time| value.
  base::TimeDelta start_time =
      std::min(ConvertFromTimeBase(stream->time_base, stream->start_time),
               start_time_estimate);

  // Next see if the first buffered pts value is usable.
  if (stream->pts_buffer[0] != static_cast<int64_t>(AV_NOPTS_VALUE)) {
    const base::TimeDelta buffered_pts =
        ConvertFromTimeBase(stream->time_base, stream->pts_buffer[0]);
    if (buffered_pts < start_time)
      start_time = buffered_pts;
  }

  // NOTE: Do not use AVStream->first_dts since |start_time| should be a
  // presentation timestamp.
  return start_time;
}

// Some videos just want to watch the world burn, with a height of 0; cap the
// "infinite" aspect ratio resulting.
const int kInfiniteRatio = 99999;

// Common aspect ratios (multiplied by 100 and truncated) used for histogramming
// video sizes.  These were taken on 20111103 from
// http://wikipedia.org/wiki/Aspect_ratio_(image)#Previous_and_currently_used_aspect_ratios
const int kCommonAspectRatios100[] = {
    100, 115, 133, 137, 143, 150, 155, 160,  166,
    175, 177, 185, 200, 210, 220, 221, 235,  237,
    240, 255, 259, 266, 276, 293, 400, 1200, kInfiniteRatio,
};

template <class T>  // T has int width() & height() methods.
static void UmaHistogramAspectRatio(const char* name, const T& size) {
  UMA_HISTOGRAM_CUSTOM_ENUMERATION(
      name,
      // Intentionally use integer division to truncate the result.
      size.height() ? (size.width() * 100) / size.height() : kInfiniteRatio,
      base::CustomHistogram::ArrayToCustomRanges(
          kCommonAspectRatios100, arraysize(kCommonAspectRatios100)));
}

// Record detected track counts by type corresponding to a src= playback.
// Counts are split into 50 buckets, capped into [0,100] range.
static void RecordDetectedTrackTypeStats(int audio_count,
                                         int video_count,
                                         int text_count) {
  UMA_HISTOGRAM_COUNTS_100("Media.DetectedTrackCount.Audio", audio_count);
  UMA_HISTOGRAM_COUNTS_100("Media.DetectedTrackCount.Video", video_count);
  UMA_HISTOGRAM_COUNTS_100("Media.DetectedTrackCount.Text", text_count);
}

// Record audio decoder config UMA stats corresponding to a src= playback.
static void RecordAudioCodecStats(const AudioDecoderConfig& audio_config) {
  UMA_HISTOGRAM_ENUMERATION("Media.AudioCodec", audio_config.codec(),
                            kAudioCodecMax + 1);
  UMA_HISTOGRAM_ENUMERATION("Media.AudioSampleFormat",
                            audio_config.sample_format(), kSampleFormatMax + 1);
  UMA_HISTOGRAM_ENUMERATION("Media.AudioChannelLayout",
                            audio_config.channel_layout(),
                            CHANNEL_LAYOUT_MAX + 1);
  AudioSampleRate asr;
  if (ToAudioSampleRate(audio_config.samples_per_second(), &asr)) {
    UMA_HISTOGRAM_ENUMERATION("Media.AudioSamplesPerSecond", asr,
                              kAudioSampleRateMax + 1);
  } else {
    UMA_HISTOGRAM_COUNTS("Media.AudioSamplesPerSecondUnexpected",
                         audio_config.samples_per_second());
  }
}

// Record video decoder config UMA stats corresponding to a src= playback.
static void RecordVideoCodecStats(const VideoDecoderConfig& video_config,
                                  AVColorRange color_range,
                                  MediaLog* media_log) {
  media_log->RecordRapporWithSecurityOrigin("Media.OriginUrl.SRC.VideoCodec." +
                                            GetCodecName(video_config.codec()));

  UMA_HISTOGRAM_ENUMERATION("Media.VideoCodec", video_config.codec(),
                            kVideoCodecMax + 1);

  // Drop UNKNOWN because U_H_E() uses one bucket for all values less than 1.
  if (video_config.profile() >= 0) {
    UMA_HISTOGRAM_ENUMERATION("Media.VideoCodecProfile", video_config.profile(),
                              VIDEO_CODEC_PROFILE_MAX + 1);
  }
  UMA_HISTOGRAM_COUNTS_10000("Media.VideoVisibleWidth",
                             video_config.visible_rect().width());
  UmaHistogramAspectRatio("Media.VideoVisibleAspectRatio",
                          video_config.visible_rect());
  UMA_HISTOGRAM_ENUMERATION("Media.VideoPixelFormatUnion",
                            video_config.format(), PIXEL_FORMAT_MAX + 1);
  UMA_HISTOGRAM_ENUMERATION("Media.VideoFrameColorSpace",
                            video_config.color_space(), COLOR_SPACE_MAX + 1);

  // Note the PRESUBMIT_IGNORE_UMA_MAX below, this silences the PRESUBMIT.py
  // check for uma enum max usage, since we're abusing
  // UMA_HISTOGRAM_ENUMERATION to report a discrete value.
  UMA_HISTOGRAM_ENUMERATION("Media.VideoColorRange", color_range,
                            AVCOL_RANGE_NB);  // PRESUBMIT_IGNORE_UMA_MAX
}

static const char kCodecNone[] = "none";

static const char* GetCodecName(enum AVCodecID id) {
  const AVCodecDescriptor* codec_descriptor = avcodec_descriptor_get(id);
  // If the codec name can't be determined, return none for tracking.
  return codec_descriptor ? codec_descriptor->name : kCodecNone;
}

static void SetTimeProperty(MediaLogEvent* event,
                            const std::string& key,
                            base::TimeDelta value) {
  if (value == kInfiniteDuration)
    event->params.SetString(key, "kInfiniteDuration");
  else if (value == kNoTimestamp)
    event->params.SetString(key, "kNoTimestamp");
  else
    event->params.SetDouble(key, value.InSecondsF());
}

std::unique_ptr<FFmpegDemuxerStream> FFmpegDemuxerStream::Create(
    FFmpegDemuxer* demuxer,
    AVStream* stream,
    const scoped_refptr<MediaLog>& media_log) {
  if (!demuxer || !stream)
    return nullptr;

  std::unique_ptr<FFmpegDemuxerStream> demuxer_stream;
  std::unique_ptr<AudioDecoderConfig> audio_config;
  std::unique_ptr<VideoDecoderConfig> video_config;

  if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
    audio_config.reset(new AudioDecoderConfig());

    // IsValidConfig() checks that the codec is supported and that the channel
    // layout and sample format are valid.
    //
    // TODO(chcunningham): Change AVStreamToAudioDecoderConfig to check
    // IsValidConfig internally and return a null scoped_ptr if not valid.
    if (!AVStreamToAudioDecoderConfig(stream, audio_config.get()) ||
        !audio_config->IsValidConfig()) {
      MEDIA_LOG(ERROR, media_log)
          << "FFmpegDemuxer: failed creating audio stream";
      return nullptr;
    }

    MEDIA_LOG(INFO, media_log) << "FFmpegDemuxer: created audio stream, config "
                               << audio_config->AsHumanReadableString();
  } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
    video_config.reset(new VideoDecoderConfig());

    // IsValidConfig() checks that the codec is supported and that the channel
    // layout and sample format are valid.
    //
    // TODO(chcunningham): Change AVStreamToVideoDecoderConfig to check
    // IsValidConfig internally and return a null scoped_ptr if not valid.
    if (!AVStreamToVideoDecoderConfig(stream, video_config.get()) ||
        !video_config->IsValidConfig()) {
      MEDIA_LOG(ERROR, media_log)
          << "FFmpegDemuxer: failed creating video stream";
      return nullptr;
    }

    MEDIA_LOG(INFO, media_log) << "FFmpegDemuxer: created video stream, config "
                               << video_config->AsHumanReadableString();
  }

  return base::WrapUnique(new FFmpegDemuxerStream(
      demuxer, stream, std::move(audio_config), std::move(video_config)));
}

static void UnmarkEndOfStream(AVFormatContext* format_context) {
  format_context->pb->eof_reached = 0;
}

//
// FFmpegDemuxerStream
//
FFmpegDemuxerStream::FFmpegDemuxerStream(
    FFmpegDemuxer* demuxer,
    AVStream* stream,
    std::unique_ptr<AudioDecoderConfig> audio_config,
    std::unique_ptr<VideoDecoderConfig> video_config)
    : demuxer_(demuxer),
      task_runner_(base::ThreadTaskRunnerHandle::Get()),
      stream_(stream),
      start_time_(kNoTimestamp),
      audio_config_(audio_config.release()),
      video_config_(video_config.release()),
      type_(UNKNOWN),
      liveness_(LIVENESS_UNKNOWN),
      end_of_stream_(false),
      last_packet_timestamp_(kNoTimestamp),
      last_packet_duration_(kNoTimestamp),
      video_rotation_(VIDEO_ROTATION_0),
      is_enabled_(true),
      waiting_for_keyframe_(false),
      aborted_(false),
      fixup_negative_timestamps_(false) {
  DCHECK(demuxer_);

  bool is_encrypted = false;
  int rotation = 0;
  AVDictionaryEntry* rotation_entry = NULL;

  // Determine our media format.
  switch (stream->codecpar->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
      DCHECK(audio_config_.get() && !video_config_.get());
      type_ = AUDIO;
      is_encrypted = audio_config_->is_encrypted();
      break;
    case AVMEDIA_TYPE_VIDEO:
      DCHECK(video_config_.get() && !audio_config_.get());
      type_ = VIDEO;
      is_encrypted = video_config_->is_encrypted();

      rotation_entry = av_dict_get(stream->metadata, "rotate", NULL, 0);
      if (rotation_entry && rotation_entry->value && rotation_entry->value[0])
        base::StringToInt(rotation_entry->value, &rotation);

      switch (rotation) {
        case 0:
          break;
        case 90:
          video_rotation_ = VIDEO_ROTATION_90;
          break;
        case 180:
          video_rotation_ = VIDEO_ROTATION_180;
          break;
        case 270:
          video_rotation_ = VIDEO_ROTATION_270;
          break;
        default:
          LOG(ERROR) << "Unsupported video rotation metadata: " << rotation;
          break;
      }

      break;
    case AVMEDIA_TYPE_SUBTITLE:
      DCHECK(!video_config_.get() && !audio_config_.get());
      type_ = TEXT;
      break;
    default:
      NOTREACHED();
      break;
  }

  // Calculate the duration.
  duration_ = ConvertStreamTimestamp(stream->time_base, stream->duration);

  if (is_encrypted) {
    AVDictionaryEntry* key = av_dict_get(stream->metadata, "enc_key_id", NULL,
                                         0);
    DCHECK(key);
    DCHECK(key->value);
    if (!key || !key->value)
      return;
    base::StringPiece base64_key_id(key->value);
    std::string enc_key_id;
    base::Base64Decode(base64_key_id, &enc_key_id);
    DCHECK(!enc_key_id.empty());
    if (enc_key_id.empty())
      return;

    encryption_key_id_.assign(enc_key_id);
    demuxer_->OnEncryptedMediaInitData(EmeInitDataType::WEBM, enc_key_id);
  }
}

FFmpegDemuxerStream::~FFmpegDemuxerStream() {
  DCHECK(!demuxer_);
  DCHECK(read_cb_.is_null());
  DCHECK(buffer_queue_.IsEmpty());
}

void FFmpegDemuxerStream::EnqueuePacket(ScopedAVPacket packet) {
  DCHECK(task_runner_->BelongsToCurrentThread());

  if (!demuxer_ || end_of_stream_) {
    NOTREACHED() << "Attempted to enqueue packet on a stopped stream";
    return;
  }

  if (waiting_for_keyframe_) {
    if (packet.get()->flags & AV_PKT_FLAG_KEY)
      waiting_for_keyframe_ = false;
    else {
      DVLOG(3) << "Dropped non-keyframe pts=" << packet->pts;
      return;
    }
  }

#if defined(USE_PROPRIETARY_CODECS)
  // Convert the packet if there is a bitstream filter.
  if (packet->data && bitstream_converter_ &&
      !bitstream_converter_->ConvertPacket(packet.get())) {
    LOG(ERROR) << "Format conversion failed.";
  }
#endif

  // Get side data if any. For now, the only type of side_data is VP8 Alpha. We
  // keep this generic so that other side_data types in the future can be
  // handled the same way as well.
  av_packet_split_side_data(packet.get());

  scoped_refptr<DecoderBuffer> buffer;

  if (type() == DemuxerStream::TEXT) {
    int id_size = 0;
    uint8_t* id_data = av_packet_get_side_data(
        packet.get(), AV_PKT_DATA_WEBVTT_IDENTIFIER, &id_size);

    int settings_size = 0;
    uint8_t* settings_data = av_packet_get_side_data(
        packet.get(), AV_PKT_DATA_WEBVTT_SETTINGS, &settings_size);

    std::vector<uint8_t> side_data;
    MakeSideData(id_data, id_data + id_size,
                 settings_data, settings_data + settings_size,
                 &side_data);

    buffer = DecoderBuffer::CopyFrom(packet.get()->data, packet.get()->size,
                                     side_data.data(), side_data.size());
  } else {
    int side_data_size = 0;
    uint8_t* side_data = av_packet_get_side_data(
        packet.get(), AV_PKT_DATA_MATROSKA_BLOCKADDITIONAL, &side_data_size);

    std::unique_ptr<DecryptConfig> decrypt_config;
    int data_offset = 0;
    if ((type() == DemuxerStream::AUDIO && audio_config_->is_encrypted()) ||
        (type() == DemuxerStream::VIDEO && video_config_->is_encrypted())) {
      if (!WebMCreateDecryptConfig(
              packet->data, packet->size,
              reinterpret_cast<const uint8_t*>(encryption_key_id_.data()),
              encryption_key_id_.size(), &decrypt_config, &data_offset)) {
        LOG(ERROR) << "Creation of DecryptConfig failed.";
      }
    }

    // If a packet is returned by FFmpeg's av_parser_parse2() the packet will
    // reference inner memory of FFmpeg.  As such we should transfer the packet
    // into memory we control.
    if (side_data_size > 0) {
      buffer = DecoderBuffer::CopyFrom(packet.get()->data + data_offset,
                                       packet.get()->size - data_offset,
                                       side_data, side_data_size);
    } else {
      buffer = DecoderBuffer::CopyFrom(packet.get()->data + data_offset,
                                       packet.get()->size - data_offset);
    }

    int skip_samples_size = 0;
    const uint32_t* skip_samples_ptr =
        reinterpret_cast<const uint32_t*>(av_packet_get_side_data(
            packet.get(), AV_PKT_DATA_SKIP_SAMPLES, &skip_samples_size));
    const int kSkipSamplesValidSize = 10;
    const int kSkipEndSamplesOffset = 1;
    if (skip_samples_size >= kSkipSamplesValidSize) {
      // Because FFmpeg rolls codec delay and skip samples into one we can only
      // allow front discard padding on the first buffer.  Otherwise the discard
      // helper can't figure out which data to discard.  See AudioDiscardHelper.
      int discard_front_samples = base::ByteSwapToLE32(*skip_samples_ptr);
      if (last_packet_timestamp_ != kNoTimestamp && discard_front_samples) {
        DLOG(ERROR) << "Skip samples are only allowed for the first packet.";
        discard_front_samples = 0;
      }

      const int discard_end_samples =
          base::ByteSwapToLE32(*(skip_samples_ptr + kSkipEndSamplesOffset));
      const int samples_per_second =
          audio_decoder_config().samples_per_second();
      buffer->set_discard_padding(std::make_pair(
          FramesToTimeDelta(discard_front_samples, samples_per_second),
          FramesToTimeDelta(discard_end_samples, samples_per_second)));
    }

    if (decrypt_config)
      buffer->set_decrypt_config(std::move(decrypt_config));
  }

  if (packet->duration >= 0) {
    buffer->set_duration(
        ConvertStreamTimestamp(stream_->time_base, packet->duration));
  } else {
    // TODO(wolenetz): Remove when FFmpeg stops returning negative durations.
    // https://crbug.com/394418
    DVLOG(1) << "FFmpeg returned a buffer with a negative duration! "
             << packet->duration;
    buffer->set_duration(kNoTimestamp);
  }

  // Note: If pts is AV_NOPTS_VALUE, stream_timestamp will be kNoTimestamp.
  const base::TimeDelta stream_timestamp =
      ConvertStreamTimestamp(stream_->time_base, packet->pts);

  if (stream_timestamp == kNoTimestamp) {
    demuxer_->NotifyDemuxerError(DEMUXER_ERROR_COULD_NOT_PARSE);
    return;
  }

  const bool is_audio = type() == AUDIO;

  // If this file has negative timestamps don't rebase any other stream types
  // against the negative starting time.
  base::TimeDelta start_time = demuxer_->start_time();
  if (fixup_negative_timestamps_ && !is_audio &&
      start_time < base::TimeDelta()) {
    start_time = base::TimeDelta();
  }

  // Don't rebase timestamps for positive start times, the HTML Media Spec
  // details this in section "4.8.10.6 Offsets into the media resource." We
  // will still need to rebase timestamps before seeking with FFmpeg though.
  if (start_time > base::TimeDelta())
    start_time = base::TimeDelta();

  buffer->set_timestamp(stream_timestamp - start_time);

  // If enabled, and no codec delay is present, mark audio packets with
  // negative timestamps for post-decode discard.
  if (fixup_negative_timestamps_ && is_audio &&
      stream_timestamp < base::TimeDelta() &&
      buffer->duration() != kNoTimestamp) {
    if (!audio_decoder_config().codec_delay()) {
      DCHECK_EQ(buffer->discard_padding().first, base::TimeDelta());

      if (stream_timestamp + buffer->duration() < base::TimeDelta()) {
        DCHECK_EQ(buffer->discard_padding().second, base::TimeDelta());

        // Discard the entire packet if it's entirely before zero.
        buffer->set_discard_padding(
            std::make_pair(kInfiniteDuration, base::TimeDelta()));
      } else {
        // Only discard part of the frame if it overlaps zero.
        buffer->set_discard_padding(std::make_pair(
            -stream_timestamp, buffer->discard_padding().second));
      }
    } else {
      // Verify that codec delay would cover discard and that we don't need to
      // mark the packet for post decode discard.  Since timestamps may be in
      // milliseconds and codec delay in nanosecond precision, round up to the
      // nearest millisecond.  See enable_negative_timestamp_fixups().
      DCHECK_LE(-std::ceil(FramesToTimeDelta(
                               audio_decoder_config().codec_delay(),
                               audio_decoder_config().samples_per_second())
                               .InMillisecondsF()),
                stream_timestamp.InMillisecondsF());
    }
  }

  if (last_packet_timestamp_ != kNoTimestamp) {
    // FFmpeg doesn't support chained ogg correctly.  Instead of guaranteeing
    // continuity across links in the chain it uses the timestamp information
    // from each link directly.  Doing so can lead to timestamps which appear to
    // go backwards in time.
    //
    // If the new link starts with a negative timestamp or a timestamp less than
    // the original (positive) |start_time|, we will get a negative timestamp
    // here.
    //
    // Fixing chained ogg is non-trivial, so for now just reuse the last good
    // timestamp.  The decoder will rewrite the timestamps to be sample accurate
    // later.  See http://crbug.com/396864.
    if (fixup_negative_timestamps_ &&
        buffer->timestamp() < last_packet_timestamp_) {
      buffer->set_timestamp(last_packet_timestamp_ +
                            (last_packet_duration_ != kNoTimestamp
                                 ? last_packet_duration_
                                 : base::TimeDelta::FromMicroseconds(1)));
    }

    // The demuxer should always output positive timestamps.
    DCHECK(buffer->timestamp() >= base::TimeDelta());

    if (last_packet_timestamp_ < buffer->timestamp()) {
      buffered_ranges_.Add(last_packet_timestamp_, buffer->timestamp());
      demuxer_->NotifyBufferingChanged();
    }
  }

  if (packet.get()->flags & AV_PKT_FLAG_KEY)
    buffer->set_is_key_frame(true);

  last_packet_timestamp_ = buffer->timestamp();
  last_packet_duration_ = buffer->duration();

  buffer_queue_.Push(buffer);
  SatisfyPendingRead();
}

void FFmpegDemuxerStream::SetEndOfStream() {
  DCHECK(task_runner_->BelongsToCurrentThread());
  end_of_stream_ = true;
  SatisfyPendingRead();
}

void FFmpegDemuxerStream::FlushBuffers() {
  DCHECK(task_runner_->BelongsToCurrentThread());
  DCHECK(read_cb_.is_null()) << "There should be no pending read";

  // H264 and AAC require that we resend the header after flush.
  // Reset bitstream for converter to do so.
  // This is related to chromium issue 140371 (http://crbug.com/140371).
  ResetBitstreamConverter();

  buffer_queue_.Clear();
  end_of_stream_ = false;
  last_packet_timestamp_ = kNoTimestamp;
  last_packet_duration_ = kNoTimestamp;
  aborted_ = false;
}

void FFmpegDemuxerStream::Abort() {
  aborted_ = true;
  if (!read_cb_.is_null())
    base::ResetAndReturn(&read_cb_).Run(DemuxerStream::kAborted, nullptr);
}

void FFmpegDemuxerStream::Stop() {
  DCHECK(task_runner_->BelongsToCurrentThread());
  buffer_queue_.Clear();
  if (!read_cb_.is_null()) {
    base::ResetAndReturn(&read_cb_).Run(
        DemuxerStream::kOk, DecoderBuffer::CreateEOSBuffer());
  }
  demuxer_ = NULL;
  stream_ = NULL;
  end_of_stream_ = true;
}

DemuxerStream::Type FFmpegDemuxerStream::type() const {
  DCHECK(task_runner_->BelongsToCurrentThread());
  return type_;
}

DemuxerStream::Liveness FFmpegDemuxerStream::liveness() const {
  DCHECK(task_runner_->BelongsToCurrentThread());
  return liveness_;
}

void FFmpegDemuxerStream::Read(const ReadCB& read_cb) {
  DCHECK(task_runner_->BelongsToCurrentThread());
  CHECK(read_cb_.is_null()) << "Overlapping reads are not supported";
  read_cb_ = BindToCurrentLoop(read_cb);

  // Don't accept any additional reads if we've been told to stop.
  // The |demuxer_| may have been destroyed in the pipeline thread.
  //
  // TODO(scherkus): it would be cleaner to reply with an error message.
  if (!demuxer_) {
    base::ResetAndReturn(&read_cb_).Run(
        DemuxerStream::kOk, DecoderBuffer::CreateEOSBuffer());
    return;
  }

  if (!is_enabled_) {
    DVLOG(1) << "Read from disabled stream, returning EOS";
    base::ResetAndReturn(&read_cb_).Run(kOk, DecoderBuffer::CreateEOSBuffer());
    return;
  }

  if (aborted_) {
    base::ResetAndReturn(&read_cb_).Run(kAborted, nullptr);
    return;
  }

  SatisfyPendingRead();
}

void FFmpegDemuxerStream::EnableBitstreamConverter() {
  DCHECK(task_runner_->BelongsToCurrentThread());

#if defined(USE_PROPRIETARY_CODECS)
  InitBitstreamConverter();
#else
  NOTREACHED() << "Proprietary codecs not enabled.";
#endif
}

void FFmpegDemuxerStream::ResetBitstreamConverter() {
#if defined(USE_PROPRIETARY_CODECS)
  if (bitstream_converter_)
    InitBitstreamConverter();
#endif  // defined(USE_PROPRIETARY_CODECS)
}

void FFmpegDemuxerStream::InitBitstreamConverter() {
#if defined(USE_PROPRIETARY_CODECS)
  switch (stream_->codecpar->codec_id) {
    case AV_CODEC_ID_H264:
      // Clear |extra_data| so that future (fallback) decoders will know that
      // conversion is forcibly enabled on this stream.
      //
      // TODO(sandersd): Ideally we would convert |extra_data| to concatenated
      // SPS/PPS data, but it's too late to be useful because Initialize() was
      // already called on GpuVideoDecoder, which is the only path that would
      // consume that data.
      if (video_config_)
        video_config_->SetExtraData(std::vector<uint8_t>());
      bitstream_converter_.reset(
          new FFmpegH264ToAnnexBBitstreamConverter(stream_->codecpar));
      break;
#if BUILDFLAG(ENABLE_HEVC_DEMUXING)
    case AV_CODEC_ID_HEVC:
      bitstream_converter_.reset(
          new FFmpegH265ToAnnexBBitstreamConverter(stream_->codecpar));
      break;
#endif
    case AV_CODEC_ID_AAC:
      bitstream_converter_.reset(
          new FFmpegAACBitstreamConverter(stream_->codecpar));
      break;
    default:
      break;
  }
#endif  // defined(USE_PROPRIETARY_CODECS)
}

bool FFmpegDemuxerStream::SupportsConfigChanges() { return false; }

AudioDecoderConfig FFmpegDemuxerStream::audio_decoder_config() {
  DCHECK(task_runner_->BelongsToCurrentThread());
  DCHECK_EQ(type_, AUDIO);
  DCHECK(audio_config_.get());
  return *audio_config_;
}

VideoDecoderConfig FFmpegDemuxerStream::video_decoder_config() {
  DCHECK(task_runner_->BelongsToCurrentThread());
  DCHECK_EQ(type_, VIDEO);
  DCHECK(video_config_.get());
  return *video_config_;
}

VideoRotation FFmpegDemuxerStream::video_rotation() {
  return video_rotation_;
}

bool FFmpegDemuxerStream::enabled() const {
  DCHECK(task_runner_->BelongsToCurrentThread());
  return is_enabled_;
}

void FFmpegDemuxerStream::set_enabled(bool enabled, base::TimeDelta timestamp) {
  DCHECK(task_runner_->BelongsToCurrentThread());
  if (enabled == is_enabled_)
    return;

  is_enabled_ = enabled;
  if (is_enabled_) {
    waiting_for_keyframe_ = true;
  }
  if (!is_enabled_ && !read_cb_.is_null()) {
    DVLOG(1) << "Read from disabled stream, returning EOS";
    base::ResetAndReturn(&read_cb_).Run(kOk, DecoderBuffer::CreateEOSBuffer());
    return;
  }
  if (!stream_status_change_cb_.is_null())
    stream_status_change_cb_.Run(is_enabled_, timestamp);
}

void FFmpegDemuxerStream::SetStreamStatusChangeCB(
    const StreamStatusChangeCB& cb) {
  DCHECK(!cb.is_null());
  stream_status_change_cb_ = cb;
}

void FFmpegDemuxerStream::SetLiveness(Liveness liveness) {
  DCHECK(task_runner_->BelongsToCurrentThread());
  DCHECK_EQ(liveness_, LIVENESS_UNKNOWN);
  liveness_ = liveness;
}

base::TimeDelta FFmpegDemuxerStream::GetElapsedTime() const {
  return ConvertStreamTimestamp(stream_->time_base, stream_->cur_dts);
}

Ranges<base::TimeDelta> FFmpegDemuxerStream::GetBufferedRanges() const {
  return buffered_ranges_;
}

void FFmpegDemuxerStream::SatisfyPendingRead() {
  DCHECK(task_runner_->BelongsToCurrentThread());
  if (!read_cb_.is_null()) {
    if (!buffer_queue_.IsEmpty()) {
      base::ResetAndReturn(&read_cb_).Run(
          DemuxerStream::kOk, buffer_queue_.Pop());
    } else if (end_of_stream_) {
      base::ResetAndReturn(&read_cb_).Run(
          DemuxerStream::kOk, DecoderBuffer::CreateEOSBuffer());
    }
  }

  // Have capacity? Ask for more!
  if (HasAvailableCapacity() && !end_of_stream_) {
    demuxer_->NotifyCapacityAvailable();
  }
}

bool FFmpegDemuxerStream::HasAvailableCapacity() {
  // TODO(scherkus): Remove this return and reenable time-based capacity
  // after our data sources support canceling/concurrent reads, see
  // http://crbug.com/165762 for details.
#if 1
  return !read_cb_.is_null();
#else
  // Try to have one second's worth of encoded data per stream.
  const base::TimeDelta kCapacity = base::TimeDelta::FromSeconds(1);
  return buffer_queue_.IsEmpty() || buffer_queue_.Duration() < kCapacity;
#endif
}

size_t FFmpegDemuxerStream::MemoryUsage() const {
  return buffer_queue_.data_size();
}

TextKind FFmpegDemuxerStream::GetTextKind() const {
  DCHECK_EQ(type_, DemuxerStream::TEXT);

  if (stream_->disposition & AV_DISPOSITION_CAPTIONS)
    return kTextCaptions;

  if (stream_->disposition & AV_DISPOSITION_DESCRIPTIONS)
    return kTextDescriptions;

  if (stream_->disposition & AV_DISPOSITION_METADATA)
    return kTextMetadata;

  return kTextSubtitles;
}

std::string FFmpegDemuxerStream::GetMetadata(const char* key) const {
  const AVDictionaryEntry* entry =
      av_dict_get(stream_->metadata, key, NULL, 0);
  return (entry == NULL || entry->value == NULL) ? "" : entry->value;
}

// static
base::TimeDelta FFmpegDemuxerStream::ConvertStreamTimestamp(
    const AVRational& time_base,
    int64_t timestamp) {
  if (timestamp == static_cast<int64_t>(AV_NOPTS_VALUE))
    return kNoTimestamp;

  return ConvertFromTimeBase(time_base, timestamp);
}

//
// FFmpegDemuxer
//
FFmpegDemuxer::FFmpegDemuxer(
    const scoped_refptr<base::SingleThreadTaskRunner>& task_runner,
    DataSource* data_source,
    const EncryptedMediaInitDataCB& encrypted_media_init_data_cb,
    const MediaTracksUpdatedCB& media_tracks_updated_cb,
    const scoped_refptr<MediaLog>& media_log)
    : host_(NULL),
      task_runner_(task_runner),
      blocking_thread_("FFmpegDemuxer"),
      pending_read_(false),
      data_source_(data_source),
      media_log_(media_log),
      bitrate_(0),
      start_time_(kNoTimestamp),
      text_enabled_(false),
      duration_known_(false),
      encrypted_media_init_data_cb_(encrypted_media_init_data_cb),
      media_tracks_updated_cb_(media_tracks_updated_cb),
      cancel_pending_seek_factory_(this),
      weak_factory_(this) {
  DCHECK(task_runner_.get());
  DCHECK(data_source_);
  DCHECK(!media_tracks_updated_cb_.is_null());
}

FFmpegDemuxer::~FFmpegDemuxer() {
  // NOTE: This class is not destroyed on |task_runner|, so we must ensure that
  // there are no outstanding WeakPtrs by the time we reach here.
  DCHECK(!weak_factory_.HasWeakPtrs());
}

std::string FFmpegDemuxer::GetDisplayName() const {
  return "FFmpegDemuxer";
}

void FFmpegDemuxer::Initialize(DemuxerHost* host,
                               const PipelineStatusCB& status_cb,
                               bool enable_text_tracks) {
  DCHECK(task_runner_->BelongsToCurrentThread());
  host_ = host;
  text_enabled_ = enable_text_tracks;
  weak_this_ = cancel_pending_seek_factory_.GetWeakPtr();

  url_protocol_.reset(new BlockingUrlProtocol(
      data_source_,
      BindToCurrentLoop(base::Bind(&FFmpegDemuxer::OnDataSourceError,
                                   base::Unretained(this)))));
  glue_.reset(new FFmpegGlue(url_protocol_.get()));
  AVFormatContext* format_context = glue_->format_context();

  // Disable ID3v1 tag reading to avoid costly seeks to end of file for data we
  // don't use.  FFmpeg will only read ID3v1 tags if no other metadata is
  // available, so add a metadata entry to ensure some is always present.
  av_dict_set(&format_context->metadata, "skip_id3v1_tags", "", 0);

  // Ensure ffmpeg doesn't give up too early while looking for stream params;
  // this does not increase the amount of data downloaded.  The default value
  // is 5 AV_TIME_BASE units (1 second each), which prevents some oddly muxed
  // streams from being detected properly; this value was chosen arbitrarily.
  format_context->max_analyze_duration = 60 * AV_TIME_BASE;

  // Open the AVFormatContext using our glue layer.
  CHECK(blocking_thread_.Start());
  base::PostTaskAndReplyWithResult(
      blocking_thread_.task_runner().get(), FROM_HERE,
      base::Bind(&FFmpegGlue::OpenContext, base::Unretained(glue_.get())),
      base::Bind(&FFmpegDemuxer::OnOpenContextDone, weak_factory_.GetWeakPtr(),
                 status_cb));
}

void FFmpegDemuxer::AbortPendingReads() {
  DCHECK(task_runner_->BelongsToCurrentThread());

  // If Stop() has been called, then drop this call.
  if (!blocking_thread_.IsRunning())
    return;

  // This should only be called after the demuxer has been initialized.
  DCHECK_GT(streams_.size(), 0u);

  // Abort all outstanding reads.
  for (const auto& stream : streams_) {
    if (stream)
      stream->Abort();
  }

  // It's important to invalidate read/seek completion callbacks to avoid any
  // errors that occur because of the data source abort.
  weak_factory_.InvalidateWeakPtrs();
  data_source_->Abort();

  // Aborting the read may cause EOF to be marked, undo this.
  blocking_thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&UnmarkEndOfStream, glue_->format_context()));
  pending_read_ = false;

  // TODO(dalecurtis): We probably should report PIPELINE_ERROR_ABORT here
  // instead to avoid any preroll work that may be started upon return, but
  // currently the PipelineImpl does not know how to handle this.
  if (!pending_seek_cb_.is_null())
    base::ResetAndReturn(&pending_seek_cb_).Run(PIPELINE_OK);
}

void FFmpegDemuxer::Stop() {
  DCHECK(task_runner_->BelongsToCurrentThread());

  // The order of Stop() and Abort() is important here.  If Abort() is called
  // first, control may pass into FFmpeg where it can destruct buffers that are
  // in the process of being fulfilled by the DataSource.
  data_source_->Stop();
  url_protocol_->Abort();

  // This will block until all tasks complete. Note that after this returns it's
  // possible for reply tasks (e.g., OnReadFrameDone()) to be queued on this
  // thread. Each of the reply task methods must check whether we've stopped the
  // thread and drop their results on the floor.
  blocking_thread_.Stop();

  for (const auto& stream : streams_) {
    if (stream)
      stream->Stop();
  }

  data_source_ = NULL;

  // Invalidate WeakPtrs on |task_runner_|, destruction may happen on another
  // thread.
  weak_factory_.InvalidateWeakPtrs();
  cancel_pending_seek_factory_.InvalidateWeakPtrs();
}

void FFmpegDemuxer::StartWaitingForSeek(base::TimeDelta seek_time) {}

void FFmpegDemuxer::CancelPendingSeek(base::TimeDelta seek_time) {
  if (task_runner_->BelongsToCurrentThread()) {
    AbortPendingReads();
  } else {
    // Don't use GetWeakPtr() here since we are on the wrong thread.
    task_runner_->PostTask(
        FROM_HERE, base::Bind(&FFmpegDemuxer::AbortPendingReads, weak_this_));
  }
}

void FFmpegDemuxer::Seek(base::TimeDelta time, const PipelineStatusCB& cb) {
  DCHECK(task_runner_->BelongsToCurrentThread());
  CHECK(pending_seek_cb_.is_null());

  // FFmpeg requires seeks to be adjusted according to the lowest starting time.
  // Since EnqueuePacket() rebased negative timestamps by the start time, we
  // must correct the shift here.
  //
  // Additionally, to workaround limitations in how we expose seekable ranges to
  // Blink (http://crbug.com/137275), we also want to clamp seeks before the
  // start time to the start time.
  base::TimeDelta seek_time = start_time_ < base::TimeDelta()
                                  ? time + start_time_
                                  : time < start_time_ ? start_time_ : time;

  // When seeking in an opus stream we need to ensure we deliver enough data to
  // satisfy the seek preroll; otherwise the audio at the actual seek time will
  // not be entirely accurate.
  FFmpegDemuxerStream* audio_stream = GetFFmpegStream(DemuxerStream::AUDIO);
  if (audio_stream) {
    const AudioDecoderConfig& config = audio_stream->audio_decoder_config();
    if (config.codec() == kCodecOpus)
      seek_time = std::max(start_time_, seek_time - config.seek_preroll());
  }

  // Choose the seeking stream based on whether it contains the seek time, if no
  // match can be found prefer the preferred stream.
  //
  // TODO(dalecurtis): Currently FFmpeg does not ensure that all streams in a
  // given container will demux all packets after the seek point.  Instead it
  // only guarantees that all packets after the file position of the seek will
  // be demuxed.  It's an open question whether FFmpeg should fix this:
  // http://lists.ffmpeg.org/pipermail/ffmpeg-devel/2014-June/159212.html
  // Tracked by http://crbug.com/387996.
  FFmpegDemuxerStream* demux_stream = FindPreferredStreamForSeeking(seek_time);
  DCHECK(demux_stream);
  const AVStream* seeking_stream = demux_stream->av_stream();
  DCHECK(seeking_stream);

  pending_seek_cb_ = cb;
  base::PostTaskAndReplyWithResult(
      blocking_thread_.task_runner().get(), FROM_HERE,
      base::Bind(&av_seek_frame, glue_->format_context(), seeking_stream->index,
                 ConvertToTimeBase(seeking_stream->time_base, seek_time),
                 // Always seek to a timestamp <= to the desired timestamp.
                 AVSEEK_FLAG_BACKWARD),
      base::Bind(&FFmpegDemuxer::OnSeekFrameDone, weak_factory_.GetWeakPtr()));
}

base::Time FFmpegDemuxer::GetTimelineOffset() const {
  return timeline_offset_;
}

DemuxerStream* FFmpegDemuxer::GetStream(DemuxerStream::Type type) {
  DCHECK(task_runner_->BelongsToCurrentThread());
  return GetFFmpegStream(type);
}

FFmpegDemuxerStream* FFmpegDemuxer::GetFFmpegStream(
    DemuxerStream::Type type) const {
  for (const auto& stream : streams_) {
    if (stream && stream->type() == type && stream->enabled()) {
      return stream.get();
    }
  }
  return NULL;
}

base::TimeDelta FFmpegDemuxer::GetStartTime() const {
  return std::max(start_time_, base::TimeDelta());
}

void FFmpegDemuxer::AddTextStreams() {
  DCHECK(task_runner_->BelongsToCurrentThread());

  for (const auto& stream : streams_) {
    if (!stream || stream->type() != DemuxerStream::TEXT)
      continue;

    TextKind kind = stream->GetTextKind();
    std::string title = stream->GetMetadata("title");
    std::string language = stream->GetMetadata("language");

    // TODO: Implement "id" metadata in FFMPEG.
    // See: http://crbug.com/323183
    host_->AddTextStream(stream.get(),
                         TextTrackConfig(kind, title, language, std::string()));
  }
}

int64_t FFmpegDemuxer::GetMemoryUsage() const {
  int64_t allocation_size = 0;
  for (const auto& stream : streams_) {
    if (stream)
      allocation_size += stream->MemoryUsage();
  }
  return allocation_size;
}

void FFmpegDemuxer::OnEncryptedMediaInitData(
    EmeInitDataType init_data_type,
    const std::string& encryption_key_id) {
  std::vector<uint8_t> key_id_local(encryption_key_id.begin(),
                                    encryption_key_id.end());
  encrypted_media_init_data_cb_.Run(init_data_type, key_id_local);
}

void FFmpegDemuxer::NotifyCapacityAvailable() {
  DCHECK(task_runner_->BelongsToCurrentThread());
  ReadFrameIfNeeded();
}

void FFmpegDemuxer::NotifyBufferingChanged() {
  DCHECK(task_runner_->BelongsToCurrentThread());
  Ranges<base::TimeDelta> buffered;
  FFmpegDemuxerStream* audio = GetFFmpegStream(DemuxerStream::AUDIO);
  FFmpegDemuxerStream* video = GetFFmpegStream(DemuxerStream::VIDEO);
  if (audio && video) {
    buffered =
        audio->GetBufferedRanges().IntersectionWith(video->GetBufferedRanges());
  } else if (audio) {
    buffered = audio->GetBufferedRanges();
  } else if (video) {
    buffered = video->GetBufferedRanges();
  }
  host_->OnBufferedTimeRangesChanged(buffered);
}

// Helper for calculating the bitrate of the media based on information stored
// in |format_context| or failing that the size and duration of the media.
//
// Returns 0 if a bitrate could not be determined.
static int CalculateBitrate(AVFormatContext* format_context,
                            const base::TimeDelta& duration,
                            int64_t filesize_in_bytes) {
  // If there is a bitrate set on the container, use it.
  if (format_context->bit_rate > 0)
    return format_context->bit_rate;

  // Then try to sum the bitrates individually per stream.
  int bitrate = 0;
  for (size_t i = 0; i < format_context->nb_streams; ++i) {
    AVCodecParameters* codec_parameters = format_context->streams[i]->codecpar;
    bitrate += codec_parameters->bit_rate;
  }
  if (bitrate > 0)
    return bitrate;

  // See if we can approximate the bitrate as long as we have a filesize and
  // valid duration.
  if (duration.InMicroseconds() <= 0 || duration == kInfiniteDuration ||
      filesize_in_bytes == 0) {
    return 0;
  }

  // Do math in floating point as we'd overflow an int64_t if the filesize was
  // larger than ~1073GB.
  double bytes = filesize_in_bytes;
  double duration_us = duration.InMicroseconds();
  return bytes * 8000000.0 / duration_us;
}

void FFmpegDemuxer::OnOpenContextDone(const PipelineStatusCB& status_cb,
                                      bool result) {
  DCHECK(task_runner_->BelongsToCurrentThread());
  if (!blocking_thread_.IsRunning()) {
    MEDIA_LOG(ERROR, media_log_) << GetDisplayName() << ": bad state";
    status_cb.Run(PIPELINE_ERROR_ABORT);
    return;
  }

  if (!result) {
    MEDIA_LOG(ERROR, media_log_) << GetDisplayName() << ": open context failed";
    status_cb.Run(DEMUXER_ERROR_COULD_NOT_OPEN);
    return;
  }

  // Fully initialize AVFormatContext by parsing the stream a little.
  base::PostTaskAndReplyWithResult(
      blocking_thread_.task_runner().get(),
      FROM_HERE,
      base::Bind(&avformat_find_stream_info,
                 glue_->format_context(),
                 static_cast<AVDictionary**>(NULL)),
      base::Bind(&FFmpegDemuxer::OnFindStreamInfoDone,
                 weak_factory_.GetWeakPtr(),
                 status_cb));
}

void FFmpegDemuxer::OnFindStreamInfoDone(const PipelineStatusCB& status_cb,
                                         int result) {
  DCHECK(task_runner_->BelongsToCurrentThread());
  if (!blocking_thread_.IsRunning() || !data_source_) {
    MEDIA_LOG(ERROR, media_log_) << GetDisplayName() << ": bad state";
    status_cb.Run(PIPELINE_ERROR_ABORT);
    return;
  }

  if (result < 0) {
    MEDIA_LOG(ERROR, media_log_) << GetDisplayName()
                                 << ": find stream info failed";
    status_cb.Run(DEMUXER_ERROR_COULD_NOT_PARSE);
    return;
  }

  // Create demuxer stream entries for each possible AVStream. Each stream
  // is examined to determine if it is supported or not (is the codec enabled
  // for it in this release?). Unsupported streams are skipped, allowing for
  // partial playback. At least one audio or video stream must be playable.
  AVFormatContext* format_context = glue_->format_context();
  streams_.resize(format_context->nb_streams);

  // Estimate the start time for each stream by looking through the packets
  // buffered during avformat_find_stream_info().  These values will be
  // considered later when determining the actual stream start time.
  //
  // These packets haven't been completely processed yet, so only look through
  // these values if the AVFormatContext has a valid start time.
  //
  // If no estimate is found, the stream entry will be kInfiniteDuration.
  std::vector<base::TimeDelta> start_time_estimates(format_context->nb_streams,
                                                    kInfiniteDuration);
  const AVFormatInternal* internal = format_context->internal;
  if (internal && internal->packet_buffer &&
      format_context->start_time != static_cast<int64_t>(AV_NOPTS_VALUE)) {
    struct AVPacketList* packet_buffer = internal->packet_buffer;
    while (packet_buffer != internal->packet_buffer_end) {
      DCHECK_LT(static_cast<size_t>(packet_buffer->pkt.stream_index),
                start_time_estimates.size());
      const AVStream* stream =
          format_context->streams[packet_buffer->pkt.stream_index];
      if (packet_buffer->pkt.pts != static_cast<int64_t>(AV_NOPTS_VALUE)) {
        const base::TimeDelta packet_pts =
            ConvertFromTimeBase(stream->time_base, packet_buffer->pkt.pts);
        if (packet_pts < start_time_estimates[stream->index])
          start_time_estimates[stream->index] = packet_pts;
      }
      packet_buffer = packet_buffer->next;
    }
  }

  std::unique_ptr<MediaTracks> media_tracks(new MediaTracks());

  DCHECK(track_id_to_demux_stream_map_.empty());

  // If available, |start_time_| will be set to the lowest stream start time.
  start_time_ = kInfiniteDuration;

  base::TimeDelta max_duration;
  int detected_audio_track_count = 0;
  int detected_video_track_count = 0;
  int detected_text_track_count = 0;
  for (size_t i = 0; i < format_context->nb_streams; ++i) {
    AVStream* stream = format_context->streams[i];
    const AVCodecParameters* codec_parameters = stream->codecpar;
    const AVMediaType codec_type = codec_parameters->codec_type;
    const AVCodecID codec_id = codec_parameters->codec_id;

    if (codec_type == AVMEDIA_TYPE_AUDIO) {
      // Log the codec detected, whether it is supported or not, and whether or
      // not we have already detected a supported codec in another stream.
      UMA_HISTOGRAM_SPARSE_SLOWLY("Media.DetectedAudioCodecHash",
                                  HashCodecName(GetCodecName(codec_id)));
      detected_audio_track_count++;
    } else if (codec_type == AVMEDIA_TYPE_VIDEO) {
      // Log the codec detected, whether it is supported or not, and whether or
      // not we have already detected a supported codec in another stream.
      UMA_HISTOGRAM_SPARSE_SLOWLY("Media.DetectedVideoCodecHash",
                                  HashCodecName(GetCodecName(codec_id)));
      detected_video_track_count++;

#if BUILDFLAG(ENABLE_HEVC_DEMUXING)
      if (codec_id == AV_CODEC_ID_HEVC) {
        // If ffmpeg is built without HEVC parser/decoder support, it will be
        // able to demux HEVC based solely on container-provided information,
        // but unable to get some of the parameters without parsing the stream
        // (e.g. coded size needs to be read from SPS, pixel format is typically
        // deduced from decoder config in hvcC box). These are not really needed
        // when using external decoder (e.g. hardware decoder), so override them
        // to make sure this translates into a valid VideoDecoderConfig. Coded
        // size is overridden in AVStreamToVideoDecoderConfig().
        if (stream->codecpar->format == AV_PIX_FMT_NONE)
          stream->codecpar->format = AV_PIX_FMT_YUV420P;
      }
#endif
    } else if (codec_type == AVMEDIA_TYPE_SUBTITLE) {
      detected_text_track_count++;
      if (codec_id != AV_CODEC_ID_WEBVTT || !text_enabled_) {
        continue;
      }
    } else {
      continue;
    }

    // Attempt to create a FFmpegDemuxerStream from the AVStream. This will
    // return nullptr if the AVStream is invalid. Validity checks will verify
    // things like: codec, channel layout, sample/pixel format, etc...
    std::unique_ptr<FFmpegDemuxerStream> demuxer_stream =
        FFmpegDemuxerStream::Create(this, stream, media_log_);
    if (demuxer_stream.get()) {
      streams_[i] = std::move(demuxer_stream);
    } else {
      if (codec_type == AVMEDIA_TYPE_AUDIO) {
        MEDIA_LOG(INFO, media_log_)
            << GetDisplayName()
            << ": skipping invalid or unsupported audio track";
      } else if (codec_type == AVMEDIA_TYPE_VIDEO) {
        MEDIA_LOG(INFO, media_log_)
            << GetDisplayName()
            << ": skipping invalid or unsupported video track";
      }

      // This AVStream does not successfully convert.
      continue;
    }

    StreamParser::TrackId track_id = stream->id;
    std::string track_label = streams_[i]->GetMetadata("handler_name");
    std::string track_language = streams_[i]->GetMetadata("language");

    // Some metadata is named differently in FFmpeg for webm files.
    if (strstr(format_context->iformat->name, "webm") ||
        strstr(format_context->iformat->name, "matroska")) {
      // TODO(servolk): FFmpeg doesn't set stream->id correctly for webm files.
      // Need to fix that and use it as track id. crbug.com/323183
      track_id =
          static_cast<StreamParser::TrackId>(media_tracks->tracks().size() + 1);
      track_label = streams_[i]->GetMetadata("title");
    }

    if ((codec_type == AVMEDIA_TYPE_AUDIO &&
         media_tracks->getAudioConfig(track_id).IsValidConfig()) ||
        (codec_type == AVMEDIA_TYPE_VIDEO &&
         media_tracks->getVideoConfig(track_id).IsValidConfig())) {
      MEDIA_LOG(INFO, media_log_)
          << GetDisplayName()
          << ": skipping duplicate media stream id=" << track_id;
      continue;
    }

    // Note when we find our audio/video stream (we only want one of each) and
    // record src= playback UMA stats for the stream's decoder config.
    MediaTrack* media_track = nullptr;
    if (codec_type == AVMEDIA_TYPE_AUDIO) {
      AudioDecoderConfig audio_config = streams_[i]->audio_decoder_config();
      RecordAudioCodecStats(audio_config);

      media_track = media_tracks->AddAudioTrack(audio_config, track_id, "main",
                                                track_label, track_language);
      media_track->set_id(base::UintToString(track_id));
      DCHECK(track_id_to_demux_stream_map_.find(media_track->id()) ==
             track_id_to_demux_stream_map_.end());
      track_id_to_demux_stream_map_[media_track->id()] = streams_[i].get();
    } else if (codec_type == AVMEDIA_TYPE_VIDEO) {
      VideoDecoderConfig video_config = streams_[i]->video_decoder_config();

      RecordVideoCodecStats(video_config, stream->codecpar->color_range,
                            media_log_.get());

      media_track = media_tracks->AddVideoTrack(video_config, track_id, "main",
                                                track_label, track_language);
      media_track->set_id(base::UintToString(track_id));
      DCHECK(track_id_to_demux_stream_map_.find(media_track->id()) ==
             track_id_to_demux_stream_map_.end());
      track_id_to_demux_stream_map_[media_track->id()] = streams_[i].get();
    }

    max_duration = std::max(max_duration, streams_[i]->duration());

    const base::TimeDelta start_time =
        ExtractStartTime(stream, start_time_estimates[i]);
    streams_[i]->set_start_time(start_time);
    if (start_time < start_time_) {
      start_time_ = start_time;
    }
  }

  RecordDetectedTrackTypeStats(detected_audio_track_count,
                               detected_video_track_count,
                               detected_text_track_count);

  if (media_tracks->tracks().empty()) {
    MEDIA_LOG(ERROR, media_log_) << GetDisplayName()
                                 << ": no supported streams";
    status_cb.Run(DEMUXER_ERROR_NO_SUPPORTED_STREAMS);
    return;
  }

  if (text_enabled_)
    AddTextStreams();

  if (format_context->duration != static_cast<int64_t>(AV_NOPTS_VALUE)) {
    // If there is a duration value in the container use that to find the
    // maximum between it and the duration from A/V streams.
    const AVRational av_time_base = {1, AV_TIME_BASE};
    max_duration =
        std::max(max_duration,
                 ConvertFromTimeBase(av_time_base, format_context->duration));
  } else {
    // The duration is unknown, in which case this is likely a live stream.
    max_duration = kInfiniteDuration;
  }

  // FFmpeg represents audio data marked as before the beginning of stream as
  // having negative timestamps.  This data must be discarded after it has been
  // decoded, not before since it is used to warmup the decoder.  There are
  // currently two known cases for this: vorbis in ogg and opus in ogg and webm.
  //
  // For API clarity, it was decided that the rest of the media pipeline should
  // not be exposed to negative timestamps.  Which means we need to rebase these
  // negative timestamps and mark them for discard post decoding.
  //
  // Post-decode frame dropping for packets with negative timestamps is outlined
  // in section A.2 in the Ogg Vorbis spec:
  // http://xiph.org/vorbis/doc/Vorbis_I_spec.html
  //
  // FFmpeg's use of negative timestamps for opus pre-skip is nonstandard, but
  // for more information on pre-skip see section 4.2 of the Ogg Opus spec:
  // https://tools.ietf.org/html/draft-ietf-codec-oggopus-08#section-4.2
  for (const auto& stream : streams_) {
    if (!stream || stream->type() != DemuxerStream::AUDIO)
      continue;
    const AVStream* audio_stream = stream->av_stream();
    DCHECK(audio_stream);
    if (audio_stream->codecpar->codec_id == AV_CODEC_ID_OPUS ||
        (strcmp(format_context->iformat->name, "ogg") == 0 &&
         audio_stream->codecpar->codec_id == AV_CODEC_ID_VORBIS)) {
      for (size_t i = 0; i < streams_.size(); ++i) {
        if (!streams_[i])
          continue;
        streams_[i]->enable_negative_timestamp_fixups();

        // Fixup the seeking information to avoid selecting the audio stream
        // simply because it has a lower starting time.
        if (streams_[i]->av_stream() == audio_stream &&
            streams_[i]->start_time() < base::TimeDelta()) {
          streams_[i]->set_start_time(base::TimeDelta());
        }
      }
    }
  }

  // If no start time could be determined, default to zero.
  if (start_time_ == kInfiniteDuration) {
    start_time_ = base::TimeDelta();
  }

  // MPEG-4 B-frames cause grief for a simple container like AVI. Enable PTS
  // generation so we always get timestamps, see http://crbug.com/169570
  if (strcmp(format_context->iformat->name, "avi") == 0)
    format_context->flags |= AVFMT_FLAG_GENPTS;

  // For testing purposes, don't overwrite the timeline offset if set already.
  if (timeline_offset_.is_null())
    timeline_offset_ = ExtractTimelineOffset(format_context);

  // Since we're shifting the externally visible start time to zero, we need to
  // adjust the timeline offset to compensate.
  if (!timeline_offset_.is_null() && start_time_ < base::TimeDelta())
    timeline_offset_ += start_time_;

  if (max_duration == kInfiniteDuration && !timeline_offset_.is_null()) {
    SetLiveness(DemuxerStream::LIVENESS_LIVE);
  } else if (max_duration != kInfiniteDuration) {
    SetLiveness(DemuxerStream::LIVENESS_RECORDED);
  } else {
    SetLiveness(DemuxerStream::LIVENESS_UNKNOWN);
  }

  // Good to go: set the duration and bitrate and notify we're done
  // initializing.
  host_->SetDuration(max_duration);
  duration_known_ = (max_duration != kInfiniteDuration);

  int64_t filesize_in_bytes = 0;
  url_protocol_->GetSize(&filesize_in_bytes);
  bitrate_ = CalculateBitrate(format_context, max_duration, filesize_in_bytes);
  if (bitrate_ > 0)
    data_source_->SetBitrate(bitrate_);

  LogMetadata(format_context, max_duration);
  media_tracks_updated_cb_.Run(std::move(media_tracks));

  status_cb.Run(PIPELINE_OK);
}

void FFmpegDemuxer::LogMetadata(AVFormatContext* avctx,
                                base::TimeDelta max_duration) {
  // Use a single MediaLogEvent to batch all parameter updates at once; this
  // prevents throttling of events due to the large number of updates here.
  std::unique_ptr<MediaLogEvent> metadata_event =
      media_log_->CreateEvent(MediaLogEvent::PROPERTY_CHANGE);

  DCHECK_EQ(avctx->nb_streams, streams_.size());
  auto& params = metadata_event->params;
  int audio_track_count = 0;
  int video_track_count = 0;
  for (size_t i = 0; i < streams_.size(); ++i) {
    FFmpegDemuxerStream* stream = streams_[i].get();
    if (!stream)
      continue;
    if (stream->type() == DemuxerStream::AUDIO) {
      ++audio_track_count;
      std::string suffix = "";
      if (audio_track_count > 1)
        suffix = "_track" + base::IntToString(audio_track_count);
      const AVCodecParameters* audio_parameters = avctx->streams[i]->codecpar;
      const AudioDecoderConfig& audio_config = stream->audio_decoder_config();
      params.SetString("audio_codec_name" + suffix,
                       GetCodecName(audio_parameters->codec_id));
      params.SetInteger("audio_channels_count" + suffix,
                        audio_parameters->channels);
      params.SetString("audio_sample_format" + suffix,
                       SampleFormatToString(audio_config.sample_format()));
      params.SetInteger("audio_samples_per_second" + suffix,
                        audio_config.samples_per_second());
    } else if (stream->type() == DemuxerStream::VIDEO) {
      ++video_track_count;
      std::string suffix = "";
      if (video_track_count > 1)
        suffix = "_track" + base::IntToString(video_track_count);
      const AVStream* video_av_stream = avctx->streams[i];
      const AVCodecParameters* video_parameters = video_av_stream->codecpar;
      const VideoDecoderConfig& video_config = stream->video_decoder_config();
      params.SetString("video_codec_name" + suffix,
                       GetCodecName(video_parameters->codec_id));
      params.SetInteger("width" + suffix, video_parameters->width);
      params.SetInteger("height" + suffix, video_parameters->height);

      // AVCodecParameters has no time_base field. We use the one from AVStream
      // here.
      params.SetString(
          "time_base" + suffix,
          base::StringPrintf("%d/%d", video_av_stream->time_base.num,
                             video_av_stream->time_base.den));

      params.SetString("video_format" + suffix,
                       VideoPixelFormatToString(video_config.format()));
      params.SetBoolean("video_is_encrypted" + suffix,
                        video_config.is_encrypted());
    }
  }
  params.SetBoolean("found_audio_stream", (audio_track_count > 0));
  params.SetBoolean("found_video_stream", (video_track_count > 0));
  SetTimeProperty(metadata_event.get(), "max_duration", max_duration);
  SetTimeProperty(metadata_event.get(), "start_time", start_time_);
  metadata_event->params.SetInteger("bitrate", bitrate_);
  media_log_->AddEvent(std::move(metadata_event));
}

FFmpegDemuxerStream* FFmpegDemuxer::FindStreamWithLowestStartTimestamp(
    bool enabled) {
  FFmpegDemuxerStream* lowest_start_time_stream = nullptr;
  for (const auto& stream : streams_) {
    if (!stream || stream->enabled() != enabled)
      continue;
    if (!lowest_start_time_stream ||
        stream->start_time() < lowest_start_time_stream->start_time()) {
      lowest_start_time_stream = stream.get();
    }
  }
  return lowest_start_time_stream;
}

FFmpegDemuxerStream* FFmpegDemuxer::FindPreferredStreamForSeeking(
    base::TimeDelta seek_time) {
  // If we have a selected/enabled video stream and its start time is lower
  // than the |seek_time| or unknown, then always prefer it for seeking.
  FFmpegDemuxerStream* video_stream = nullptr;
  for (const auto& stream : streams_) {
    if (stream && stream->type() == DemuxerStream::VIDEO && stream->enabled()) {
      video_stream = stream.get();
      if (video_stream->start_time() <= seek_time) {
        return video_stream;
      }
      break;
    }
  }

  // If video stream is not present or |seek_time| is lower than the video start
  // time, then try to find an enabled stream with the lowest start time.
  FFmpegDemuxerStream* lowest_start_time_enabled_stream =
      FindStreamWithLowestStartTimestamp(true);
  if (lowest_start_time_enabled_stream &&
      lowest_start_time_enabled_stream->start_time() <= seek_time) {
    return lowest_start_time_enabled_stream;
  }

  // If there's no enabled streams to consider from, try a disabled stream with
  // the lowest known start time.
  FFmpegDemuxerStream* lowest_start_time_disabled_stream =
      FindStreamWithLowestStartTimestamp(false);
  if (lowest_start_time_disabled_stream &&
      lowest_start_time_disabled_stream->start_time() <= seek_time) {
    return lowest_start_time_disabled_stream;
  }

  // Otherwise fall back to any other stream.
  for (const auto& stream : streams_) {
    if (stream)
      return stream.get();
  }

  NOTREACHED();
  return nullptr;
}

void FFmpegDemuxer::OnSeekFrameDone(int result) {
  DCHECK(task_runner_->BelongsToCurrentThread());
  CHECK(!pending_seek_cb_.is_null());

  if (!blocking_thread_.IsRunning()) {
    MEDIA_LOG(ERROR, media_log_) << GetDisplayName() << ": bad state";
    base::ResetAndReturn(&pending_seek_cb_).Run(PIPELINE_ERROR_ABORT);
    return;
  }

  if (result < 0) {
    // Use VLOG(1) instead of NOTIMPLEMENTED() to prevent the message being
    // captured from stdout and contaminates testing.
    // TODO(scherkus): Implement this properly and signal error (BUG=23447).
    VLOG(1) << "Not implemented";
  }

  // Tell streams to flush buffers due to seeking.
  for (const auto& stream : streams_) {
    if (stream)
      stream->FlushBuffers();
  }

  // Resume reading until capacity.
  ReadFrameIfNeeded();

  // Notify we're finished seeking.
  base::ResetAndReturn(&pending_seek_cb_).Run(PIPELINE_OK);
}

void FFmpegDemuxer::OnEnabledAudioTracksChanged(
    const std::vector<MediaTrack::Id>& track_ids,
    base::TimeDelta currTime) {
  DCHECK(task_runner_->BelongsToCurrentThread());

  std::set<DemuxerStream*> enabled_streams;
  for (const auto& id : track_ids) {
    DemuxerStream* stream = track_id_to_demux_stream_map_[id];
    DCHECK(stream);
    DCHECK_EQ(DemuxerStream::AUDIO, stream->type());
    enabled_streams.insert(stream);
  }

  // First disable all streams that need to be disabled and then enable streams
  // that are enabled.
  for (const auto& stream : streams_) {
    if (stream && stream->type() == DemuxerStream::AUDIO &&
        enabled_streams.find(stream.get()) == enabled_streams.end()) {
      DVLOG(1) << __func__ << ": disabling stream " << stream.get();
      stream->set_enabled(false, currTime);
    }
  }
  for (const auto& stream : enabled_streams) {
    DCHECK(stream);
    DVLOG(1) << __func__ << ": enabling stream " << stream;
    stream->set_enabled(true, currTime);
  }
}

void FFmpegDemuxer::OnSelectedVideoTrackChanged(
    const std::vector<MediaTrack::Id>& track_ids,
    base::TimeDelta currTime) {
  DCHECK(task_runner_->BelongsToCurrentThread());
  DCHECK_LE(track_ids.size(), 1u);

  DemuxerStream* selected_stream = nullptr;
  if (!track_ids.empty()) {
    selected_stream = track_id_to_demux_stream_map_[track_ids[0]];
    DCHECK(selected_stream);
    DCHECK_EQ(DemuxerStream::VIDEO, selected_stream->type());
  }

  // First disable all streams that need to be disabled and then enable the
  // stream that needs to be enabled (if any).
  for (const auto& stream : streams_) {
    if (stream && stream->type() == DemuxerStream::VIDEO &&
        stream.get() != selected_stream) {
      DVLOG(1) << __func__ << ": disabling stream " << stream.get();
      stream->set_enabled(false, currTime);
    }
  }
  if (selected_stream) {
    DVLOG(1) << __func__ << ": enabling stream " << selected_stream;
    selected_stream->set_enabled(true, currTime);
  }
}

void FFmpegDemuxer::ReadFrameIfNeeded() {
  DCHECK(task_runner_->BelongsToCurrentThread());

  // Make sure we have work to do before reading.
  if (!blocking_thread_.IsRunning() || !StreamsHaveAvailableCapacity() ||
      pending_read_ || !pending_seek_cb_.is_null()) {
    return;
  }

  // Allocate and read an AVPacket from the media. Save |packet_ptr| since
  // evaluation order of packet.get() and base::Passed(&packet) is
  // undefined.
  ScopedAVPacket packet(new AVPacket());
  AVPacket* packet_ptr = packet.get();

  pending_read_ = true;
  base::PostTaskAndReplyWithResult(
      blocking_thread_.task_runner().get(),
      FROM_HERE,
      base::Bind(&av_read_frame, glue_->format_context(), packet_ptr),
      base::Bind(&FFmpegDemuxer::OnReadFrameDone,
                 weak_factory_.GetWeakPtr(),
                 base::Passed(&packet)));
}

void FFmpegDemuxer::OnReadFrameDone(ScopedAVPacket packet, int result) {
  DCHECK(task_runner_->BelongsToCurrentThread());
  DCHECK(pending_read_);
  pending_read_ = false;

  if (!blocking_thread_.IsRunning() || !pending_seek_cb_.is_null())
    return;

  // Consider the stream as ended if:
  // - either underlying ffmpeg returned an error
  // - or FFMpegDemuxer reached the maximum allowed memory usage.
  if (result < 0 || IsMaxMemoryUsageReached()) {
    DVLOG(1) << __func__ << " result=" << result
             << " IsMaxMemoryUsageReached=" << IsMaxMemoryUsageReached();
    // Update the duration based on the highest elapsed time across all streams
    // if it was previously unknown.
    if (!duration_known_) {
      base::TimeDelta max_duration;

      for (const auto& stream : streams_) {
        if (!stream)
          continue;

        base::TimeDelta duration = stream->GetElapsedTime();
        if (duration != kNoTimestamp && duration > max_duration)
          max_duration = duration;
      }

      if (max_duration > base::TimeDelta()) {
        host_->SetDuration(max_duration);
        duration_known_ = true;
      }
    }
    // If we have reached the end of stream, tell the downstream filters about
    // the event.
    StreamHasEnded();
    return;
  }

  // Queue the packet with the appropriate stream.
  DCHECK_GE(packet->stream_index, 0);
  DCHECK_LT(packet->stream_index, static_cast<int>(streams_.size()));

  // Defend against ffmpeg giving us a bad stream index.
  if (packet->stream_index >= 0 &&
      packet->stream_index < static_cast<int>(streams_.size()) &&
      streams_[packet->stream_index]) {
    // TODO(scherkus): Fix demuxing upstream to never return packets w/o data
    // when av_read_frame() returns success code. See bug comment for ideas:
    //
    // https://code.google.com/p/chromium/issues/detail?id=169133#c10
    if (!packet->data) {
      ScopedAVPacket new_packet(new AVPacket());
      av_new_packet(new_packet.get(), 0);
      av_packet_copy_props(new_packet.get(), packet.get());
      packet.swap(new_packet);
    }

    FFmpegDemuxerStream* demuxer_stream = streams_[packet->stream_index].get();
    if (demuxer_stream->enabled())
      demuxer_stream->EnqueuePacket(std::move(packet));
  }

  // Keep reading until we've reached capacity.
  ReadFrameIfNeeded();
}

bool FFmpegDemuxer::StreamsHaveAvailableCapacity() {
  DCHECK(task_runner_->BelongsToCurrentThread());
  for (const auto& stream : streams_) {
    if (stream && stream->HasAvailableCapacity())
      return true;
  }
  return false;
}

bool FFmpegDemuxer::IsMaxMemoryUsageReached() const {
  DCHECK(task_runner_->BelongsToCurrentThread());

  // Max allowed memory usage, all streams combined.
  const size_t kDemuxerMemoryLimit = 150 * 1024 * 1024;

  size_t memory_left = kDemuxerMemoryLimit;
  for (const auto& stream : streams_) {
    if (!stream)
      continue;

    size_t stream_memory_usage = stream->MemoryUsage();
    if (stream_memory_usage > memory_left)
      return true;
    memory_left -= stream_memory_usage;
  }
  return false;
}

void FFmpegDemuxer::StreamHasEnded() {
  DCHECK(task_runner_->BelongsToCurrentThread());
  for (const auto& stream : streams_) {
    if (stream)
      stream->SetEndOfStream();
  }
}

void FFmpegDemuxer::OnDataSourceError() {
  MEDIA_LOG(ERROR, media_log_) << GetDisplayName() << ": data source error";
  host_->OnDemuxerError(PIPELINE_ERROR_READ);
}

void FFmpegDemuxer::NotifyDemuxerError(PipelineStatus status) {
  MEDIA_LOG(ERROR, media_log_) << GetDisplayName()
                               << ": demuxer error: " << status;
  host_->OnDemuxerError(status);
}

void FFmpegDemuxer::SetLiveness(DemuxerStream::Liveness liveness) {
  DCHECK(task_runner_->BelongsToCurrentThread());
  for (const auto& stream : streams_) {
    if (stream)
      stream->SetLiveness(liveness);
  }
}

}  // namespace media
